#!/usr/bin/env -S python3.15 -E -S -B

"""Materialize actual MEM02 subjects with closed native-only environment bindings."""

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
from tests.host.kernel_mapping_source import (
    OBSERVATION_PREFIX, RECIPE_ADMISSION, SOURCE_PROJECTION,
    current_function_names, observe_source, project_observations, validate_retained_source,
)
from tests.host.work_count_types import Stage
from tools.stack_artifact_files import read_regular
from tools.stack_bound_manifest import owned_artifact


ROOT = Path(__file__).resolve().parents[1]


@lru_cache(maxsize=4)
def fixed_inputs(root):
    root = Path(root)
    reference = SOURCE_PROJECTION["source_reference"]
    entries = subprocess.check_output(
        ("git", "ls-tree", "-r", "-z", reference), cwd=root, timeout=30,
    ).decode().split("\0")
    members = {}
    for entry in filter(None, entries):
        metadata, separator, name = entry.partition("\t")
        fields = metadata.split()
        if not separator or len(fields) != 3 or fields[1] != "blob" or name in members:
            raise ValueError("MEM02 fixed native Git membership is malformed")
        members[name] = (fields[0], fields[2])
    selected = {
        name for name in members
        if name.endswith(".h") and name.startswith(("include/", "arch/", "kernel/"))
    } | set(RECIPE_ADMISSION["native_source_members"])
    if any(members[name][0] not in ("100644", "100755") for name in selected):
        raise ValueError("MEM02 fixed native input is not an ordinary Git blob")
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
                raise ValueError("MEM02 native archive has an unadmitted member")
            body = archive.extractfile(member).read()
            oid = hashlib.sha1(b"blob " + str(len(body)).encode() + b"\0" + body).hexdigest()
            if oid != members[member.name][1]:
                raise ValueError("MEM02 native archive differs from its fixed Git bytes")
            result[member.name] = (body, member.mode & 0o777)
    if set(result) != selected:
        raise ValueError("MEM02 native archive omitted a fixed input")
    return result


def _prefix(observed):
    return (
        (OBSERVATION_PREFIX if observed else "")
        + '#include "tests/host/kernel_mapping_test.h"\n'
        '#include "arch/riscv64/platform.h"\n'
        "#include <stddef.h>\n#include <stdint.h>\n#include <string.h>\n"
        '_Static_assert(sizeof(uintptr_t) == sizeof(uint64_t), "MEM02 requires native LP64");\n'
    )


def _linker_bindings():
    fields = (
        ("__kernel_start", 0), ("__kernel_text_start", 0),
        ("__kernel_text_end", 1), ("__kernel_rodata_start", 1),
        ("__kernel_rodata_end", 2), ("__kernel_writable_start", 2),
        ("__kernel_writable_end", 3), ("__kernel_end", 3),
    )
    return "".join(
        "#define " + name
        + " ((const unsigned char *)(uintptr_t)micros_kernel_mapping_native_linker_bounds()["
        + str(index) + "])\n"
        for name, index in fields
    )


def _definitions(text, names, *, bind_tables=False):
    records = function_records(text)
    if not set(names) <= set(records) or len(set(names)) != len(names):
        raise ValueError("MEM02 native helper closure is missing or duplicated")
    prototypes = "".join(records[name].signature + ";\n" for name in names)
    bodies = []
    for name in names:
        record = records[name]
        body = record.body
        if bind_tables and name == "table_at":
            body = "\n    return micros_kernel_mapping_native_table_at(physical_address);\n"
        bodies.append(record.signature + "{" + body + "}\n")
    return prototypes + "".join(bodies)


def _bootstrap_unit(text, observed):
    from tests.host.selected_release_handoff_source import legacy_binding_prefix

    return (
        _prefix(observed) + legacy_binding_prefix(text)
        + "static struct micros_frame_allocator bootstrap_allocator;\n"
        "static struct micros_frame_ownership bootstrap_ownership;\n"
        "static bool ownership_runtime_ready;\n"
        "static bool native_irq_enabled;\n"
        "#define riscv_irq_is_enabled micros_kernel_mapping_native_irq_enabled\n"
        + _definitions(text, SOURCE_PROJECTION["bootstrap_closure"])
        + "struct micros_frame_allocator *micros_kernel_mapping_native_allocator(void)\n"
        "{ return &bootstrap_allocator; }\n"
        "struct micros_frame_ownership *micros_kernel_mapping_native_ownership(void)\n"
        "{ return &bootstrap_ownership; }\n"
        "void micros_kernel_mapping_native_ready(bool ready)\n"
        "{ ownership_runtime_ready = ready; }\n"
        "void micros_kernel_mapping_native_irq(bool enabled)\n"
        "{ native_irq_enabled = enabled; }\n"
        "bool micros_kernel_mapping_native_irq_enabled(void)\n"
        "{ return native_irq_enabled; }\n"
    )


def _constructor_adapters():
    return (
        "struct micros_kernel_address_space_private_state *micros_kernel_mapping_native_state(void)\n"
        "{ return &kernel_address_space; }\n"
        "const uint64_t *micros_kernel_mapping_native_linker_bounds(void)\n"
        "{ return native_linker_bounds; }\n"
        "void micros_kernel_mapping_native_bounds(const uint64_t bounds[4])\n"
        "{ for (size_t i = 0; i < 4; ++i) native_linker_bounds[i] = bounds[i]; }\n"
        "enum micros_kernel_address_space_error micros_kernel_mapping_native_allocate_table(uint64_t *address)\n"
        "{ return allocate_table(address); }\n"
        "enum micros_kernel_address_space_error micros_kernel_mapping_native_map_range"
        "(uint64_t start, uint64_t end, uint32_t permissions)\n"
        "{ return map_identity_range(start, end, permissions); }\n"
        "enum micros_kernel_address_space_error micros_kernel_mapping_native_construct(void)\n"
        "{\n"
        "    static const uint64_t devices[] = {MICROS_RISCV_UART0_BASE,\n"
        "        MICROS_RISCV_PLIC_PRIORITY_PAGE_BASE, MICROS_RISCV_PLIC_SUPERVISOR_ENABLE_PAGE_BASE,\n"
        "        MICROS_RISCV_PLIC_SUPERVISOR_CONTEXT_PAGE_BASE};\n"
        "    enum micros_kernel_address_space_error error;\n"
        "    if (!linker_layout_is_valid()) return MICROS_KERNEL_ADDRESS_SPACE_ERROR_LINKER_LAYOUT;\n"
        "    error = allocate_table(&kernel_address_space.root_physical_address);\n"
        "    if (error != MICROS_KERNEL_ADDRESS_SPACE_OK) return error;\n"
        "    error = map_kernel_image();\n"
        "    if (error != MICROS_KERNEL_ADDRESS_SPACE_OK) return error;\n"
        "    error = map_managed_memory(micros_bootstrap_frame_allocator());\n"
        "    if (error != MICROS_KERNEL_ADDRESS_SPACE_OK) return error;\n"
        "    for (size_t i = 0; i < sizeof(devices) / sizeof(devices[0]); ++i) {\n"
        "        error = map_identity_range(devices[i], devices[i] + MICROS_SV39_PAGE_SIZE,\n"
        "            MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE);\n"
        "        if (error != MICROS_KERNEL_ADDRESS_SPACE_OK) return error;\n"
        "    }\n"
        "    return MICROS_KERNEL_ADDRESS_SPACE_OK;\n"
        "}\n"
    )


def _constructor_unit(root, text, observed, stage):
    names = (
        ("table_at", *SOURCE_PROJECTION["baseline_constructor_closure"])
        if stage is Stage.RED else tuple(
            name for name in current_function_names(root, "kernel/memory/address_space.c")
            if name != "micros_kernel_address_space_initialize"
        )
    )
    return (
        _prefix(observed) + _linker_bindings()
        + "static struct micros_kernel_address_space_private_state kernel_address_space;\n"
        "static uint64_t native_linker_bounds[4];\n"
        + _definitions(text, names, bind_tables=True)
        + _constructor_adapters()
    )


def _validator_unit(root, text, observed, stage):
    names = (
        SOURCE_PROJECTION["baseline_validator_closure"] if stage is Stage.RED
        else current_function_names(root, "kernel/memory/address_space_validation.c")
    )
    return (
        _prefix(observed) + _linker_bindings()
        + _definitions(text, names, bind_tables=True)
        + "static bool native_reachable[MICROS_KERNEL_PAGE_TABLE_MAX_FRAMES];\n"
        "static uint64_t native_leaf_count;\n"
        "bool micros_kernel_mapping_native_validate(void)\n"
        "{ return micros_kernel_address_space_candidate_is_valid"
        "(micros_kernel_mapping_native_state(), micros_bootstrap_frame_allocator()); }\n"
        "void micros_kernel_mapping_native_prepare_tree(void)\n"
        "{ memset(native_reachable, 0, sizeof(native_reachable)); native_leaf_count = 0; }\n"
        "bool micros_kernel_mapping_native_tree(void)\n"
        "{ return table_tree_is_valid(micros_kernel_mapping_native_state(), native_reachable, &native_leaf_count); }\n"
        "bool micros_kernel_mapping_native_coverage(void)\n"
        "{ return expected_mappings_are_valid"
        "(micros_kernel_mapping_native_state(), micros_bootstrap_frame_allocator()); }\n"
        "bool micros_kernel_mapping_native_lookup(uint64_t va, uint64_t *pa, uint32_t *permissions)\n"
        "{ return lookup_mapping(micros_kernel_mapping_native_state(), va, pa, permissions); }\n"
    )


def source_bundle(root, stage, *, observed=True):
    root = Path(root).resolve()
    if stage not in (Stage.RED, Stage.GREEN) or type(observed) is not bool:
        raise ValueError("MEM02 native source generation has an unknown stage/mode")
    inputs, outputs = {}, {}
    for name, (original, mode) in sorted(fixed_inputs(str(root)).items()):
        data = original
        if stage is Stage.GREEN:
            data, identity = read_regular(regular_source(root, name))
            mode = identity["mode"]
            if data != original and name not in (
                "kernel/memory/address_space.c", "kernel/memory/address_space_validation.c",
                "kernel/memory/address_space_internal.h",
            ):
                from tests.host.selected_ownership_source import native_assertion_admission
                if "MEM02/native-input/" + name not in native_assertion_admission(
                    root, "MEM02", name, data.decode("utf-8"), stage,
                ):
                    raise ValueError("MEM02 corrected native input remains pending its actual pre-production Red")
        inputs[name] = {
            "kind": "file", "mode": mode, "size": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
        }
        text = data.decode("utf-8")
        if name in (
            "kernel/memory/address_space.c", "kernel/memory/address_space_validation.c",
            "kernel/memory/address_space_internal.h",
        ):
            validate_retained_source(root, name, text, stage)
        if observed and name in RECIPE_ADMISSION["generated_subjects"]:
            annotated = observe_source(root, name, text, stage)
            if project_observations(root, name, annotated, stage) != text:
                raise ValueError("MEM02 observations do not recover their exact selected source")
            text = annotated
        if observed:
            from tests.host.object_hart_validation_source import legacy_observe_source, project_legacy_observations

            current = legacy_observe_source(root, "MEM02", name, text, stage)
            if current is not None:
                if project_legacy_observations(root, "MEM02", name, current, stage) != text:
                    raise ValueError("MEM02 current object observations changed their qualified source")
                text = current
        if name == "kernel/memory/bootstrap_memory.c":
            text = _bootstrap_unit(text, observed)
        elif name == "kernel/memory/address_space.c":
            text = _constructor_unit(root, text, observed, stage)
        elif name == "kernel/memory/address_space_validation.c":
            text = _validator_unit(root, text, observed, stage)
        outputs[name] = (text.encode(), mode)
    return inputs, outputs


def prepare_sources(root, directory, stage, *, observed=True):
    root, directory = Path(root).resolve(), Path(directory)
    if directory.is_symlink() or directory.exists() and not directory.is_dir():
        raise ValueError("MEM02 native output is not an owned ordinary directory")
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
    return tuple(directory / name for name in RECIPE_ADMISSION["native_source_members"]), inputs, generated


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.parse_args(argv)
    from tools.python_host_runtime import require_current_runtime

    require_current_runtime()
    if Path.cwd().resolve() != ROOT:
        raise ValueError("MEM02 native generation requires the project root")
    prepare_sources(ROOT, owned_artifact(ROOT, RECIPE_ADMISSION["generator_output"]), Stage.GREEN)
    return 0


if __name__ == "__main__":
    try:
        with checked_source_view(ROOT):
            status = main()
        raise SystemExit(status)
    except (OSError, ValueError, RuntimeError, AssertionError, subprocess.SubprocessError) as error:
        print("MICROS_MEM02_GENERATION_ERROR " + str(error), file=sys.stderr)
        raise SystemExit(2) from error

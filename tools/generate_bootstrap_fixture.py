#!/usr/bin/env python3

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tools import check_user_elf


CONFIG_SYMBOL = "micros_bootstrap_service_config"
VM_BOOT_INFO_SYMBOL = "micros_vm_boot_info"
IMAGE_END_SYMBOL = "__micros_user_image_end"
IMAGE_SEGMENT_COUNT = 3
PAGE_SIZE = 4096
VM_BOOT_INFO_SIZE = 364672


def _format_bytes(data):
    if not data:
        return "    0,\n"
    lines = []
    for offset in range(0, len(data), 12):
        chunk = data[offset : offset + 12]
        lines.append(
            "    " + ", ".join(f"0x{value:02x}" for value in chunk) + ","
        )
    return "\n".join(lines) + "\n"


def _validated_descriptor(image, prefix):
    errors = check_user_elf.validate_elf(image)
    if errors:
        raise check_user_elf.ElfFormatError("; ".join(errors))
    symbols = check_user_elf.defined_symbols(image)
    missing = [
        name
        for name in (CONFIG_SYMBOL, IMAGE_END_SYMBOL)
        if name not in symbols
    ]
    if missing:
        raise check_user_elf.ElfFormatError(
            "missing bootstrap fixture symbols: " + ", ".join(missing)
        )
    loads = sorted(
        (
            program
            for program in image.programs
            if program.program_type == check_user_elf.PROGRAM_LOAD
        ),
        key=lambda program: program.virtual_address,
    )
    if len(loads) != IMAGE_SEGMENT_COUNT:
        raise check_user_elf.ElfFormatError(
            "unexpected bootstrap load-segment count"
        )
    config = symbols[CONFIG_SYMBOL]
    if config.size != 128 or config.value % 8 != 0:
        raise check_user_elf.ElfFormatError(
            "bootstrap configuration has wrong size or alignment"
        )
    containing = [
        program
        for program in loads
        if (
            config.value >= program.virtual_address
            and config.size
                <= program.virtual_address
                + program.memory_size
                - config.value
        )
    ]
    if (
        len(containing) != 1
        or containing[0].flags
            != (
                check_user_elf.PROGRAM_READ
                | check_user_elf.PROGRAM_WRITE
            )
    ):
        raise check_user_elf.ElfFormatError(
            "bootstrap configuration is not wholly writable"
        )
    config_load = containing[0]
    config_offset = config.value - config_load.virtual_address
    for index in range(config.size):
        offset = config_offset + index
        if (
            offset < config_load.file_size
            and image.data[config_load.offset + offset] != 0
        ):
            raise check_user_elf.ElfFormatError(
                "bootstrap configuration initial bytes are nonzero"
            )
    vm_boot_info = symbols.get(VM_BOOT_INFO_SYMBOL)
    vm_boot_info_address = 0
    vm_boot_info_size = 0
    if vm_boot_info is not None:
        if (
            vm_boot_info.size != VM_BOOT_INFO_SIZE
            or vm_boot_info.value % PAGE_SIZE != 0
        ):
            raise check_user_elf.ElfFormatError(
                "VM boot information has wrong size or alignment"
            )
        vm_containing = [
            program
            for program in loads
            if (
                vm_boot_info.value >= program.virtual_address
                and vm_boot_info.size
                    <= program.virtual_address
                    + program.memory_size
                    - vm_boot_info.value
            )
        ]
        if (
            len(vm_containing) != 1
            or vm_containing[0].flags
                != (
                    check_user_elf.PROGRAM_READ
                    | check_user_elf.PROGRAM_WRITE
                )
        ):
            raise check_user_elf.ElfFormatError(
                "VM boot information is not wholly writable"
            )
        if (
            config.value < vm_boot_info.value + vm_boot_info.size
            and vm_boot_info.value < config.value + config.size
        ):
            raise check_user_elf.ElfFormatError(
                "VM boot information overlaps bootstrap configuration"
            )
        vm_load = vm_containing[0]
        vm_offset = vm_boot_info.value - vm_load.virtual_address
        for index in range(vm_boot_info.size):
            offset = vm_offset + index
            if (
                offset < vm_load.file_size
                and image.data[vm_load.offset + offset] != 0
            ):
                raise check_user_elf.ElfFormatError(
                    "VM boot information initial bytes are nonzero"
                )
        vm_boot_info_address = vm_boot_info.value
        vm_boot_info_size = vm_boot_info.size
    page_count = sum(program.memory_size // PAGE_SIZE for program in loads)
    return {
        "prefix": prefix,
        "image": image,
        "symbols": symbols,
        "loads": loads,
        "config_address": config.value,
        "image_end": symbols[IMAGE_END_SYMBOL].value,
        "page_count": page_count,
        "vm_boot_info_address": vm_boot_info_address,
        "vm_boot_info_size": vm_boot_info_size,
    }


def _render_segment_bytes(descriptor):
    output = []
    image = descriptor["image"]
    prefix = descriptor["prefix"]
    for index, program in enumerate(descriptor["loads"]):
        file_bytes = image.data[
            program.offset : program.offset + program.file_size
        ]
        output.extend(
            (
                f"static const unsigned char {prefix}_segment_{index}[] = {{",
                _format_bytes(file_bytes).rstrip(),
                "};",
                "",
            )
        )
    return output


def _render_image(descriptor, image_id, indent="    "):
    image = descriptor["image"]
    prefix = descriptor["prefix"]
    output = [
        f"{indent}{{",
        f"{indent}    .version = MICROS_BOOTSTRAP_IMAGE_VERSION,",
        f"{indent}    .image_id = {image_id},",
        f"{indent}    .entry = UINT64_C(0x{image.header.entry:016x}),",
        f"{indent}    .segment_count = MICROS_BOOTSTRAP_IMAGE_SEGMENT_COUNT,",
        f"{indent}    .segments = {{",
    ]
    for index, program in enumerate(descriptor["loads"]):
        output.extend(
            (
                f"{indent}        {{",
                f"{indent}            .virtual_address = "
                f"UINT64_C(0x{program.virtual_address:016x}),",
                f"{indent}            .file_size = "
                f"UINT64_C(0x{program.file_size:016x}),",
                f"{indent}            .memory_size = "
                f"UINT64_C(0x{program.memory_size:016x}),",
                f"{indent}            .flags = "
                f"UINT32_C(0x{program.flags:08x}),",
                f"{indent}            .file_bytes = "
                f"{prefix}_segment_{index},",
                f"{indent}        }},",
            )
        )
    output.extend(
        (
            f"{indent}    }},",
            f"{indent}    .config_address = "
            f"UINT64_C(0x{descriptor['config_address']:016x}),",
            f"{indent}    .config_size = "
            "sizeof(struct micros_bootstrap_service_config),",
            f"{indent}    .page_count = {descriptor['page_count']},",
            f"{indent}    .image_end = "
            f"UINT64_C(0x{descriptor['image_end']:016x}),",
            f"{indent}    .vm_boot_info_address = "
            f"UINT64_C(0x{descriptor['vm_boot_info_address']:016x}),",
            f"{indent}    .vm_boot_info_size = "
            f"UINT32_C(0x{descriptor['vm_boot_info_size']:08x}),",
            f"{indent}}},",
        )
    )
    return output


def render_fixture(launcher_image, probe_image):
    launcher = _validated_descriptor(launcher_image, "launcher")
    probe = _validated_descriptor(probe_image, "probe")
    if "micros_bootstrap_probe_report" not in probe["symbols"]:
        raise check_user_elf.ElfFormatError(
            "missing bootstrap probe report symbol"
        )
    launcher_limit = launcher["page_count"] + 2
    probe_limit = probe["page_count"] + 1
    total_limit = launcher_limit + 2 * probe_limit
    output = [
        '#include "kernel/bootstrap_test_fixture.h"',
        "",
        '#include "tests/qemu/bootstrap_protocol.h"',
        "",
    ]
    output.extend(_render_segment_bytes(launcher))
    output.extend(_render_segment_bytes(probe))
    output.extend(
        (
            "const struct micros_bootstrap_image",
            "    micros_bootstrap_test_images[",
            "        MICROS_BOOTSTRAP_TEST_SERVICE_COUNT",
            "    ] = {",
        )
    )
    output.extend(_render_image(launcher, 101))
    output.extend(_render_image(probe, 102))
    output.extend(_render_image(probe, 103))
    output.extend(
        (
            "};",
            "",
            "const struct micros_bootstrap_manifest",
            "    micros_bootstrap_test_manifest = {",
            "        .header = {",
            "            .magic = MICROS_BOOTSTRAP_MANIFEST_MAGIC,",
            "            .version = MICROS_BOOTSTRAP_MANIFEST_VERSION,",
            "            .header_size = MICROS_BOOTSTRAP_MANIFEST_HEADER_SIZE,",
            "            .entry_size = MICROS_BOOTSTRAP_MANIFEST_ENTRY_SIZE,",
            "            .entry_capacity = MICROS_BOOTSTRAP_SERVICE_CAPACITY,",
            "            .entry_count = MICROS_BOOTSTRAP_TEST_SERVICE_COUNT,",
            f"            .total_user_page_limit = {total_limit},",
            "            .manifest_size = MICROS_BOOTSTRAP_MANIFEST_SIZE,",
            "        },",
            "        .entries = {",
            "            {",
            "                .service_id = "
            "MICROS_BOOTSTRAP_TEST_LAST_SERVICE_ID,",
            "                .image_id = 103,",
            "                .process_slot = 2,",
            "                .stack_page_count = 1,",
            "                .profile_id = 3,",
            '                .service_name = "bootstrap-probe-b",',
            '                .profile_name = "BOOTSTRAP_PROBE_B",',
            "                .prerequisites = UINT64_C(1) << 1,",
            "                .ready_timeout_counter_ticks = "
            "MICROS_BOOTSTRAP_TEST_READY_TIMEOUT,",
            f"                .user_page_limit = {probe_limit},",
            "            },",
            "            {",
            "                .service_id = "
            "MICROS_BOOTSTRAP_TEST_LAUNCHER_SERVICE_ID,",
            "                .image_id = 101,",
            "                .process_slot = 0,",
            "                .stack_page_count = 1,",
            "                .profile_id = 1,",
            '                .service_name = "bootstrap-launcher",',
            '                .profile_name = "BOOTSTRAP_LAUNCHER",',
            f"                .user_page_limit = {launcher_limit},",
            "                .role_flags = MICROS_BOOTSTRAP_ROLE_CONTROLLER,",
            "            },",
            "            {",
            "                .service_id = "
            "MICROS_BOOTSTRAP_TEST_FIRST_SERVICE_ID,",
            "                .image_id = 102,",
            "                .process_slot = 1,",
            "                .stack_page_count = 1,",
            "                .profile_id = 2,",
            '                .service_name = "bootstrap-probe-a",',
            '                .profile_name = "BOOTSTRAP_PROBE_A",',
            "                .prerequisites = UINT64_C(1),",
            "                .ready_timeout_counter_ticks = "
            "MICROS_BOOTSTRAP_TEST_READY_TIMEOUT,",
            f"                .user_page_limit = {probe_limit},",
            "            },",
            "        },",
            "    };",
            "",
            "const uint64_t micros_bootstrap_test_probe_report_address =",
            "    UINT64_C("
            f"0x{probe['symbols']['micros_bootstrap_probe_report'].value:016x}"
            ");",
            "",
        )
    )
    return "\n".join(output)


def parse_arguments(argv=None):
    parser = argparse.ArgumentParser(
        description="Generate immutable bootstrap launcher test fixtures."
    )
    parser.add_argument("--launcher-elf", required=True, type=Path)
    parser.add_argument("--probe-elf", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    return parser.parse_args(argv)


def main(argv=None):
    arguments = parse_arguments(argv)
    try:
        launcher = check_user_elf.load_validated_elf(arguments.launcher_elf)
        probe = check_user_elf.load_validated_elf(arguments.probe_elf)
        content = render_fixture(launcher, probe)
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.output.write_text(content, encoding="utf-8")
    except (OSError, check_user_elf.ElfFormatError) as error:
        raise SystemExit(f"bootstrap fixture generation failed: {error}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

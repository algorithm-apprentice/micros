#!/usr/bin/env python3

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tools import check_user_elf


FIXTURE_SYMBOLS = (
    "micros_user_runtime_test_config",
    "micros_user_runtime_test_rodata",
    "micros_user_runtime_test_rodata_fault",
    "micros_user_runtime_test_rodata_resume",
    "micros_runtime_service_returned",
    "__micros_user_data_start",
    "__micros_user_data_end",
    "__micros_user_bss_start",
    "__micros_user_bss_end",
    "__micros_user_image_end",
)
IMAGE_SEGMENT_COUNT = 3


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


def render_fixture(image):
    errors = check_user_elf.validate_elf(image)
    if errors:
        raise check_user_elf.ElfFormatError("; ".join(errors))
    symbols = check_user_elf.defined_symbols(image)
    missing = [name for name in FIXTURE_SYMBOLS if name not in symbols]
    if missing:
        raise check_user_elf.ElfFormatError(
            "missing fixture symbols: " + ", ".join(missing)
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
            "unexpected user-runtime load-segment count"
        )

    def containing_load(symbol_name, size):
        symbol = symbols[symbol_name]
        matches = [
            program
            for program in loads
            if (
                symbol.value >= program.virtual_address
                and size
                    <= program.virtual_address
                    + program.memory_size
                    - symbol.value
            )
        ]
        if len(matches) != 1:
            raise check_user_elf.ElfFormatError(
                f"fixture symbol {symbol_name} is outside one load segment"
            )
        return matches[0]

    config_load = containing_load(
        "micros_user_runtime_test_config",
        symbols["micros_user_runtime_test_config"].size,
    )
    rodata_load = containing_load("micros_user_runtime_test_rodata", 8)
    text_symbols = (
        "micros_user_runtime_test_rodata_fault",
        "micros_user_runtime_test_rodata_resume",
        "micros_runtime_service_returned",
    )
    if config_load.flags != (
        check_user_elf.PROGRAM_READ | check_user_elf.PROGRAM_WRITE
    ):
        raise check_user_elf.ElfFormatError(
            "fixture configuration is not in the writable segment"
        )
    if rodata_load.flags != check_user_elf.PROGRAM_READ:
        raise check_user_elf.ElfFormatError(
            "fixture rodata probe is not in the read-only segment"
        )
    for symbol_name in text_symbols:
        if containing_load(symbol_name, 4).flags != (
            check_user_elf.PROGRAM_READ
            | check_user_elf.PROGRAM_EXECUTE
        ):
            raise check_user_elf.ElfFormatError(
                f"fixture symbol {symbol_name} is not executable"
            )
    output = [
        '#include "kernel/user_runtime_image.h"',
        "",
    ]
    for index, program in enumerate(loads):
        file_bytes = image.data[
            program.offset : program.offset + program.file_size
        ]
        output.extend(
            (
                f"static const unsigned char segment_{index}_bytes[] = {{",
                _format_bytes(file_bytes).rstrip(),
                "};",
                "",
            )
        )
    output.extend(
        (
            "const struct micros_user_runtime_image",
            "    micros_user_runtime_test_image = {",
            f"        .entry = UINT64_C(0x{image.header.entry:016x}),",
            f"        .segment_count = {len(loads)},",
            "        .segments = {",
        )
    )
    for index, program in enumerate(loads):
        output.extend(
            (
                "            {",
                "                .virtual_address = "
                f"UINT64_C(0x{program.virtual_address:016x}),",
                f"                .file_size = UINT64_C(0x{program.file_size:016x}),",
                f"                .memory_size = UINT64_C(0x{program.memory_size:016x}),",
                f"                .flags = UINT32_C(0x{program.flags:08x}),",
                f"                .file_bytes = segment_{index}_bytes,",
                "            },",
            )
        )
    symbol_fields = (
        ("config_address", "micros_user_runtime_test_config"),
        ("rodata_address", "micros_user_runtime_test_rodata"),
        (
            "rodata_fault_address",
            "micros_user_runtime_test_rodata_fault",
        ),
        (
            "rodata_resume_address",
            "micros_user_runtime_test_rodata_resume",
        ),
        (
            "service_returned_address",
            "micros_runtime_service_returned",
        ),
        ("data_start", "__micros_user_data_start"),
        ("data_end", "__micros_user_data_end"),
        ("bss_start", "__micros_user_bss_start"),
        ("bss_end", "__micros_user_bss_end"),
        ("image_end", "__micros_user_image_end"),
    )
    output.extend(("        },",))
    for field, symbol in symbol_fields:
        output.append(
            f"        .{field} = UINT64_C(0x{symbols[symbol].value:016x}),"
        )
    output.extend(("    };", ""))
    return "\n".join(output)


def parse_arguments(argv=None):
    parser = argparse.ArgumentParser(
        description="Generate the bounded kernel fixture for a checked user ELF."
    )
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    return parser.parse_args(argv)


def main(argv=None):
    arguments = parse_arguments(argv)
    try:
        image = check_user_elf.load_validated_elf(arguments.elf)
        content = render_fixture(image)
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.output.write_text(content, encoding="utf-8")
    except (OSError, check_user_elf.ElfFormatError) as error:
        raise SystemExit(f"user runtime fixture generation failed: {error}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

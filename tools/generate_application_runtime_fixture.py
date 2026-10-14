#!/usr/bin/env python3

from __future__ import annotations

import argparse
from pathlib import Path
import struct
import sys

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tools import check_application_elf
from tools.generate_user_runtime_fixture import _format_bytes
from tools.user_elf_format import ElfFormatError, PAGE_SIZE, USER_BASE


def application_request_bytes():
    request = bytearray(256)
    for reference, value in (
        (16, b"/bin/application-probe\0"),
        (32, b"probe\0"),
        (40, b"child\0"),
        (96, b"MODE=runtime\0"),
    ):
        struct.pack_into("<HHI", request, reference, len(request), len(value), 0)
        request.extend(value)
    struct.pack_into("<4I", request, 0, 1, 256, len(request), 0)
    struct.pack_into("<3H", request, 24, 2, 1, 0)
    return bytes(request)


def render_fixture(image):
    errors = check_application_elf.validate_elf(image)
    if errors:
        raise ElfFormatError("; ".join(errors))
    if (
        len(image.programs) != 3
        or any(
            program.memory_size != PAGE_SIZE
            or program.virtual_address != USER_BASE + index * PAGE_SIZE
            for index, program in enumerate(image.programs)
        )
    ):
        raise ElfFormatError("PM application component requires three one-page image segments")
    output = ['#include "tests/qemu/application_runtime_fixture.h"', ""]
    for index, program in enumerate(image.programs):
        data = image.data[program.offset:program.offset + program.file_size]
        output.extend((
            f"static const uint8_t application_segment_{index}_bytes[] = {{",
            _format_bytes(data).rstrip(),
            "};",
            "",
        ))
    output.extend((
        "static const uint8_t application_request_bytes[] = {",
        _format_bytes(application_request_bytes()).rstrip(),
        "};",
        "",
        "const struct micros_application_test_image",
        "    micros_application_runtime_test_image = {",
        f"    .entry = UINT64_C(0x{image.header.entry:016x}),",
        "    .segments = {",
    ))
    for index, program in enumerate(image.programs):
        output.extend((
            "        {",
            f"            .bytes = application_segment_{index}_bytes,",
            f"            .virtual_address = UINT64_C(0x{program.virtual_address:016x}),",
            f"            .memory_size = UINT64_C(0x{program.memory_size:016x}),",
            f"            .file_size = {program.file_size},",
            f"            .elf_flags = {program.flags},",
            "        },",
        ))
    output.extend((
        "    },",
        "    .request_bytes = application_request_bytes,",
        "    .request_size = sizeof(application_request_bytes),",
        "};",
        "",
    ))
    return "\n".join(output)


def main(argv=None):
    parser = argparse.ArgumentParser(description="Embed a checked application for the PM component.")
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    arguments = parser.parse_args(argv)
    try:
        image = check_application_elf.load_validated_elf(arguments.elf)
        output = render_fixture(image)
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.output.write_text(output)
    except (OSError, ElfFormatError, ValueError, struct.error) as error:
        print(f"application fixture generation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


PAGE_SIZE = 4096
REQUIRED_SYMBOLS = (
    "__kernel_start",
    "__kernel_text_start",
    "__kernel_text_end",
    "__kernel_rodata_start",
    "__kernel_rodata_end",
    "__kernel_writable_start",
    "__kernel_writable_end",
    "__kernel_end",
)
FORBIDDEN_DEFINED_SYMBOLS = frozenset(
    (
        "micros_trap_hart_id",
        "timer_state",
    )
)
ALLOWED_SECTION_RANGES = {
    ".text": "text",
    ".rodata": "rodata",
    ".data": "writable",
    ".bss": "writable",
    ".stack": "writable",
    ".thread_kernel_stacks": "writable",
    ".trap_stacks": "writable",
}
SECTION_ROW_START = re.compile(
    r"^\s*\[\s*(?P<index>\d+)\]\s+"
)
SECTION_ROW = re.compile(
    r"^\s*\[\s*\d+\]\s+"
    r"(?P<name>\S+)\s+"
    r"\S+\s+"
    r"(?P<address>[0-9A-Fa-f]+)\s+"
    r"[0-9A-Fa-f]+\s+"
    r"(?P<size>[0-9A-Fa-f]+)\s+"
    r"\S+\s+"
    r"(?P<flags>[A-Za-z]*)\s+"
    r"\d+\s+\d+\s+\d+\s*$"
)


@dataclass(frozen=True)
class ElfSection:
    name: str
    address: int
    size: int
    flags: frozenset[str]


def parse_section_headers(output):
    sections = []
    in_headers = False

    for line in output.splitlines():
        if line.strip() == "Section Headers:":
            in_headers = True
            continue
        if in_headers and line.startswith("Key to Flags:"):
            break
        if not in_headers:
            continue
        row_start = SECTION_ROW_START.match(line)
        if row_start is None:
            continue
        if int(row_start.group("index")) == 0:
            continue

        match = SECTION_ROW.fullmatch(line)
        if match is None:
            raise ValueError(f"malformed section header: {line.strip()}")
        flags = frozenset(match.group("flags"))
        if "A" not in flags:
            continue
        sections.append(
            ElfSection(
                name=match.group("name"),
                address=int(match.group("address"), 16),
                size=int(match.group("size"), 16),
                flags=flags,
            )
        )

    if not in_headers:
        raise ValueError("missing section header table")
    if not sections:
        raise ValueError("no allocatable sections found")
    return sections


def parse_symbol_addresses(output):
    symbols = {}

    for line in output.splitlines():
        fields = line.split()
        if len(fields) < 3 or fields[-1] not in REQUIRED_SYMBOLS:
            continue
        name = fields[-1]
        try:
            address = int(fields[0], 16)
        except ValueError as error:
            raise ValueError(f"malformed symbol address for {name}") from error
        if name in symbols:
            raise ValueError(f"duplicate symbol {name}")
        symbols[name] = address
    return symbols


def parse_defined_symbol_names(output):
    symbols = set()

    for line in output.splitlines():
        fields = line.split()
        if len(fields) >= 3:
            symbols.add(fields[-1])
    return symbols


def validate_forbidden_symbols(symbols):
    return [
        f"forbidden legacy symbol {name}"
        for name in sorted(FORBIDDEN_DEFINED_SYMBOLS.intersection(symbols))
    ]


def _permission_ranges(symbols):
    return (
        (
            "text",
            symbols["__kernel_text_start"],
            symbols["__kernel_text_end"],
            False,
            True,
        ),
        (
            "rodata",
            symbols["__kernel_rodata_start"],
            symbols["__kernel_rodata_end"],
            False,
            False,
        ),
        (
            "writable",
            symbols["__kernel_writable_start"],
            symbols["__kernel_writable_end"],
            True,
            False,
        ),
    )


def validate_allocatable_sections(sections, symbols):
    errors = []
    missing = [name for name in REQUIRED_SYMBOLS if name not in symbols]

    for name in missing:
        errors.append(f"missing symbol {name}")
    if missing:
        return errors

    for name in REQUIRED_SYMBOLS:
        if symbols[name] % PAGE_SIZE != 0:
            errors.append(f"symbol {name} must be page-aligned")

    if symbols["__kernel_start"] != symbols["__kernel_text_start"]:
        errors.append(
            "__kernel_start must equal __kernel_text_start"
        )
    if symbols["__kernel_end"] != symbols["__kernel_writable_end"]:
        errors.append(
            "__kernel_end must equal __kernel_writable_end"
        )

    ranges = _permission_ranges(symbols)
    if not (
        ranges[0][1] < ranges[0][2]
        and ranges[0][2] == ranges[1][1]
        and ranges[1][1] < ranges[1][2]
        and ranges[1][2] == ranges[2][1]
        and ranges[2][1] < ranges[2][2]
    ):
        errors.append("permission ranges are not contiguous and nonempty")

    seen_names = set()
    for section in sections:
        if section.name in seen_names:
            errors.append(f"duplicate allocatable section {section.name}")
            continue
        seen_names.add(section.name)

        expected_range = ALLOWED_SECTION_RANGES.get(section.name)
        if expected_range is None:
            errors.append(
                f"unexpected allocatable section {section.name}"
            )
            continue

        section_end = section.address + section.size
        containing = [
            permission_range
            for permission_range in ranges
            if (
                section.address >= permission_range[1]
                and section_end <= permission_range[2]
            )
        ]
        if not containing:
            intersects = any(
                section.address < permission_range[2]
                and permission_range[1] < section_end
                for permission_range in ranges
            )
            if intersects:
                errors.append(
                    f"section {section.name} crosses permission boundary"
                )
            else:
                errors.append(
                    f"section {section.name} is outside permission ranges"
                )
            continue

        actual_range = containing[0]
        if actual_range[0] != expected_range:
            errors.append(
                f"section {section.name} is in the wrong permission range"
            )
            continue

        has_write = "W" in section.flags
        has_execute = "X" in section.flags
        if has_write != actual_range[3] or has_execute != actual_range[4]:
            errors.append(
                f"section {section.name} flags do not match "
                f"{actual_range[0]} permissions"
            )
    return errors


def _run_tool(command, description):
    completed = subprocess.run(
        command,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    if completed.returncode != 0:
        detail = completed.stderr.strip() or "no diagnostic"
        raise RuntimeError(f"{description} failed: {detail}")
    return completed.stdout


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Validate allocatable ELF sections against kernel ranges."
    )
    parser.add_argument("--readelf", required=True)
    parser.add_argument("--nm", required=True)
    parser.add_argument("--elf", required=True, type=Path)
    arguments = parser.parse_args(argv)

    if not arguments.elf.is_file():
        parser.error(f"ELF image does not exist: {arguments.elf}")

    try:
        sections = parse_section_headers(
            _run_tool(
                [arguments.readelf, "-SW", str(arguments.elf)],
                "section inspection",
            )
        )
        symbol_output = _run_tool(
            [
                arguments.nm,
                "-n",
                "--defined-only",
                str(arguments.elf),
            ],
            "symbol inspection",
        )
        symbols = parse_symbol_addresses(symbol_output)
        defined_symbol_names = parse_defined_symbol_names(symbol_output)
    except (FileNotFoundError, RuntimeError, ValueError) as error:
        print(error, file=sys.stderr)
        return 1

    errors = validate_allocatable_sections(sections, symbols)
    errors.extend(validate_forbidden_symbols(defined_symbol_names))
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

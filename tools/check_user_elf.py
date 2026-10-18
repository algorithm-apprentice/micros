#!/usr/bin/env -S python3.15 -E -S -B

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tools.user_elf_format import (
    ELF_HEADER,
    PROGRAM_HEADER,
    SECTION_HEADER,
    SYMBOL,
    ELF_CLASS_64,
    ELF_DATA_LITTLE,
    ELF_VERSION_CURRENT,
    ELF_TYPE_EXECUTABLE,
    ELF_MACHINE_RISCV,
    ELF_FLAG_RISCV_RVC,
    PROGRAM_LOAD,
    PROGRAM_DYNAMIC,
    PROGRAM_INTERP,
    PROGRAM_TLS,
    PROGRAM_EXECUTE,
    PROGRAM_WRITE,
    PROGRAM_READ,
    SECTION_NULL,
    SECTION_PROGBITS,
    SECTION_SYMTAB,
    SECTION_STRTAB,
    SECTION_RELA,
    SECTION_NOBITS,
    SECTION_REL,
    SECTION_DYNSYM,
    SECTION_WRITE,
    SECTION_ALLOC,
    SECTION_EXECUTE,
    SYMBOL_BIND_GLOBAL,
    SYMBOL_UNDEFINED,
    PAGE_SIZE,
    USER_BASE,
    USER_END,
    MINIMUM_STACK_BOTTOM,
    ElfFormatError,
    ElfHeader,
    ProgramHeader,
    Section,
    ElfSymbol,
    ElfImage,
    _bounded_slice,
    _cstring,
    _defined_symbols,
    defined_symbols,
    _program_file_bytes,
    _section_by_name,
)

from tools.user_elf_parse import (
    _parse_header,
    _parse_programs,
    _parse_raw_sections,
    _parse_sections,
    _parse_symbols,
    parse_elf,
    load_elf,
)

from tools.user_elf_structure import (
    FORBIDDEN_PROGRAM_TYPES,
    FORBIDDEN_SECTION_NAMES,
    FORBIDDEN_SECTION_PREFIXES,
    _validate_header,
    _validate_programs,
    _validate_sections,
)

from tools.user_elf_instructions import (
    _sign_extend,
    _compressed_destination,
    _instruction_destination,
    _instruction_writes,
)

from tools.user_elf_policy import (
    REQUIRED_SYMBOLS,
    _validate_symbols,
    _validate_instructions,
    resident_page_count,
    validate_resident_page_limit,
)


def validate_elf(image: ElfImage):
    errors = []
    errors.extend(_validate_header(image))
    program_errors, loads = _validate_programs(image)
    errors.extend(program_errors)
    errors.extend(_validate_sections(image, loads))
    symbol_errors, symbols = _validate_symbols(image, loads)
    errors.extend(symbol_errors)
    errors.extend(_validate_instructions(image, symbols, loads))
    return errors


def load_validated_elf(path: Path):
    image = load_elf(path)
    errors = validate_elf(image)
    if errors:
        raise ElfFormatError("; ".join(errors))
    return image


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Validate a standalone micros user-service ELF."
    )
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--resident-page-limit", type=int)
    parser.add_argument("--stack-page-count", type=int, default=0)
    arguments = parser.parse_args(argv)
    if (
        arguments.resident_page_limit is None
        and arguments.stack_page_count != 0
    ):
        parser.error(
            "--stack-page-count requires --resident-page-limit"
        )
    if not arguments.elf.is_file():
        parser.error(f"ELF image does not exist: {arguments.elf}")
    try:
        image = load_elf(arguments.elf)
        errors = validate_elf(image)
        if arguments.resident_page_limit is not None:
            errors.extend(
                validate_resident_page_limit(
                    image,
                    arguments.stack_page_count,
                    arguments.resident_page_limit,
                )
            )
    except (OSError, ElfFormatError, ValueError, struct.error) as error:
        print(f"user ELF validation failed: {error}", file=sys.stderr)
        return 1
    if errors:
        for error in errors:
            print(f"user ELF validation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

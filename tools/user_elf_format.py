"""Binary records and checked queries assigned by ADR-0063."""

from __future__ import annotations

import struct
from dataclasses import dataclass


ELF_HEADER = struct.Struct("<16sHHIQQQIHHHHHH")


PROGRAM_HEADER = struct.Struct("<IIQQQQQQ")


SECTION_HEADER = struct.Struct("<IIQQQQIIQQ")


SYMBOL = struct.Struct("<IBBHQQ")


ELF_CLASS_64 = 2


ELF_DATA_LITTLE = 1


ELF_VERSION_CURRENT = 1


ELF_TYPE_EXECUTABLE = 2


ELF_MACHINE_RISCV = 243


ELF_FLAG_RISCV_RVC = 0x1


PROGRAM_LOAD = 1


PROGRAM_DYNAMIC = 2


PROGRAM_INTERP = 3


PROGRAM_TLS = 7


PROGRAM_EXECUTE = 0x1


PROGRAM_WRITE = 0x2


PROGRAM_READ = 0x4


SECTION_NULL = 0


SECTION_PROGBITS = 1


SECTION_SYMTAB = 2


SECTION_STRTAB = 3


SECTION_RELA = 4


SECTION_NOBITS = 8


SECTION_REL = 9


SECTION_DYNSYM = 11


SECTION_WRITE = 0x1


SECTION_ALLOC = 0x2


SECTION_EXECUTE = 0x4


SYMBOL_BIND_GLOBAL = 1


SYMBOL_UNDEFINED = 0


PAGE_SIZE = 4096


USER_BASE = 0x0000000040000000


USER_END = 0x0000000080000000


MINIMUM_STACK_BOTTOM = USER_END - PAGE_SIZE


class ElfFormatError(ValueError):
    pass


@dataclass(frozen=True)
class ElfHeader:
    elf_type: int
    machine: int
    version: int
    entry: int
    program_offset: int
    section_offset: int
    flags: int
    header_size: int
    program_entry_size: int
    program_count: int
    section_entry_size: int
    section_count: int
    section_names_index: int


@dataclass(frozen=True)
class ProgramHeader:
    index: int
    program_type: int
    flags: int
    offset: int
    virtual_address: int
    physical_address: int
    file_size: int
    memory_size: int
    alignment: int


@dataclass(frozen=True)
class Section:
    index: int
    name: str
    section_type: int
    flags: int
    address: int
    offset: int
    size: int
    link: int
    info: int
    alignment: int
    entry_size: int


@dataclass(frozen=True)
class ElfSymbol:
    name: str
    binding: int
    symbol_type: int
    section_index: int
    value: int
    size: int


@dataclass(frozen=True)
class ElfImage:
    data: bytes
    header: ElfHeader
    programs: tuple[ProgramHeader, ...]
    sections: tuple[Section, ...]
    symbols: tuple[ElfSymbol, ...]


def _bounded_slice(data: bytes, offset: int, size: int, description: str):
    if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
        raise ElfFormatError(f"{description} lies outside the ELF file")
    return data[offset : offset + size]


def _cstring(table: bytes, offset: int, description: str):
    if offset < 0 or offset >= len(table):
        raise ElfFormatError(f"{description} string offset is out of range")
    end = table.find(b"\0", offset)
    if end < 0:
        raise ElfFormatError(f"{description} string is unterminated")
    try:
        return table[offset:end].decode("ascii")
    except UnicodeDecodeError as error:
        raise ElfFormatError(f"{description} string is not ASCII") from error


def _defined_symbols(image: ElfImage):
    symbols = {}
    duplicates = set()
    for symbol in image.symbols:
        if not symbol.name or symbol.section_index == SYMBOL_UNDEFINED:
            continue
        if symbol.name.startswith("$"):
            continue
        if symbol.name in symbols:
            duplicates.add(symbol.name)
        else:
            symbols[symbol.name] = symbol
    return symbols, duplicates


def defined_symbols(image: ElfImage):
    symbols, duplicates = _defined_symbols(image)
    if duplicates:
        raise ElfFormatError(
            "duplicate defined symbols: " + ", ".join(sorted(duplicates))
        )
    return symbols


def _program_file_bytes(image: ElfImage, address: int, size: int):
    for program in image.programs:
        if program.program_type != PROGRAM_LOAD:
            continue
        start = program.virtual_address
        end = start + program.file_size
        if address >= start and size <= end - address:
            offset = program.offset + address - start
            return _bounded_slice(
                image.data,
                offset,
                size,
                "requested virtual bytes",
            )
    raise ElfFormatError("requested virtual bytes are not file-backed")


def _section_by_name(image: ElfImage, name: str):
    matches = [section for section in image.sections if section.name == name]
    if len(matches) != 1:
        return None
    return matches[0]

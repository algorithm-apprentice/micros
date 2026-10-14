"""ELF decoding owner assigned by ADR-0063."""

from __future__ import annotations

from pathlib import Path

from tools.user_elf_format import (
    ELF_HEADER,
    PROGRAM_HEADER,
    SECTION_HEADER,
    SYMBOL,
    ELF_CLASS_64,
    ELF_DATA_LITTLE,
    ELF_VERSION_CURRENT,
    SECTION_NULL,
    SECTION_SYMTAB,
    SECTION_STRTAB,
    SECTION_NOBITS,
    SECTION_DYNSYM,
    ElfFormatError,
    ElfHeader,
    ProgramHeader,
    Section,
    ElfSymbol,
    ElfImage,
    _bounded_slice,
    _cstring,
)


def _parse_header(data: bytes):
    if len(data) < ELF_HEADER.size:
        raise ElfFormatError("ELF header is truncated")
    unpacked = ELF_HEADER.unpack_from(data)
    identification = unpacked[0]
    if identification[:4] != b"\x7fELF":
        raise ElfFormatError("ELF magic is invalid")
    if identification[4] != ELF_CLASS_64:
        raise ElfFormatError("ELF class is not ELF64")
    if identification[5] != ELF_DATA_LITTLE:
        raise ElfFormatError("ELF data encoding is not little-endian")
    if identification[6] != ELF_VERSION_CURRENT:
        raise ElfFormatError("ELF identification version is invalid")
    return ElfHeader(
        elf_type=unpacked[1],
        machine=unpacked[2],
        version=unpacked[3],
        entry=unpacked[4],
        program_offset=unpacked[5],
        section_offset=unpacked[6],
        flags=unpacked[7],
        header_size=unpacked[8],
        program_entry_size=unpacked[9],
        program_count=unpacked[10],
        section_entry_size=unpacked[11],
        section_count=unpacked[12],
        section_names_index=unpacked[13],
    )


def _parse_programs(data: bytes, header: ElfHeader):
    if header.program_entry_size != PROGRAM_HEADER.size:
        raise ElfFormatError("ELF program-header entry size is invalid")
    if header.program_count == 0 or header.program_count == 0xFFFF:
        raise ElfFormatError("ELF program-header count is unsupported")
    _bounded_slice(
        data,
        header.program_offset,
        header.program_count * header.program_entry_size,
        "program-header table",
    )
    programs = []
    for index in range(header.program_count):
        offset = header.program_offset + index * header.program_entry_size
        values = PROGRAM_HEADER.unpack_from(data, offset)
        programs.append(
            ProgramHeader(
                index=index,
                program_type=values[0],
                flags=values[1],
                offset=values[2],
                virtual_address=values[3],
                physical_address=values[4],
                file_size=values[5],
                memory_size=values[6],
                alignment=values[7],
            )
        )
    return tuple(programs)


def _parse_raw_sections(data: bytes, header: ElfHeader):
    if header.section_entry_size != SECTION_HEADER.size:
        raise ElfFormatError("ELF section-header entry size is invalid")
    if header.section_count == 0 or header.section_count == 0xFFFF:
        raise ElfFormatError("ELF section-header count is unsupported")
    if header.section_names_index >= header.section_count:
        raise ElfFormatError("ELF section-name table index is invalid")
    _bounded_slice(
        data,
        header.section_offset,
        header.section_count * header.section_entry_size,
        "section-header table",
    )
    return tuple(
        SECTION_HEADER.unpack_from(
            data,
            header.section_offset + index * header.section_entry_size,
        )
        for index in range(header.section_count)
    )


def _parse_sections(data: bytes, header: ElfHeader, raw_sections):
    names_raw = raw_sections[header.section_names_index]
    if names_raw[1] != SECTION_STRTAB:
        raise ElfFormatError("ELF section-name table has the wrong type")
    names = _bounded_slice(
        data,
        names_raw[4],
        names_raw[5],
        "section-name string table",
    )
    sections = []
    for index, values in enumerate(raw_sections):
        name = "" if index == 0 else _cstring(
            names,
            values[0],
            f"section {index}",
        )
        section = Section(
            index=index,
            name=name,
            section_type=values[1],
            flags=values[2],
            address=values[3],
            offset=values[4],
            size=values[5],
            link=values[6],
            info=values[7],
            alignment=values[8],
            entry_size=values[9],
        )
        if (
            section.section_type != SECTION_NOBITS
            and section.section_type != SECTION_NULL
        ):
            _bounded_slice(
                data,
                section.offset,
                section.size,
                f"section {name or index}",
            )
        sections.append(section)
    return tuple(sections)


def _parse_symbols(data: bytes, sections):
    symbols = []
    for section in sections:
        if section.section_type not in (SECTION_SYMTAB, SECTION_DYNSYM):
            continue
        if section.entry_size != SYMBOL.size or section.size % SYMBOL.size != 0:
            raise ElfFormatError(f"symbol table {section.name} is malformed")
        if section.link >= len(sections):
            raise ElfFormatError(
                f"symbol table {section.name} has an invalid string table"
            )
        strings_section = sections[section.link]
        if strings_section.section_type != SECTION_STRTAB:
            raise ElfFormatError(
                f"symbol table {section.name} links a non-string section"
            )
        strings = _bounded_slice(
            data,
            strings_section.offset,
            strings_section.size,
            f"symbol strings for {section.name}",
        )
        for offset in range(0, section.size, SYMBOL.size):
            values = SYMBOL.unpack_from(data, section.offset + offset)
            name = _cstring(
                strings,
                values[0],
                f"symbol {offset // SYMBOL.size}",
            )
            symbols.append(
                ElfSymbol(
                    name=name,
                    binding=values[1] >> 4,
                    symbol_type=values[1] & 0xF,
                    section_index=values[3],
                    value=values[4],
                    size=values[5],
                )
            )
    return tuple(symbols)


def parse_elf(data: bytes):
    header = _parse_header(data)
    programs = _parse_programs(data, header)
    raw_sections = _parse_raw_sections(data, header)
    sections = _parse_sections(data, header, raw_sections)
    symbols = _parse_symbols(data, sections)
    return ElfImage(
        data=data,
        header=header,
        programs=programs,
        sections=sections,
        symbols=symbols,
    )


def load_elf(path: Path):
    return parse_elf(path.read_bytes())

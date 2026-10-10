#!/usr/bin/env python3

from __future__ import annotations

import argparse
import struct
import sys
from dataclasses import dataclass
from pathlib import Path


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

REQUIRED_SYMBOLS = (
    "_start",
    "micros_service_main",
    "micros_runtime_raw_syscall",
    "micros_runtime_service_returned",
    "__micros_user_image_start",
    "__micros_user_text_start",
    "__micros_user_text_end",
    "__micros_user_rodata_start",
    "__micros_user_rodata_end",
    "__micros_user_data_start",
    "__micros_user_data_end",
    "__micros_user_bss_start",
    "__micros_user_bss_end",
    "__micros_user_image_end",
)

FORBIDDEN_PROGRAM_TYPES = {
    PROGRAM_INTERP: "PT_INTERP",
    PROGRAM_DYNAMIC: "PT_DYNAMIC",
    PROGRAM_TLS: "PT_TLS",
}

FORBIDDEN_SECTION_NAMES = frozenset(
    (
        ".dynamic",
        ".dynsym",
        ".dynstr",
        ".hash",
        ".gnu.hash",
        ".plt",
        ".plt.got",
        ".got",
        ".got.plt",
        ".preinit_array",
        ".init_array",
        ".fini_array",
        ".ctors",
        ".dtors",
        ".tdata",
        ".tbss",
        ".sdata",
        ".sbss",
        ".stack",
        ".eh_frame",
        ".eh_frame_hdr",
        ".gcc_except_table",
        ".exception_ranges",
    )
)

FORBIDDEN_SECTION_PREFIXES = (
    ".rel",
    ".rela",
    ".gnu.version",
    ".llvm_prf",
    ".llvm_cov",
    ".asan",
    ".ubsan",
    ".sancov",
    ".prof",
)


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


def _sign_extend(value: int, bits: int):
    sign = 1 << (bits - 1)
    return (value & (sign - 1)) - (value & sign)


def _compressed_destination(instruction: int):
    quadrant = instruction & 0x3
    function = (instruction >> 13) & 0x7
    if quadrant == 0:
        if function in (0, 2, 3):
            return 8 + ((instruction >> 2) & 0x7)
        return None
    if quadrant == 1:
        if function in (0, 1, 2):
            return (instruction >> 7) & 0x1F
        if function == 3:
            return (instruction >> 7) & 0x1F
        if function == 4:
            return 8 + ((instruction >> 7) & 0x7)
        return None
    if quadrant == 2:
        if function in (0, 2, 3):
            return (instruction >> 7) & 0x1F
        if function == 4:
            destination = (instruction >> 7) & 0x1F
            source_two = (instruction >> 2) & 0x1F
            high = (instruction >> 12) & 0x1
            if high == 0 and source_two != 0:
                return destination
            if high == 1 and source_two != 0:
                return destination
            if high == 1 and source_two == 0 and destination != 0:
                return 1
        return None
    return None


def _instruction_destination(instruction: int, size: int):
    if size == 2:
        return _compressed_destination(instruction)
    opcode = instruction & 0x7F
    writing_opcodes = {
        0x03,
        0x0F,
        0x13,
        0x17,
        0x1B,
        0x2F,
        0x33,
        0x37,
        0x3B,
        0x67,
        0x6F,
    }
    if opcode in writing_opcodes:
        return (instruction >> 7) & 0x1F
    if opcode == 0x73 and ((instruction >> 12) & 0x7) != 0:
        return (instruction >> 7) & 0x1F
    return None


def _instruction_writes(address: int, data: bytes):
    writes = []
    offset = 0
    while offset < len(data):
        if len(data) - offset < 2:
            raise ElfFormatError("executable section ends mid-instruction")
        half = int.from_bytes(data[offset : offset + 2], "little")
        size = 4 if half & 0x3 == 0x3 else 2
        if len(data) - offset < size:
            raise ElfFormatError("executable section ends mid-instruction")
        instruction = int.from_bytes(data[offset : offset + size], "little")
        destination = _instruction_destination(instruction, size)
        if destination is not None and destination != 0:
            writes.append((address + offset, destination))
        offset += size
    return writes


def _validate_header(image: ElfImage):
    header = image.header
    errors = []
    if header.elf_type != ELF_TYPE_EXECUTABLE:
        errors.append("ELF type must be ET_EXEC")
    if header.machine != ELF_MACHINE_RISCV:
        errors.append("ELF machine must be EM_RISCV")
    if header.version != ELF_VERSION_CURRENT:
        errors.append("ELF version is invalid")
    if header.header_size != ELF_HEADER.size:
        errors.append("ELF header size is invalid")
    if header.entry != USER_BASE:
        errors.append("ELF entry must equal MICROS_USER_VIRTUAL_BASE")
    if header.flags & ~ELF_FLAG_RISCV_RVC:
        errors.append("ELF flags claim an unsupported RISC-V ABI or extension")
    return errors


def _validate_programs(image: ElfImage):
    errors = []
    for program in image.programs:
        forbidden = FORBIDDEN_PROGRAM_TYPES.get(program.program_type)
        if forbidden is not None:
            errors.append(f"forbidden program header {forbidden}")
    loads = sorted(
        (
            program
            for program in image.programs
            if program.program_type == PROGRAM_LOAD
        ),
        key=lambda program: program.virtual_address,
    )
    if len(loads) != 3:
        errors.append("ELF must contain exactly three PT_LOAD segments")
        return errors, loads
    expected_flags = (
        PROGRAM_READ | PROGRAM_EXECUTE,
        PROGRAM_READ,
        PROGRAM_READ | PROGRAM_WRITE,
    )
    previous_end = None
    for index, (program, flags) in enumerate(zip(loads, expected_flags)):
        if program.flags != flags:
            errors.append(
                f"PT_LOAD {index} has the wrong permission class"
            )
        if program.alignment != PAGE_SIZE:
            errors.append(f"PT_LOAD {index} alignment must be 4096")
        if (
            program.virtual_address % PAGE_SIZE != 0
            or program.offset % PAGE_SIZE != 0
            or program.offset % PAGE_SIZE
                != program.virtual_address % PAGE_SIZE
        ):
            errors.append(f"PT_LOAD {index} has invalid page congruence")
        if program.physical_address != program.virtual_address:
            errors.append(f"PT_LOAD {index} p_paddr must equal p_vaddr")
        if program.file_size > program.memory_size:
            errors.append(f"PT_LOAD {index} has p_filesz greater than p_memsz")
        if (
            program.offset > len(image.data)
            or program.file_size > len(image.data) - program.offset
        ):
            errors.append(f"PT_LOAD {index} file range is truncated")
        if program.memory_size == 0:
            errors.append(f"PT_LOAD {index} is empty")
        end = program.virtual_address + program.memory_size
        if (
            end < program.virtual_address
            or program.virtual_address < USER_BASE
            or end > USER_END
        ):
            errors.append(f"PT_LOAD {index} lies outside the user window")
        if previous_end is not None and program.virtual_address < previous_end:
            errors.append("PT_LOAD segments overlap")
        previous_end = end
        if (
            program.flags & PROGRAM_WRITE
            and program.flags & PROGRAM_EXECUTE
        ):
            errors.append(f"PT_LOAD {index} is writable and executable")
    return errors, loads


def _validate_sections(image: ElfImage, loads):
    errors = []
    names = set()
    for section in image.sections:
        if not section.name:
            continue
        if section.name in names:
            errors.append(f"duplicate section name {section.name}")
        names.add(section.name)
        if (
            section.name in FORBIDDEN_SECTION_NAMES
            or section.name.startswith(FORBIDDEN_SECTION_PREFIXES)
        ):
            errors.append(f"forbidden section {section.name}")
        if section.section_type in (SECTION_REL, SECTION_RELA):
            errors.append(f"relocation section {section.name} is forbidden")
        if section.flags & SECTION_ALLOC:
            section_end = section.address + section.size
            compatible = []
            for program in loads:
                program_end = program.virtual_address + program.memory_size
                if (
                    section.address >= program.virtual_address
                    and section_end <= program_end
                ):
                    compatible.append(program)
            if len(compatible) != 1:
                errors.append(
                    f"allocatable section {section.name} is not closed "
                    "inside one PT_LOAD"
                )
                continue
            program = compatible[0]
            expected_write = bool(section.flags & SECTION_WRITE)
            expected_execute = bool(section.flags & SECTION_EXECUTE)
            if (
                expected_write != bool(program.flags & PROGRAM_WRITE)
                or expected_execute != bool(program.flags & PROGRAM_EXECUTE)
            ):
                errors.append(
                    f"allocatable section {section.name} has incompatible "
                    "segment permissions"
                )
            if section.section_type != SECTION_NOBITS:
                file_end = program.virtual_address + program.file_size
                if section_end > file_end:
                    errors.append(
                        f"file-backed section {section.name} exceeds p_filesz"
                    )
                expected_offset = (
                    program.offset
                    + section.address
                    - program.virtual_address
                )
                if section.offset != expected_offset:
                    errors.append(
                        f"section {section.name} file offset is not congruent"
                    )
    required = {
        ".text": (SECTION_PROGBITS, SECTION_ALLOC | SECTION_EXECUTE),
        ".rodata": (SECTION_PROGBITS, SECTION_ALLOC),
        ".data": (SECTION_PROGBITS, SECTION_ALLOC | SECTION_WRITE),
        ".bss": (SECTION_NOBITS, SECTION_ALLOC | SECTION_WRITE),
    }
    for name, (section_type, flags) in required.items():
        section = _section_by_name(image, name)
        if section is None:
            errors.append(f"missing required section {name}")
        elif (
            section.section_type != section_type
            or section.flags
                & (SECTION_ALLOC | SECTION_WRITE | SECTION_EXECUTE)
                != flags
        ):
            errors.append(f"required section {name} has the wrong shape")
    for section in image.sections:
        if (
            section.flags & SECTION_ALLOC
            and section.name not in required
        ):
            errors.append(
                f"unexpected allocatable section {section.name}"
            )
    return errors


def _validate_symbols(image: ElfImage, loads):
    errors = []
    symbols, duplicates = _defined_symbols(image)
    for name in sorted(duplicates):
        errors.append(f"duplicate defined symbol {name}")
    for name in REQUIRED_SYMBOLS:
        if name not in symbols:
            errors.append(f"missing required symbol {name}")
    undefined = sorted(
        {
            symbol.name
            for symbol in image.symbols
            if symbol.name and symbol.section_index == SYMBOL_UNDEFINED
        }
    )
    for name in undefined:
        errors.append(f"undefined symbol {name}")
    if any(name not in symbols for name in REQUIRED_SYMBOLS):
        return errors, symbols
    if symbols["_start"].binding != SYMBOL_BIND_GLOBAL:
        errors.append("_start must be a global symbol")
    if (
        image.header.entry != symbols["_start"].value
        or symbols["_start"].value != USER_BASE
    ):
        errors.append("_start and e_entry must both equal the user base")
    boundaries = {
        name: symbols[name].value
        for name in REQUIRED_SYMBOLS
        if name.startswith("__micros_user_")
    }
    aligned = (
        "__micros_user_image_start",
        "__micros_user_text_start",
        "__micros_user_text_end",
        "__micros_user_rodata_start",
        "__micros_user_rodata_end",
        "__micros_user_data_start",
        "__micros_user_bss_end",
        "__micros_user_image_end",
    )
    for name in aligned:
        if boundaries[name] % PAGE_SIZE != 0:
            errors.append(f"symbol {name} must be page-aligned")
    if (
        boundaries["__micros_user_image_start"] != USER_BASE
        or boundaries["__micros_user_text_start"] != USER_BASE
    ):
        errors.append("user image and text must start at the user base")
    if not (
        boundaries["__micros_user_text_end"]
        == boundaries["__micros_user_rodata_start"]
        < boundaries["__micros_user_rodata_end"]
        == boundaries["__micros_user_data_start"]
        <= boundaries["__micros_user_data_end"]
        == boundaries["__micros_user_bss_start"]
        < boundaries["__micros_user_bss_end"]
        == boundaries["__micros_user_image_end"]
    ):
        errors.append("user linker boundaries are not ordered and closed")
    if boundaries["__micros_user_text_start"] >= boundaries[
        "__micros_user_text_end"
    ]:
        errors.append("user text range is empty")
    if boundaries["__micros_user_image_end"] > MINIMUM_STACK_BOTTOM:
        errors.append("user image overlaps the minimum external stack")
    if len(loads) == 3:
        expected_ranges = (
            (
                boundaries["__micros_user_text_start"],
                boundaries["__micros_user_text_end"],
            ),
            (
                boundaries["__micros_user_rodata_start"],
                boundaries["__micros_user_rodata_end"],
            ),
            (
                boundaries["__micros_user_data_start"],
                boundaries["__micros_user_image_end"],
            ),
        )
        for index, (program, expected) in enumerate(
            zip(loads, expected_ranges)
        ):
            if (
                program.virtual_address != expected[0]
                or program.virtual_address + program.memory_size
                    != expected[1]
            ):
                errors.append(
                    f"PT_LOAD {index} does not close its linker range"
                )
        writable_file_end = (
            loads[2].virtual_address + loads[2].file_size
        )
        if writable_file_end != boundaries["__micros_user_bss_start"]:
            errors.append(
                "writable PT_LOAD file range must end at BSS start"
            )
    required_sections = {
        ".text": (
            boundaries["__micros_user_text_start"],
            boundaries["__micros_user_text_end"],
        ),
        ".rodata": (
            boundaries["__micros_user_rodata_start"],
            boundaries["__micros_user_rodata_end"],
        ),
        ".data": (
            boundaries["__micros_user_data_start"],
            boundaries["__micros_user_data_end"],
        ),
        ".bss": (
            boundaries["__micros_user_bss_start"],
            boundaries["__micros_user_bss_end"],
        ),
    }
    for name, expected in required_sections.items():
        section = _section_by_name(image, name)
        if (
            section is not None
            and (
                section.address != expected[0]
                or section.address + section.size != expected[1]
            )
        ):
            errors.append(
                f"{name} does not match its exported linker range"
            )
    bss = _section_by_name(image, ".bss")
    if (
        bss is not None
        and (
            bss.address != boundaries["__micros_user_bss_start"]
            or bss.address + bss.size
                != boundaries["__micros_user_bss_end"]
        )
    ):
        errors.append(".bss does not match the exported BSS range")
    return errors, symbols


def _validate_instructions(image: ElfImage, symbols, loads):
    errors = []
    if any(name not in symbols for name in REQUIRED_SYMBOLS):
        return errors
    raw = symbols["micros_runtime_raw_syscall"]
    if raw.size != 8:
        errors.append("raw syscall stub must be exactly eight bytes")
    else:
        try:
            raw_bytes = _program_file_bytes(image, raw.value, raw.size)
        except ElfFormatError as error:
            errors.append(str(error))
        else:
            if raw_bytes != b"\x73\x00\x00\x00\x67\x80\x00\x00":
                errors.append("raw syscall stub must be exact ecall; ret")
    returned = symbols["micros_runtime_service_returned"]
    try:
        return_bytes = _program_file_bytes(image, returned.value, 8)
    except ElfFormatError as error:
        errors.append(str(error))
    else:
        if return_bytes != b"\x73\x00\x10\x00\x6f\x00\x00\x00":
            errors.append(
                "service-return path must be uncompressed ebreak and loop"
            )
    start = symbols["_start"].value
    try:
        startup = _program_file_bytes(image, start, 24)
    except ElfFormatError as error:
        errors.append(str(error))
    else:
        words = struct.unpack("<6I", startup)
        if words[0] != 0x00000193 or words[1] != 0x00000213:
            errors.append("startup must initialize gp and tp to zero")
        if returned.value != start + 16:
            errors.append("service-return label must immediately follow call")
        auipc = words[2]
        jalr = words[3]
        if (
            auipc & 0x7F != 0x17
            or (auipc >> 7) & 0x1F != 1
            or jalr & 0x7F != 0x67
            or (jalr >> 7) & 0x1F != 1
            or (jalr >> 15) & 0x1F != 1
        ):
            errors.append("startup must call micros_service_main directly")
        else:
            upper = _sign_extend(auipc & 0xFFFFF000, 32)
            lower = _sign_extend(jalr >> 20, 12)
            target = (start + 8 + upper + lower) & ~1
            if target != symbols["micros_service_main"].value:
                errors.append("startup call target is not micros_service_main")
    executable_loads = [
        program
        for program in loads
        if program.flags == PROGRAM_READ | PROGRAM_EXECUTE
    ]
    if len(executable_loads) == 1:
        text = executable_loads[0]
        try:
            text_bytes = _bounded_slice(
                image.data,
                text.offset,
                text.file_size,
                "executable PT_LOAD bytes",
            )
            writes = [
                item
                for item in _instruction_writes(
                    text.virtual_address,
                    text_bytes,
                )
                if item[1] in (3, 4)
            ]
        except ElfFormatError as error:
            errors.append(str(error))
        else:
            if writes != [(start, 3), (start + 4, 4)]:
                errors.append(
                    "gp or tp is written outside startup initialization"
                )
    return errors


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


def resident_page_count(image: ElfImage, stack_page_count: int):
    if stack_page_count < 0:
        raise ValueError("stack page count must not be negative")
    load_pages = sum(
        (program.memory_size + PAGE_SIZE - 1) // PAGE_SIZE
        for program in image.programs
        if program.program_type == PROGRAM_LOAD
    )
    return load_pages + stack_page_count


def validate_resident_page_limit(
    image: ElfImage,
    stack_page_count: int,
    resident_page_limit: int,
):
    if resident_page_limit <= 0:
        raise ValueError("resident page limit must be positive")
    count = resident_page_count(image, stack_page_count)
    if count > resident_page_limit:
        return [
            f"resident page count {count} exceeds limit "
            f"{resident_page_limit}"
        ]
    return []


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

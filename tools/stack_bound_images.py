"""Exact allocatable-image and runtime-symbol passivity checks."""

from tools.stack_bound_types import StackEvidenceError
from tools.user_elf_format import (
    ELF_MACHINE_RISCV, ELF_TYPE_EXECUTABLE, PROGRAM_LOAD, SECTION_ALLOC,
    SYMBOL_BIND_GLOBAL,
)
from tools.user_elf_parse import parse_elf


def load_records(image):
    records = []
    for program in image.programs:
        if program.program_type != PROGRAM_LOAD:
            continue
        records.append((
            program.flags, program.virtual_address, program.physical_address,
            program.file_size, program.memory_size, program.alignment,
            image.data[program.offset:program.offset + program.file_size],
        ))
    if not records:
        raise StackEvidenceError("stack image has no allocatable load segments")
    return tuple(records)


def runtime_symbols(image):
    records = []
    for symbol in image.symbols:
        if not symbol.name or symbol.name.startswith(("$", ".L")):
            continue
        if 0 < symbol.section_index < len(image.sections):
            section = image.sections[symbol.section_index]
            if not section.flags & SECTION_ALLOC:
                continue
            owner = section.name
        elif symbol.binding == SYMBOL_BIND_GLOBAL and symbol.section_index == 0xfff1:
            owner = "absolute"
        else:
            continue
        records.append((
            owner, symbol.name, symbol.binding, symbol.symbol_type,
            symbol.value, symbol.size,
        ))
    return tuple(sorted(records))


def require_passive_images(ordinary, metadata):
    try:
        first, second = parse_elf(ordinary), parse_elf(metadata)
    except ValueError as error:
        raise StackEvidenceError(f"invalid stack evidence image: {error}") from error
    for image in (first, second):
        if (
            image.header.machine != ELF_MACHINE_RISCV
            or image.header.elf_type != ELF_TYPE_EXECUTABLE
        ):
            raise StackEvidenceError("stack evidence image is not an RV64 executable")
    if (
        first.header.entry != second.header.entry
        or first.header.flags != second.header.flags
    ):
        raise StackEvidenceError("metadata changed executable entry or ISA flags")
    if load_records(first) != load_records(second):
        raise StackEvidenceError("metadata changed allocatable image bytes or geometry")
    if runtime_symbols(first) != runtime_symbols(second):
        raise StackEvidenceError("metadata changed retained runtime symbols")

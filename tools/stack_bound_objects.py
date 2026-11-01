"""RV64 relocatable-object ownership and exact call-instruction decoding."""

from dataclasses import dataclass
from pathlib import Path
import struct

from tools.stack_bound_types import EdgeKind, StackEvidenceError, SymbolCall
from tools.user_elf_format import (
    ELF_MACHINE_RISCV, SECTION_ALLOC, SECTION_EXECUTE, SECTION_NOBITS,
    SECTION_RELA, SECTION_SYMTAB, SYMBOL_UNDEFINED, _bounded_slice,
)
from tools.user_elf_parse import (
    _parse_header, _parse_raw_sections, _parse_sections, _parse_symbols,
)


RELOCATION = struct.Struct("<QQq")


@dataclass(frozen=True)
class Object:
    data: bytes
    sections: tuple
    symbols: tuple


def load_object(path):
    data = Path(path).read_bytes()
    try:
        header = _parse_header(data)
        if header.elf_type != 1 or header.machine != ELF_MACHINE_RISCV:
            raise StackEvidenceError("stack input is not an RV64 relocatable object")
        sections = _parse_sections(data, header, _parse_raw_sections(data, header))
        if sum(section.section_type == SECTION_SYMTAB for section in sections) != 1:
            raise StackEvidenceError("object must have one exact symbol table")
        symbols = _parse_symbols(data, sections)
    except ValueError as error:
        raise StackEvidenceError(f"invalid stack object: {error}") from error
    return Object(data, sections, symbols)


def section_bytes(obj, section):
    return _bounded_slice(obj.data, section.offset, section.size, section.name)


def function_symbols(obj):
    result = {}
    for symbol in obj.symbols:
        if symbol.symbol_type != 2 or symbol.section_index == SYMBOL_UNDEFINED:
            continue
        if not 0 < symbol.section_index < len(obj.sections):
            raise StackEvidenceError("function has no owned executable section")
        section = obj.sections[symbol.section_index]
        if (
            not section.flags & SECTION_EXECUTE or symbol.size == 0
            or symbol.value > section.size
            or symbol.size > section.size - symbol.value
            or symbol.name in result
        ):
            raise StackEvidenceError("invalid or duplicate object function")
        result[symbol.name] = symbol
    return result


def relocations(obj, section_index):
    result = []
    for section in obj.sections:
        if section.section_type != SECTION_RELA or section.info != section_index:
            continue
        if (
            not 0 < section.link < len(obj.sections)
            or obj.sections[section.link].section_type != SECTION_SYMTAB
            or section.entry_size != RELOCATION.size
            or section.size % RELOCATION.size
        ):
            raise StackEvidenceError("invalid stack object relocation table")
        for offset in range(0, section.size, RELOCATION.size):
            address, info, addend = RELOCATION.unpack_from(
                obj.data, section.offset + offset,
            )
            index = info >> 32
            if index >= len(obj.symbols):
                raise StackEvidenceError("relocation has an unknown symbol")
            result.append((address, info & 0xffffffff, obj.symbols[index], addend))
    return tuple(result)


def allocatable_object_bytes(path):
    obj = load_object(path)
    return tuple(
        (
            section.name, section.flags, section.address, section.size,
            section.alignment,
            b"" if section.section_type == SECTION_NOBITS else section_bytes(obj, section),
        )
        for section in obj.sections if section.flags & SECTION_ALLOC
    )


def instructions(data, base):
    offset = 0
    while offset < len(data):
        if len(data) - offset < 2:
            raise StackEvidenceError("truncated RV64 instruction")
        half = int.from_bytes(data[offset:offset + 2], "little")
        width = 4 if half & 3 == 3 else 2
        if width == 4 and (half & 31 == 31 or len(data) - offset < 4):
            raise StackEvidenceError("unsupported or truncated RV64 instruction")
        yield base + offset, width, int.from_bytes(data[offset:offset + width], "little")
        offset += width


def call_instructions(obj, symbol, key, switches=None):
    section = obj.sections[symbol.section_index]
    data = section_bytes(obj, section)[symbol.value:symbol.value + symbol.size]
    calls = {}
    for address, kind, target, addend in relocations(obj, section.index):
        if symbol.value <= address < symbol.value + symbol.size and kind in (18, 19):
            if addend != 0 or address in calls:
                raise StackEvidenceError("ambiguous relocated call target")
            calls[address] = target.name
    decoded = tuple(instructions(data, symbol.value))
    pairs = {}
    for index, (address, width, instruction) in enumerate(decoded):
        if address not in calls:
            continue
        if (
            width != 4 or instruction & 0x7f != 0x17
            or (instruction >> 7) & 31 == 0 or instruction >> 12 != 0
            or index + 1 == len(decoded)
        ):
            raise StackEvidenceError("call relocation lacks its AUIPC/JALR pair")
        following, next_width, next_instruction = decoded[index + 1]
        if (
            following != address + 4 or next_width != 4
            or next_instruction & 0x7f != 0x67
            or (instruction >> 7) & 31 != (next_instruction >> 15) & 31
            or (next_instruction >> 12) & 7 != 0
            or next_instruction >> 20 != 0
        ):
            raise StackEvidenceError("call relocation lacks its exact register pair")
        pairs[following] = (address, calls[address])
    result = []
    for address, width, instruction in decoded:
        if switches is not None and address in switches:
            continue
        if address in pairs:
            site, target = pairs[address]
            register = (instruction >> 7) & 31
            if register not in (0, 1):
                raise StackEvidenceError("unsupported call link register")
            result.append(SymbolCall(
                key, site - symbol.value, target,
                EdgeKind.TAIL if register == 0 else EdgeKind.CALL,
            ))
        elif width == 4 and instruction & 0x7f == 0x67:
            destination = (instruction >> 7) & 31
            source = (instruction >> 15) & 31
            immediate = instruction >> 20
            if destination == 0 and source == 1 and immediate == 0:
                continue
            result.append(SymbolCall(
                key, address - symbol.value, None,
                EdgeKind.TAIL if destination == 0 else EdgeKind.CALL,
            ))
        elif width == 4 and instruction & 0x7f == 0x6f:
            raise StackEvidenceError("unbound direct JAL in C object")
        elif (
            width == 2 and instruction & 3 == 2
            and instruction >> 13 == 4 and (instruction >> 2) & 31 == 0
        ):
            source = (instruction >> 7) & 31
            high = (instruction >> 12) & 1
            if source == 0 or high == 0 and source == 1:
                continue
            result.append(SymbolCall(
                key, address - symbol.value, None,
                EdgeKind.CALL if high else EdgeKind.TAIL,
            ))
    return tuple(result)

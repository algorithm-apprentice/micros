"""Bounded RISC-V destination decoding owner assigned by ADR-0063."""

from __future__ import annotations



from tools.user_elf_format import (
    ElfFormatError,
)


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

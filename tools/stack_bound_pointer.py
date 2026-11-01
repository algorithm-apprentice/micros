"""Intraprocedural argument provenance for actual RV64 callback registers."""

from collections import deque

from tools.stack_bound_machine import (
    CALLER_SAVED, branch_offset, compressed_result, is_call, is_register_jump,
    register_result, signed,
)
from tools.stack_bound_objects import instructions, relocations, section_bytes
from tools.stack_bound_switch import switch_jumps
from tools.stack_bound_types import StackEvidenceError
from tools.user_elf_instructions import _instruction_destination


def indirect_register(width, word):
    if width == 4 and word & 0x707f == 0x67:
        return (word >> 15) & 31
    if (
        width == 2 and word & 3 == 2 and word >> 13 == 4
        and (word >> 2) & 31 == 0 and (word >> 7) & 31
    ):
        return (word >> 7) & 31
    return None


def memory_action(width, word):
    if width == 4:
        opcode, operation = word & 0x7f, (word >> 12) & 7
        if opcode == 3 and operation in (0, 1, 2, 3, 4, 5, 6):
            return False, (1, 2, 4, 8, 1, 2, 4)[operation], (word >> 7) & 31, (
                word >> 15
            ) & 31, signed(word >> 20, 12)
        if opcode == 0x23 and operation in (0, 1, 2, 3):
            return True, 1 << operation, (word >> 20) & 31, (word >> 15) & 31, signed(
                (word >> 25) << 5 | (word >> 7) & 31, 12,
            )
        return None
    quadrant, operation = word & 3, word >> 13
    if quadrant == 0 and operation in (2, 3, 6, 7):
        size = 8 if operation & 1 else 4
        offset = (
            ((word >> 10) & 7) << 3 | ((word >> 5) & 3) << 6
            if size == 8 else
            ((word >> 10) & 7) << 3 | ((word >> 5) & 1) << 6 | ((word >> 6) & 1) << 2
        )
        return operation >= 6, size, 8 + ((word >> 2) & 7), 8 + ((word >> 7) & 7), offset
    if quadrant == 2 and operation in (2, 3):
        size = 8 if operation == 3 else 4
        offset = (
            ((word >> 12) & 1) << 5 | ((word >> 5) & 3) << 3 | ((word >> 2) & 7) << 6
            if size == 8 else
            ((word >> 12) & 1) << 5 | ((word >> 4) & 7) << 2 | ((word >> 2) & 3) << 6
        )
        return False, size, (word >> 7) & 31, 2, offset
    if quadrant == 2 and operation in (6, 7):
        size = 8 if operation == 7 else 4
        offset = (
            ((word >> 10) & 7) << 3 | ((word >> 7) & 7) << 6
            if size == 8 else
            ((word >> 9) & 15) << 2 | ((word >> 7) & 3) << 6
        )
        return True, size, (word >> 2) & 31, 2, offset
    return None


def arithmetic_result(width, word, registers):
    if width == 4 and word & 0x707f == 0x13 and word >> 20 == 0:
        return registers[(word >> 15) & 31]
    if width == 2 and word & 0xf003 == 0x8002 and (word >> 2) & 31:
        return registers[(word >> 2) & 31]
    converted = tuple(
        (value[0] == "stack", value[1])
        if value is not None and value[0] in ("stack", "scalar") else None
        for value in registers
    )
    result = (
        compressed_result(word, converted) if width == 2
        else register_result(word, converted)
    )
    return None if result is None else ("stack" if result[0] else "scalar", result[1])


def pointer_transfer(state, width, word):
    registers, stored = state
    changed, memory = list(registers), dict(stored)
    action = memory_action(width, word)
    destination = _instruction_destination(word, width)
    if action is None:
        if destination not in (None, 0):
            changed[destination] = arithmetic_result(width, word, registers)
    else:
        write, size, register, basis, offset = action
        base = registers[basis]
        if write:
            if base is not None and base[0] == "stack":
                address = base[1] + offset
                memory = {
                    location: value for location, value in memory.items()
                    if location + 8 <= address or address + size <= location
                }
                if size == 8 and address % 8 == 0 and registers[register] is not None:
                    memory[address] = registers[register]
        elif register:
            value = None
            if size == 8 and base is not None:
                if base[0] == "stack":
                    value = memory.get(base[1] + offset)
                elif base[0] == "argument" and offset >= 0 and offset % 8 == 0:
                    value = ("argument", base[1], (*base[2], offset))
            changed[register] = value
    if is_call(width, word):
        for register in CALLER_SAVED:
            changed[register] = None
    changed[0] = ("scalar", 0)
    return tuple(changed), tuple(sorted(memory.items()))


def merge_pointer_states(previous, current):
    if previous[0][2] != current[0][2]:
        raise StackEvidenceError("callback provenance has inconsistent stack paths")
    registers = tuple(
        left if left == right else None for left, right in zip(previous[0], current[0])
    )
    old, new = dict(previous[1]), dict(current[1])
    stored = tuple(sorted(
        (location, value) for location, value in old.items() if new.get(location) == value
    ))
    return registers, stored


def callback_argument_paths(obj, symbol):
    section = obj.sections[symbol.section_index]
    data = section_bytes(obj, section)[symbol.value:symbol.value + symbol.size]
    decoded = {
        address: (width, word) for address, width, word in instructions(data, symbol.value)
    }
    targets = {}
    for address, kind, target, addend in relocations(obj, section.index):
        if kind in (16, 17, 44, 45):
            if target.section_index != section.index or address in targets:
                raise StackEvidenceError("callback provenance has an unbound branch")
            targets[address] = target.value + addend
    switches = switch_jumps(obj, symbol)
    registers = [None] * 32
    registers[0], registers[2] = ("scalar", 0), ("stack", 0)
    for argument in range(8):
        registers[10 + argument] = ("argument", argument, ())
    states = {symbol.value: (tuple(registers), ())}
    ready = deque((symbol.value,))
    while ready:
        address = ready.popleft()
        width, word = decoded[address]
        current = pointer_transfer(states[address], width, word)
        if address in switches:
            following = switches[address]
        elif is_register_jump(width, word):
            continue
        else:
            next_address = address + width
            branch = branch_offset(width, word)
            if is_call(width, word):
                following = (next_address,) if next_address in decoded else ()
            elif branch is not None:
                offset, conditional = branch
                target = targets.get(address, address + offset)
                following = (target, next_address) if conditional else (target,)
            else:
                following = (next_address,) if next_address in decoded else ()
        for target in following:
            if target not in decoded:
                raise StackEvidenceError("callback provenance leaves its exact function")
            old = states.get(target)
            merged = current if old is None else merge_pointer_states(old, current)
            if merged != old:
                states[target] = merged
                ready.append(target)
    return {
        address - symbol.value: states[address][0][register]
        for address, (width, word) in decoded.items()
        if address in states and (register := indirect_register(width, word)) is not None
    }

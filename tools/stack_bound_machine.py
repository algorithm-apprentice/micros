"""Intraprocedural RV64 stack movement checked against compiler frames."""

from collections import deque

from tools.stack_bound_objects import instructions, relocations, section_bytes
from tools.stack_bound_types import StackEvidenceError
from tools.user_elf_instructions import _instruction_destination


CALLER_SAVED = (1, 5, 6, 7, *range(10, 18), *range(28, 32))


def signed(value, bits):
    value &= (1 << bits) - 1
    return value - (1 << bits) if value & (1 << (bits - 1)) else value


def add(left, right, *, subtract=False):
    if left is None or right is None:
        return None
    if left[0] and right[0]:
        return (False, left[1] - right[1]) if subtract else None
    if not left[0] and right[0]:
        return None if subtract else (True, left[1] + right[1])
    value = left[1] - right[1] if subtract else left[1] + right[1]
    return (left[0], value if left[0] else signed(value, 64))


def compressed_result(instruction, registers):
    quadrant, operation = instruction & 3, instruction >> 13
    destination = (instruction >> 7) & 31
    immediate = signed(
        ((instruction >> 12) & 1) << 5 | ((instruction >> 2) & 31), 6,
    )
    if quadrant == 1 and operation == 0:
        return add(registers[destination], (False, immediate))
    if quadrant == 1 and operation == 1:
        value = add(registers[destination], (False, immediate))
        return (False, signed(value[1], 32)) if value is not None and not value[0] else None
    if quadrant == 1 and operation == 2:
        return (False, immediate)
    if quadrant == 1 and operation == 3:
        if destination != 2:
            return (False, immediate << 12)
        offset = (
            ((instruction >> 12) & 1) << 9
            | ((instruction >> 3) & 3) << 7
            | ((instruction >> 5) & 1) << 6
            | ((instruction >> 2) & 1) << 5
            | ((instruction >> 6) & 1) << 4
        )
        return add(registers[2], (False, signed(offset, 10)))
    if quadrant == 0 and operation == 0:
        offset = (
            ((instruction >> 7) & 15) << 6
            | ((instruction >> 11) & 3) << 4
            | ((instruction >> 5) & 1) << 3
            | ((instruction >> 6) & 1) << 2
        )
        return add(registers[2], (False, offset))
    if quadrant == 2 and operation == 4 and (instruction >> 2) & 31:
        source = (instruction >> 2) & 31
        if not (instruction >> 12) & 1:
            return registers[source]
        return add(registers[destination], registers[source])
    return None


def register_result(instruction, registers):
    opcode = instruction & 0x7f
    source = (instruction >> 15) & 31
    operation = (instruction >> 12) & 7
    immediate = signed(instruction >> 20, 12)
    if opcode == 0x37:
        return (False, signed(instruction & 0xfffff000, 32))
    if opcode in (0x13, 0x1b) and operation == 0:
        value = add(registers[source], (False, immediate))
        if opcode == 0x1b:
            return (False, signed(value[1], 32)) if value is not None and not value[0] else None
        return value
    if opcode == 0x13 and operation == 1:
        value = registers[source]
        if value is not None and not value[0]:
            return (False, signed(value[1] << ((instruction >> 20) & 63), 64))
    if opcode == 0x33 and operation == 0 and instruction >> 25 in (0, 0x20):
        return add(
            registers[source], registers[(instruction >> 20) & 31],
            subtract=instruction >> 25 == 0x20,
        )
    return None


def is_call(width, instruction):
    if width == 4:
        return instruction & 0x7f in (0x67, 0x6f) and (instruction >> 7) & 31 == 1
    return (
        instruction & 3 == 2 and instruction >> 13 == 4
        and (instruction >> 12) & 1 == 1
        and (instruction >> 7) & 31 != 0 and (instruction >> 2) & 31 == 0
    )


def transfer(registers, width, instruction, frame):
    changed = list(registers)
    destination = _instruction_destination(instruction, width)
    if destination is not None and destination != 0:
        changed[destination] = (
            compressed_result(instruction, registers) if width == 2
            else register_result(instruction, registers)
        )
    if is_call(width, instruction):
        for register in CALLER_SAVED:
            changed[register] = None
    stack = changed[2]
    if stack is None or not stack[0] or stack[1] > 0 or stack[1] < -frame or stack[1] % 16:
        raise StackEvidenceError("machine stack movement exceeds or obscures its compiler frame")
    changed[0] = (False, 0)
    return tuple(changed)


def branch_offset(width, instruction):
    if width == 4 and instruction & 0x7f == 0x63:
        value = (
            (instruction >> 31) << 12 | ((instruction >> 7) & 1) << 11
            | ((instruction >> 25) & 63) << 5 | ((instruction >> 8) & 15) << 1
        )
        return signed(value, 13), True
    if width == 4 and instruction & 0x7f == 0x6f:
        value = (
            (instruction >> 31) << 20 | ((instruction >> 12) & 255) << 12
            | ((instruction >> 20) & 1) << 11 | ((instruction >> 21) & 1023) << 1
        )
        return signed(value, 21), False
    quadrant, operation = instruction & 3, instruction >> 13
    if width == 2 and quadrant == 1 and operation == 5:
        value = (
            ((instruction >> 12) & 1) << 11 | ((instruction >> 11) & 1) << 4
            | ((instruction >> 9) & 3) << 8 | ((instruction >> 8) & 1) << 10
            | ((instruction >> 7) & 1) << 6 | ((instruction >> 6) & 1) << 7
            | ((instruction >> 3) & 7) << 1 | ((instruction >> 2) & 1) << 5
        )
        return signed(value, 12), False
    if width == 2 and quadrant == 1 and operation in (6, 7):
        value = (
            ((instruction >> 12) & 1) << 8 | ((instruction >> 10) & 3) << 3
            | ((instruction >> 5) & 3) << 6 | ((instruction >> 3) & 3) << 1
            | ((instruction >> 2) & 1) << 5
        )
        return signed(value, 9), True
    return None


def is_register_jump(width, instruction):
    if width == 4:
        return instruction & 0x7f == 0x67 and (instruction >> 7) & 31 == 0
    return (
        instruction & 3 == 2 and instruction >> 13 == 4
        and (instruction >> 12) & 1 == 0
        and (instruction >> 7) & 31 != 0 and (instruction >> 2) & 31 == 0
    )

def is_return(width, instruction):
    if width == 4:
        return (
            instruction & 0x7f == 0x67 and (instruction >> 7) & 31 == 0
            and (instruction >> 15) & 31 == 1 and instruction >> 20 == 0
        )
    return (
        instruction & 3 == 2 and instruction >> 13 == 4
        and (instruction >> 12) & 1 == 0
        and (instruction >> 7) & 31 == 1 and (instruction >> 2) & 31 == 0
    )


def require_c_frame_movement(obj, symbol, frame, switches=None):
    section = obj.sections[symbol.section_index]
    data = section_bytes(obj, section)[symbol.value:symbol.value + symbol.size]
    decoded = tuple(instructions(data, symbol.value))
    by_address = {address: (width, instruction) for address, width, instruction in decoded}
    targets = {}
    for address, kind, target, addend in relocations(obj, section.index):
        if kind in (16, 17, 44, 45):
            if target.section_index != section.index or address in targets:
                raise StackEvidenceError("unbound machine stack-control edge")
            targets[address] = target.value + addend
    initial = [None] * 32
    initial[0], initial[2] = (False, 0), (True, 0)
    states = {symbol.value: tuple(initial)}
    ready = deque((symbol.value,))
    while ready:
        address = ready.popleft()
        width, instruction = by_address[address]
        state = transfer(states[address], width, instruction, frame)
        if switches is not None and address in switches:
            following = switches[address]
        elif is_register_jump(width, instruction):
            if is_return(width, instruction) and state[2] != (True, 0):
                raise StackEvidenceError("machine return leaves an unreleased stack frame")
            continue
        else:
            next_address = address + width
            branch = branch_offset(width, instruction)
            if is_call(width, instruction):
                following = (next_address,) if next_address in by_address else ()
            elif branch is not None:
                offset, conditional = branch
                destination = targets.get(address, address + offset)
                following = (destination, next_address) if conditional else (destination,)
            else:
                following = (next_address,) if next_address in by_address else ()
        for destination in following:
            if destination not in by_address:
                raise StackEvidenceError("stack-control edge leaves its C function")
            previous = states.get(destination)
            if previous is not None:
                if previous[2] != state[2]:
                    raise StackEvidenceError("machine stack movement differs across control paths")
                merged = tuple(
                    left if left == right else None for left, right in zip(previous, state)
                )
                if merged == previous:
                    continue
                state_for_destination = merged
            else:
                state_for_destination = state
            states[destination] = state_for_destination
            ready.append(destination)

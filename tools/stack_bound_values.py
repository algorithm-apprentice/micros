"""Finite RV64 operand provenance for the closed fatal-context proof."""

from collections import deque

from tools.stack_bound_flow import function_flow
from tools.stack_bound_machine import CALLER_SAVED, is_call, signed
from tools.stack_bound_objects import relocations, section_bytes
from tools.stack_bound_pointer import arithmetic_result, memory_action
from tools.stack_bound_types import StackEvidenceError, require_uint64
from tools.user_elf_instructions import _instruction_destination


def scalar_operation(width, word, registers):
    if width == 4 and word & 0x7f == 0x13:
        source = registers[(word >> 15) & 31]
        operation = (word >> 12) & 7
        immediate = signed(word >> 20, 12)
        if operation == 7:
            if source is not None and source[0] == "scalar":
                return "scalar", source[1] & immediate
            if source is not None and source[0] == "result" and source[2] == 1 and immediate == 1:
                return source
        if operation == 5 and source is not None and source[0] == "scalar":
            shift = (word >> 20) & 63
            value = source[1] >> shift if word >> 30 & 1 else (source[1] & ((1 << 64) - 1)) >> shift
            return "scalar", signed(value, 64)
    if width == 2 and word & 3 == 1 and word >> 13 == 4 and (word >> 10) & 3 == 2:
        source = registers[8 + ((word >> 7) & 7)]
        immediate = signed(((word >> 12) & 1) << 5 | ((word >> 2) & 31), 6)
        if source is not None and source[0] == "scalar":
            return "scalar", source[1] & immediate
        if source is not None and source[0] == "result" and source[2] == 1 and immediate == 1:
            return source
    return arithmetic_result(width, word, registers)


def pcrel_values(obj, symbol, flow):
    section = obj.sections[symbol.section_index]
    high, low = {}, {}
    for address, kind, target, addend in relocations(obj, section.index):
        relative = address - symbol.value
        if relative not in flow.instructions or kind not in (23, 24):
            continue
        width, word = flow.instructions[relative]
        if width != 4 or relative in high or relative in low:
            raise StackEvidenceError("ambiguous PC-relative fatal operand")
        if kind == 23:
            if word & 0x7f != 0x17 or word >> 12 or (word >> 7) & 31 == 0:
                raise StackEvidenceError("PC-relative object lacks its exact AUIPC")
            high[relative] = ("high", relative, ("symbol", target.name, target.section_index, target.value + addend))
        else:
            if target.section_index != section.index or word >> 20:
                raise StackEvidenceError("PC-relative operand has an unowned lower relocation")
            low[relative] = target.value + addend - symbol.value
    return high, low


def loaded_value(obj, stored, base, offset, size, unsigned):
    if base is None:
        return None
    if base[0] == "stack":
        item = stored.get(base[1] + offset)
        if item is None or item[0] < size:
            return None
        value = item[1]
        if value[0] == "scalar":
            number = value[1] & ((1 << (8 * size)) - 1)
            return "scalar", number if unsigned else signed(number, 8 * size)
        if value[0] == "result" and value[2] <= 8 * size:
            return value
        return value if size == 8 else None
    if base[0] == "symbol":
        section = obj.sections[base[2]] if 0 < base[2] < len(obj.sections) else None
        address = base[3] + offset
        if section is not None and section.flags & 2 and not section.flags & 5 and section.section_type == 1:
            if address < 0 or address + size > section.size:
                raise StackEvidenceError("read-only fatal operand leaves its object section")
            data = section_bytes(obj, section)[address:address + size]
            return "scalar", int.from_bytes(data, "little", signed=not unsigned)
    return "field", base, offset, size


def value_transfer(obj, state, address, width, word, high, low, produced):
    registers, stored = state
    changed, memory = list(registers), dict(stored)
    destination = _instruction_destination(word, width)
    action = memory_action(width, word)
    lower = high.get(low[address]) if address in low else None
    if address in low:
        basis = (word >> 15) & 31
        if lower is None or registers[basis] != lower:
            raise StackEvidenceError("PC-relative operand borrowed another AUIPC register")
        if word & 0x707f == 0x13:
            changed[destination] = lower[2]
            action = None
        elif action is None or action[0]:
            raise StackEvidenceError("unsupported PC-relative fatal operand operation")
    if address in high:
        changed[destination] = high[address]
    elif action is not None:
        write, size, register, basis, offset = action
        base = lower[2] if lower is not None else registers[basis]
        if write:
            if base is not None and base[0] == "stack":
                location = base[1] + offset
                memory = {
                    point: value for point, value in memory.items()
                    if point + value[0] <= location or location + size <= point
                }
                value = registers[register]
                if value is not None and (size == 8 or value[0] in ("scalar", "result")):
                    memory[location] = size, value
        elif register:
            unsigned = width == 4 and (word >> 12) & 7 in (4, 5, 6)
            changed[register] = loaded_value(obj, memory, base, offset, size, unsigned)
    elif address not in low and destination not in (None, 0):
        changed[destination] = scalar_operation(width, word, registers)
    if is_call(width, word):
        for register in CALLER_SAVED:
            changed[register] = None
        if address in produced:
            changed[10] = produced[address]
    if width == 4 and word == 0x73:
        changed[10] = changed[11] = None
    changed[0] = ("scalar", 0)
    return tuple(changed), tuple(sorted(memory.items()))


def merge_values(previous, current):
    if previous[0][2] != current[0][2]:
        raise StackEvidenceError("fatal operands have inconsistent stack control paths")
    registers = tuple(left if left == right else None for left, right in zip(previous[0], current[0]))
    old, new = dict(previous[1]), dict(current[1])
    memory = tuple(sorted((address, value) for address, value in old.items() if new.get(address) == value))
    return registers, memory


def register_inputs(obj, symbol, *, arguments=None, result_call=None, result_bits=32):
    flow = function_flow(obj, symbol)
    high, low = pcrel_values(obj, symbol, flow)
    registers = [None] * 32
    registers[0], registers[2] = ("scalar", 0), ("stack", 0)
    registers[1] = ("entry-return-address",)
    if arguments is not None and (type(arguments) is not tuple or len(arguments) > 8):
        raise StackEvidenceError("invalid finite fatal argument inputs")
    for argument in range(8):
        if arguments is None:
            registers[10 + argument] = ("argument", argument, ())
        elif argument < len(arguments):
            require_uint64(arguments[argument], "fatal argument")
            registers[10 + argument] = ("scalar", arguments[argument])
    produced = {}
    if result_call is not None:
        if result_bits not in (1, 32) or type(result_bits) is not int:
            raise StackEvidenceError("unsupported finite result width")
        continuation = flow.continuations.get(result_call)
        if continuation is None:
            raise StackEvidenceError("result has no owned call continuation")
        last = max(address for address in flow.instructions if result_call <= address < continuation)
        produced[last] = ("result", result_call, result_bits)
    states = {0: (tuple(registers), ())}
    ready = deque((0,))
    while ready:
        address = ready.popleft()
        width, word = flow.instructions[address]
        current = value_transfer(obj, states[address], address, width, word, high, low, produced)
        for successor in flow.successors[address]:
            old = states.get(successor)
            merged = current if old is None else merge_values(old, current)
            if merged != old:
                states[successor] = merged
                ready.append(successor)
    return {address: state[0] for address, state in states.items()}

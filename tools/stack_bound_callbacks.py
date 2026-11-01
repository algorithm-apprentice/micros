"""Actual callback load/register ownership for finite source declarations."""

from tools.stack_bound_machine import branch_offset, is_call
from tools.stack_bound_objects import instructions, relocations, section_bytes
from tools.stack_bound_switch import switch_jumps
from tools.stack_bound_types import StackEvidenceError
from tools.user_elf_instructions import _instruction_destination


def require_callback_argument(obj, symbol, offset, argument, members):
    from tools.stack_bound_pointer import callback_argument_paths

    if type(argument) is not int or not 0 <= argument < 8 or type(members) is not tuple:
        raise StackEvidenceError("invalid callback argument declaration")
    if any(type(member) is not int or member < 0 or member % 8 for member in members):
        raise StackEvidenceError("invalid callback member declaration")
    actual = callback_argument_paths(obj, symbol).get(offset)
    if actual is None or actual[0] != "argument" or actual[1] != argument:
        raise StackEvidenceError("callback pointer does not come from its declared argument")
    if actual[2] != members:
        raise StackEvidenceError("callback pointer does not load its declared member path")


def indirect_member_offset(obj, symbol, offset):
    section = obj.sections[symbol.section_index]
    data = section_bytes(obj, section)[symbol.value:symbol.value + symbol.size]
    decoded = tuple(instructions(data, symbol.value))
    address = symbol.value + offset
    matching = [index for index, entry in enumerate(decoded) if entry[0] == address]
    if len(matching) != 1:
        raise StackEvidenceError("callback is not at an owned instruction boundary")
    position = matching[0]
    _, width, word = decoded[position]
    if width == 4 and word & 0x707f == 0x67:
        register = (word >> 15) & 31
    elif (
        width == 2 and word & 3 == 2 and word >> 13 == 4
        and (word >> 2) & 31 == 0
    ):
        register = (word >> 7) & 31
    else:
        raise StackEvidenceError("callback is not an indirect register transfer")
    writes = [
        index for index in range(position)
        if _instruction_destination(decoded[index][2], decoded[index][1]) == register
    ]
    if not writes:
        raise StackEvidenceError("callback register has no declared pointer load")
    loaded = writes[-1]
    load_address, load_width, load_word = decoded[loaded]
    if load_width == 4 and load_word & 0x707f == 0x3003:
        base = (load_word >> 15) & 31
        displacement = load_word >> 20
        if displacement & 0x800:
            displacement -= 0x1000
    elif load_width == 2 and load_word & 3 == 0 and load_word >> 13 == 3:
        base = 8 + ((load_word >> 7) & 7)
        displacement = ((load_word >> 10) & 7) << 3 | ((load_word >> 5) & 3) << 6
    else:
        raise StackEvidenceError("callback register was not loaded from its exact member")
    if base in (0, 2) or displacement < 0:
        raise StackEvidenceError("callback pointer is not a declared structure member")
    middle = {entry[0] for entry in decoded[loaded + 1:position + 1]}
    for _, entry_width, entry_word in decoded[loaded + 1:position]:
        if branch_offset(entry_width, entry_word) is not None or is_call(entry_width, entry_word):
            raise StackEvidenceError("callback load is not in the uninterrupted call block")
    for _, kind, target, addend in relocations(obj, section.index):
        if kind in (16, 17, 44, 45) and target.section_index == section.index:
            if target.value + addend in middle:
                raise StackEvidenceError("callback member load was bypassed")
    for targets in switch_jumps(obj, symbol).values():
        if any(target in middle for target in targets):
            raise StackEvidenceError("callback member load was bypassed by a switch")
    return displacement

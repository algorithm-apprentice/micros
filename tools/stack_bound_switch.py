"""Closed Clang RV64 switch-table transfers, not function-pointer calls."""

import re

from tools.stack_bound_objects import instructions, relocations, section_bytes
from tools.stack_bound_types import StackEvidenceError


def load_sp_slot(width, word):
    if width == 4 and word & 0x7f == 3 and (word >> 12) & 7 == 3 and (word >> 15) & 31 == 2:
        return (word >> 7) & 31, word >> 20
    if width == 2 and word & 3 == 2 and word >> 13 == 3:
        offset = ((word >> 2) & 7) << 6 | ((word >> 12) & 1) << 5 | ((word >> 5) & 3) << 3
        return (word >> 7) & 31, offset
    return None


def store_sp_slot(width, word):
    if width == 4 and word & 0x7f == 0x23 and (word >> 12) & 7 == 3 and (word >> 15) & 31 == 2:
        return (word >> 20) & 31, (word >> 25) << 5 | ((word >> 7) & 31)
    if width == 2 and word & 3 == 2 and word >> 13 == 7:
        offset = ((word >> 7) & 7) << 6 | ((word >> 10) & 7) << 3
        return (word >> 2) & 31, offset
    return None


def bound_constant(width, word):
    if width == 4 and word & 0x707f == 0x13 and (word >> 15) & 31 == 0:
        value = word >> 20
        return ((word >> 7) & 31, value) if value < 2048 else None
    if width == 2 and word & 3 == 1 and word >> 13 == 2:
        value = ((word >> 12) & 1) << 5 | ((word >> 2) & 31)
        return ((word >> 7) & 31, value) if value < 32 else None
    return None


def table_entries(obj, function, table, boundaries):
    if not 0 < table.section_index < len(obj.sections):
        raise StackEvidenceError("switch table has no owned data section")
    section = obj.sections[table.section_index]
    if (
        section.name != ".rodata." + function.name
        or section.flags != 2 or section.section_type != 1
    ):
        raise StackEvidenceError("switch table is not immutable function-owned data")
    following = [
        symbol.value for symbol in obj.symbols
        if symbol.section_index == table.section_index
        and re.fullmatch(r"\.LJTI[0-9]+_[0-9]+", symbol.name)
        and symbol.value > table.value
    ]
    end = min(following, default=section.size)
    if table.value >= end or (end - table.value) % 8:
        raise StackEvidenceError("switch table has an incomplete entry range")
    entries = {}
    data = section_bytes(obj, section)
    for address, kind, target, addend in relocations(obj, section.index):
        if not table.value <= address < end:
            continue
        destination = target.value + addend
        if (
            kind != 2 or address in entries or (address - table.value) % 8
            or target.section_index != function.section_index
            or destination not in boundaries
            or data[address:address + 8] != bytes(8)
        ):
            raise StackEvidenceError("switch entry leaves its exact C function")
        entries[address] = destination
    expected = tuple(range(table.value, end, 8))
    if set(entries) != set(expected):
        raise StackEvidenceError("missing or extra switch-table relocation")
    return tuple(entries[address] for address in expected)


def register_jump(width, word, register):
    if width == 4:
        return (
            word & 0x707f == 0x67 and (word >> 7) & 31 == 0
            and (word >> 15) & 31 == register and word >> 20 == 0
        )
    return (
        word & 3 == 2 and word >> 13 == 4 and (word >> 12) & 1 == 0
        and (word >> 7) & 31 == register and (word >> 2) & 31 == 0
    )


def lookup_register(width, word, base):
    if width == 4 and word & 0x707f == 0x3003 and (word >> 15) & 31 == base and word >> 20 == 0:
        return (word >> 7) & 31
    if width == 2 and word & 3 == 0 and word >> 13 == 3:
        source = 8 + ((word >> 7) & 7)
        offset = ((word >> 10) & 7) << 3 | ((word >> 5) & 3) << 6
        if source == base and offset == 0:
            return 8 + ((word >> 2) & 7)
    return None


def add_index(width, word, base, index):
    if width == 4:
        return (
            word & 0xfe00707f == 0x33 and (word >> 7) & 31 == base
            and {(word >> 15) & 31, (word >> 20) & 31} == {base, index}
        )
    return (
        word & 3 == 2 and word >> 13 == 4 and (word >> 12) & 1 == 1
        and (word >> 7) & 31 == base and (word >> 2) & 31 == index
    )

def zero_extends_index(left, right, source, destination):
    if (
        left[1] != 4 or left[2] & 0xfc00707f != 0x1013
        or (left[2] >> 7) & 31 != destination
        or (left[2] >> 15) & 31 != source or (left[2] >> 20) & 63 != 32
    ):
        return False
    if right[1] == 4:
        return (
            right[2] & 0xfc00707f == 0x5013
            and (right[2] >> 7) & 31 == destination
            and (right[2] >> 15) & 31 == destination
            and (right[2] >> 20) & 63 == 32
        )
    return (
        right[2] & 3 == 1 and right[2] >> 13 == 4
        and (right[2] >> 10) & 3 == 0
        and 8 + ((right[2] >> 7) & 7) == destination
        and (((right[2] >> 12) & 1) << 5 | ((right[2] >> 2) & 31)) == 32
    )


def prove_dispatch(decoded, position, relocations_by_address, count, code_section):
    if position < 5 or position + 4 >= len(decoded):
        raise StackEvidenceError("switch lacks its complete guarded dispatch")
    saved, constant, guard, loaded, scale = decoded[position - 5:position]
    high, low, combined, lookup, jump = decoded[position:position + 5]
    stored = store_sp_slot(saved[1], saved[2])
    bound = bound_constant(constant[1], constant[2])
    loaded_slot = load_sp_slot(loaded[1], loaded[2])
    comparison = (guard[2] >> 20) & 31
    if (
        stored is None or bound is None or loaded_slot is None
        or stored[1] != loaded_slot[1] or bound[1] != count - 1
        or guard[1] != 4 or guard[2] & 0x707f != 0x6063
        or (guard[2] >> 15) & 31 != bound[0]
        or bound[0] in (0, comparison)
    ):
        raise StackEvidenceError("switch index lacks its exact dominating bound")
    start = position - 5
    if comparison != stored[0]:
        if position < 7 or not zero_extends_index(
            decoded[position - 7], decoded[position - 6], comparison, stored[0],
        ):
            raise StackEvidenceError("switch stored index differs from its bounded value")
        start = position - 7
    if (
        scale[1] != 4 or scale[2] & 0xfc00707f != 0x1013
        or (scale[2] >> 15) & 31 != loaded_slot[0]
        or (scale[2] >> 20) & 63 != 3
    ):
        raise StackEvidenceError("switch index is not an unchanged eight-byte scale")
    base = (high[2] >> 7) & 31
    scaled = (scale[2] >> 7) & 31
    low_references = [entry for entry in relocations_by_address.get(low[0], ()) if entry[1] == 24]
    if (
        high[1] != 4 or high[2] & 0x7f != 0x17 or base in (0, scaled)
        or high[2] >> 12 != 0 or low[1] != 4 or low[2] & 0x707f != 0x13
        or (low[2] >> 7) & 31 != base or (low[2] >> 15) & 31 != base
        or low[2] >> 20 != 0 or len(low_references) != 1
        or low_references[0][2].section_index != code_section
        or low_references[0][2].value != high[0]
        or not add_index(combined[1], combined[2], base, scaled)
    ):
        raise StackEvidenceError("switch lookup lost its exact table register")
    target_register = lookup_register(lookup[1], lookup[2], base)
    if target_register in (None, 0, 1, 2) or not register_jump(jump[1], jump[2], target_register):
        raise StackEvidenceError("switch jump does not consume its bounded table entry")
    return jump[0], tuple(item[0] for item in decoded[start:position + 5])


def switch_jumps(obj, function):
    section = obj.sections[function.section_index]
    data = section_bytes(obj, section)[function.value:function.value + function.size]
    decoded = tuple(instructions(data, function.value))
    indices = {address: index for index, (address, _, _) in enumerate(decoded)}
    references = {}
    for entry in relocations(obj, section.index):
        references.setdefault(entry[0], []).append(entry)
    result = {}
    chains = []
    for address, entries in references.items():
        if address not in indices:
            continue
        for _, kind, table, addend in entries:
            if kind != 23 or not re.fullmatch(r"\.LJTI[0-9]+_[0-9]+", table.name):
                continue
            if addend != 0:
                raise StackEvidenceError("switch table has an unreviewed base offset")
            targets = table_entries(obj, function, table, set(indices))
            jump, chain = prove_dispatch(
                decoded, indices[address], references, len(targets), section.index,
            )
            if jump in result:
                raise StackEvidenceError("duplicate switch dispatch identity")
            result[jump] = targets
            chains.append(chain)
    protected = {address for chain in chains for address in chain[1:]}
    for address, entries in references.items():
        for _, kind, target, addend in entries:
            if kind in (16, 17, 44, 45) and target.section_index == section.index:
                if target.value + addend in protected:
                    raise StackEvidenceError("switch bound was bypassed by another control edge")
    for targets in result.values():
        if any(target in protected for target in targets):
            raise StackEvidenceError("switch target bypasses a guarded lookup")
    return result

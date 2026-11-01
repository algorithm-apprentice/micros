"""Exact LLD text-section ownership for qualified stack graph nodes."""

import re
import struct
from dataclasses import dataclass

from tools.stack_bound_types import (
    EdgeKind, FunctionKey, MAX_U64, StackEvidenceError, checked_add, require_function,
    require_path, require_uint64,
)
from tools.user_elf_format import _program_file_bytes


@dataclass(frozen=True)
class FunctionLocation:
    function: FunctionKey
    object_name: str
    section_name: str
    offset: int
    size: int


def text_owners(content):
    lines = content.splitlines()
    if not lines or lines[0].split() != ["VMA", "LMA", "Size", "Align", "Out", "In", "Symbol"]:
        raise StackEvidenceError("unsupported stack linker-map format")
    result = {}
    for line in lines[1:]:
        if ":(.text" not in line:
            continue
        match = re.fullmatch(
            r"\s*([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9]+)"
            r"\s+(.+\.(?:o|obj)):\((\.text(?:\.[A-Za-z0-9_.$-]+)?)\)\s*",
            line,
        )
        if match is None:
            raise StackEvidenceError("malformed stack linker input section")
        address, load_address, size = (int(match[index], 16) for index in (1, 2, 3))
        require_uint64(address, "linked text address")
        require_uint64(size, "linked text size")
        if address != load_address or int(match[4]) == 0:
            raise StackEvidenceError("unreviewed text load geometry")
        object_name, section = match[5], match[6]
        require_path(object_name)
        key = (object_name, section)
        if key in result:
            raise StackEvidenceError("duplicate linked object-section owner")
        result[key] = (address, size)
    if not result:
        raise StackEvidenceError("missing linked text ownership")
    return result


def join_function_locations(locations, map_text, symbols):
    if type(locations) is not tuple or type(symbols) is not tuple:
        raise StackEvidenceError("linked function inputs must be complete tuples")
    owners = text_owners(map_text)
    symbol_counts = {}
    for symbol in symbols:
        if symbol.symbol_type == 2 and 0 < symbol.section_index < 0xff00:
            identity = (symbol.name, symbol.value, symbol.size)
            symbol_counts[identity] = symbol_counts.get(identity, 0) + 1
    result = {}
    seen = set()
    claimed = set()
    for location in locations:
        if type(location) is not FunctionLocation:
            raise StackEvidenceError("invalid qualified function location")
        require_function(location.function)
        require_path(location.object_name)
        require_uint64(location.offset, "object function offset")
        require_uint64(location.size, "object function size")
        if location.function in seen or location.size == 0:
            raise StackEvidenceError("duplicate or empty function location")
        seen.add(location.function)
        owner = owners.get((location.object_name, location.section_name))
        if owner is None:
            continue
        base, size = owner
        if location.offset > size or location.size > size - location.offset:
            raise StackEvidenceError("function exceeds its linked input section range")
        address = checked_add(base, location.offset)
        identity = (location.function.symbol, address, location.size)
        if symbol_counts.get(identity) != 1 or identity in claimed:
            raise StackEvidenceError("function has no unique exact linked symbol owner")
        claimed.add(identity)
        result[location.function] = address
    return result


def direct_call_target(image, address):
    require_uint64(address, "linked call address")
    try:
        first, second = struct.unpack("<II", _program_file_bytes(image, address, 8))
    except ValueError as error:
        raise StackEvidenceError(f"linked call is not file-backed: {error}") from error
    basis = (first >> 7) & 31
    link = (second >> 7) & 31
    if (
        first & 0x7f != 0x17 or second & 0x707f != 0x67
        or basis == 0 or (second >> 15) & 31 != basis or link not in (0, 1)
    ):
        raise StackEvidenceError("linked call changed its exact register pair")
    high = first & 0xfffff000
    if high & (1 << 31):
        high -= 1 << 32
    low = second >> 20
    if low & (1 << 11):
        low -= 1 << 12
    target = address + high + low
    if not 0 <= target <= MAX_U64:
        raise StackEvidenceError("linked call target arithmetic overflow")
    return target & ~1, EdgeKind.TAIL if link == 0 else EdgeKind.CALL

"""Actual intrafunction control flow and checked normal-return dominance."""

from collections import deque
from dataclasses import dataclass

from tools.stack_bound_machine import branch_offset, is_call, is_register_jump
from tools.stack_bound_objects import call_instructions, instructions, relocations, section_bytes
from tools.stack_bound_switch import switch_jumps
from tools.stack_bound_types import EdgeKind, StackEvidenceError, require_uint64


@dataclass(frozen=True)
class FunctionFlow:
    instructions: dict
    successors: dict
    calls: tuple
    continuations: dict


def function_flow(obj, symbol):
    section = obj.sections[symbol.section_index]
    data = section_bytes(obj, section)[symbol.value:symbol.value + symbol.size]
    decoded = {
        address - symbol.value: (width, word)
        for address, width, word in instructions(data, symbol.value)
    }
    switches = switch_jumps(obj, symbol)
    calls = call_instructions(obj, symbol, None, switches)
    branches = {}
    for address, kind, target, addend in relocations(obj, section.index):
        if kind not in (16, 17, 44, 45) or not symbol.value <= address < symbol.value + symbol.size:
            continue
        relative = address - symbol.value
        if target.section_index != section.index or relative in branches:
            raise StackEvidenceError("unbound or duplicate function-control branch")
        branches[relative] = target.value + addend - symbol.value
    continuations = {}
    for call in calls:
        width, word = decoded[call.offset]
        size = 8 if width == 4 and word & 0x7f == 0x17 else width
        if call.kind is EdgeKind.CALL and call.offset + size in decoded:
            continuations[call.offset] = call.offset + size
    successors = {}
    for address, (width, word) in decoded.items():
        following = address + width
        if address + symbol.value in switches:
            targets = tuple(target - symbol.value for target in switches[address + symbol.value])
        elif is_call(width, word):
            targets = (following,) if following in decoded else ()
        elif is_register_jump(width, word):
            targets = ()
        elif (branch := branch_offset(width, word)) is not None:
            offset, conditional = branch
            target = branches.get(address, address + offset)
            targets = (target, following) if conditional else (target,)
        elif following in decoded:
            targets = (following,)
        elif (width, word) == (2, 0):
            # Terminal C.UNIMP faults; it is not a successful return.
            targets = ()
        else:
            raise StackEvidenceError("unexplained fallthrough leaves its function")
        if any(target not in decoded for target in targets):
            raise StackEvidenceError("control flow leaves its exact function range")
        successors[address] = targets
    if not decoded or 0 not in decoded:
        raise StackEvidenceError("function has no entry instruction")
    return FunctionFlow(decoded, successors, calls, continuations)


def reachable_instructions(flow, *, excluded=None, start=0):
    reached = set()
    if start not in flow.instructions:
        raise StackEvidenceError("control-flow reachability has no owned entry")
    ready = deque((start,))
    while ready:
        address = ready.popleft()
        if address == excluded or address in reached:
            continue
        reached.add(address)
        ready.extend(flow.successors[address])
    return frozenset(reached)


def normal_return_dominated_calls(obj, symbol, producer_offset):
    require_uint64(producer_offset, "transition call offset")
    flow = function_flow(obj, symbol)
    continuation = flow.continuations.get(producer_offset)
    if continuation is None:
        raise StackEvidenceError("transition lacks an owned normal-return call continuation")
    reached = reachable_instructions(flow)
    if producer_offset not in reached:
        raise StackEvidenceError("transition call is unreachable")
    bypass = reachable_instructions(flow, excluded=continuation)
    return frozenset(
        call.offset for call in flow.calls
        if call.offset != producer_offset and call.offset in reached and call.offset not in bypass
    )

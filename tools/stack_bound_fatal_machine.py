"""Closed zero-result and exact SRST machine boundaries for ADR-0086."""

from dataclasses import dataclass

from tools.stack_bound_flow import function_flow, reachable_instructions
from tools.stack_bound_machine import is_return
from tools.stack_bound_values import register_inputs
from tools.stack_bound_observer import runtime_call_inventory
from tools.stack_bound_types import StackEvidenceError


@dataclass(frozen=True)
class ZeroGate:
    branch: int
    zero: int
    nonzero: int


def result_zero_gate(obj, symbol, producer, *, bits=32):
    flow = function_flow(obj, symbol)
    inputs = register_inputs(obj, symbol, result_call=producer, result_bits=bits)
    expected = ("result", producer, bits)
    result = []
    for address, registers in inputs.items():
        width, word = flow.instructions[address]
        operation = (word >> 12) & 7
        if width == 4 and word & 0x7f == 0x63 and operation in (0, 1):
            left, right = registers[(word >> 15) & 31], registers[(word >> 20) & 31]
            if (left, right) not in ((expected, ("scalar", 0)), (("scalar", 0), expected)):
                continue
            is_equal = operation == 0
        elif width == 2 and word & 3 == 1 and word >> 13 in (6, 7):
            if registers[8 + ((word >> 7) & 7)] != expected:
                continue
            is_equal = word >> 13 == 6
        else:
            continue
        target, following = flow.successors[address]
        result.append(ZeroGate(address, target if is_equal else following, following if is_equal else target))
    if len(result) != 1:
        raise StackEvidenceError("transition lacks one exact zero-result machine guard")
    gate, = result
    if gate.branch in reachable_instructions(flow, excluded=flow.continuations[producer]):
        raise StackEvidenceError("result guard bypasses its call continuation dominance")
    return gate


def zero_success_returns(obj, symbol, producer):
    flow = function_flow(obj, symbol)
    gate = result_zero_gate(obj, symbol, producer)
    returns = {
        address for address, (width, word) in flow.instructions.items()
        if is_return(width, word)
    }
    zero = reachable_instructions(flow, start=gate.zero)
    nonzero = reachable_instructions(flow, start=gate.nonzero)
    bypass = reachable_instructions(flow, excluded=flow.continuations[producer])
    if not returns or returns & (nonzero | bypass) or not returns <= zero:
        raise StackEvidenceError("failed or bypassed transition reaches a ready normal return")
    return gate.branch


def require_srst_registers(obj, symbol):
    flow = function_flow(obj, symbol)
    systems = tuple(
        (address, word) for address, (width, word) in flow.instructions.items()
        if width == 4 and word & 0x7f == 0x73
    )
    runtime_calls, _ = runtime_call_inventory(obj, symbol)
    if len(systems) != 1 or systems[0][1] != 0x73 or runtime_calls:
        raise StackEvidenceError("SRST wrapper has an unbound call or system operation")
    address, _ = systems[0]
    inputs = register_inputs(obj, symbol, arguments=(0, 1))
    operands = tuple(inputs[address][register] for register in (10, 11, 16, 17))
    if operands != tuple(("scalar", value) for value in (0, 1, 0, 0x53525354)):
        raise StackEvidenceError("SRST ecall changed its exact scalar argument registers")
    returns = {
        point for point, (width, word) in flow.instructions.items()
        if is_return(width, word)
    }
    if address + 4 not in flow.instructions or not returns or returns & reachable_instructions(
        flow, excluded=address + 4,
    ):
        raise StackEvidenceError("SRST wrapper has a redirected return continuation")
    return address

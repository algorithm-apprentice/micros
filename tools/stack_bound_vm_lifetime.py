"""Actual ADR-0087 preparation/validation frame separation."""

from dataclasses import dataclass
from pathlib import Path

from tests.host.test_vm_load_stack_lifetime import (
    PREPARE_NAME, WORKER_NAME, verify_prepare_lifetime,
)
from tools.stack_bound_artifacts import object_frames
from tools.stack_bound_flow import function_flow, reachable_instructions
from tools.stack_bound_machine import is_return, require_c_frame_movement
from tools.stack_bound_objects import function_symbols
from tools.stack_bound_types import EdgeKind, StackEvidenceError


@dataclass(frozen=True)
class PrepareLifetime:
    facade_frame: int
    worker_frame: int
    worker_call: int
    final_validation: int
    return_sites: tuple


def require_vm_prepare_lifetime(root, obj):
    try:
        verify_prepare_lifetime((Path(root) / "servers/vm/vm_load.c").read_text())
    except AssertionError as error:
        raise StackEvidenceError(f"VM source lifetime is not the Accepted split: {error}") from error
    symbols = function_symbols(obj)
    sizes = object_frames(obj, symbols)
    if PREPARE_NAME not in symbols or WORKER_NAME not in symbols:
        raise StackEvidenceError("VM compiler did not retain separate preparation frames")
    facade, worker = symbols[PREPARE_NAME], symbols[WORKER_NAME]
    if facade.binding != 1 or worker.binding != 0:
        raise StackEvidenceError("VM preparation lost its public/private function ownership")
    for symbol in (facade, worker):
        require_c_frame_movement(obj, symbol, sizes[symbol.name])
    flow, work = function_flow(obj, facade), function_flow(obj, worker)
    calls = tuple(call for call in flow.calls if call.target_symbol == WORKER_NAME)
    validators = tuple(call for call in flow.calls if call.target_symbol == "vm_core_validate_state")
    if (
        len(calls) != 1 or calls[0].kind is not EdgeKind.CALL or len(validators) != 2
        or any(call.target_symbol == "vm_core_validate_state" for call in work.calls)
    ):
        raise StackEvidenceError("VM preparation has an unbound or overlapping validator call")
    call, = calls
    initial, final = validators
    continuation = flow.continuations.get(call.offset)
    if (
        initial.offset >= call.offset or final.offset <= call.offset or continuation is None
        or final.offset in reachable_instructions(flow, excluded=continuation)
        or call.offset in reachable_instructions(flow, excluded=flow.continuations[initial.offset])
    ):
        raise StackEvidenceError("VM final validation bypasses the released preparation continuation")
    returns = tuple(point for point, instruction in work.instructions.items() if is_return(*instruction))
    if not returns:
        raise StackEvidenceError("VM preparation has no exact normal-return frame release")
    return PrepareLifetime(sizes[PREPARE_NAME], sizes[WORKER_NAME], call.offset, final.offset, returns)

"""Finite actual-machine console relations; no caller-supplied ready fact."""

from dataclasses import dataclass
import hashlib
from pathlib import Path

from tests.host.c_source_records import tokens
from tools.stack_artifact_files import read_regular
from tools.stack_bound_artifacts import object_frames
from tools.stack_bound_bindings import reference_source
from tools.stack_bound_flow import function_flow
from tools.stack_bound_machine import (
    branch_offset, is_call, is_return, require_c_frame_movement,
)
from tools.stack_bound_objects import function_symbols
from tools.stack_bound_observer import runtime_call_inventory
from tools.stack_bound_pointer import memory_action
from tools.stack_bound_types import EdgeKind, StackEvidenceError
from tools.stack_bound_values import pcrel_values, value_transfer


MAGIC = 0x554152544f574e52
STATE_POINTER = ("argument", 0, ())
NAMES = (
    "micros_uart_console_panic", "micros_uart_console_validate",
    "micros_uart_console_output_allowed",
)


@dataclass(frozen=True)
class ConsoleOutcome:
    before: tuple | None
    result: int
    after: tuple | None
    reads: tuple
    writes: tuple


@dataclass(frozen=True)
class ConsoleMachineProof:
    object_sha256: str
    transitions: tuple[ConsoleOutcome, ...]
    predicates: tuple[ConsoleOutcome, ...]


def equality(left, right):
    if left is None or right is None:
        raise StackEvidenceError("console branch consumes an unknown operand")
    if left[0] == right[0] == "scalar":
        return (left[1] & ((1 << 64) - 1)) == (right[1] & ((1 << 64) - 1))
    if left == right:
        return True
    if (left, right) in (
        (STATE_POINTER, ("scalar", 0)), (("scalar", 0), STATE_POINTER),
    ):
        return False
    raise StackEvidenceError("console branch borrows an unbound pointer comparison")


def branch_taken(width, word, registers):
    if width == 2:
        result = equality(registers[8 + (word >> 7 & 7)], ("scalar", 0))
        return result if word >> 13 == 6 else not result
    operation = word >> 12 & 7
    left, right = registers[word >> 15 & 31], registers[word >> 20 & 31]
    if operation in (0, 1):
        result = equality(left, right)
        return result if operation == 0 else not result
    if left is None or right is None or left[0] != "scalar" or right[0] != "scalar":
        raise StackEvidenceError("console range branch has unknown scalar operands")
    first, second = left[1], right[1]
    if operation in (6, 7):
        first, second = first & ((1 << 64) - 1), second & ((1 << 64) - 1)
    if operation not in (4, 5, 6, 7):
        raise StackEvidenceError("console has an unsupported conditional branch")
    return first < second if operation in (4, 6) else first >= second


def require_console_instruction(width, word):
    if memory_action(width, word) is not None or branch_offset(width, word) is not None:
        return
    if is_call(width, word) or is_return(width, word):
        return
    if width == 4:
        opcode, operation = word & 0x7f, word >> 12 & 7
        if (
            opcode in (0x17, 0x37)
            or opcode == 0x13 and operation in (0, 1, 5, 7)
            or opcode == 0x1b and operation == 0
            or opcode == 0x33 and (
                operation == 0 and word >> 25 in (0, 0x20)
                or operation == 3 and word >> 25 == 0
            )
        ):
            return
    else:
        quadrant, operation = word & 3, word >> 13
        if (
            quadrant == 1 and operation in (0, 1, 2, 3)
            or quadrant == 1 and operation == 4 and word >> 10 & 3 == 2
            or quadrant == 0 and operation == 0
            or quadrant == 2 and operation == 4 and word >> 2 & 31
        ):
            return
    raise StackEvidenceError("console relation has an unmodeled instruction/effect")


def _execute(obj, symbol, before, validation=None):
    flow = function_flow(obj, symbol)
    runtime, _ = runtime_call_inventory(obj, symbol)
    calls = {call.offset: call for call in runtime}
    if calls and (
        symbol.name != NAMES[2] or len(calls) != 1
        or next(iter(calls.values())).target_symbol != NAMES[1]
        or next(iter(calls.values())).kind is not EdgeKind.CALL
        or validation is None or validation.before != before
        or validation.after != before or validation.writes
    ):
        raise StackEvidenceError("console relation has an unbound or mutating callee")
    registers = [None] * 32
    registers[0], registers[2] = ("scalar", 0), ("stack", 0)
    registers[10] = STATE_POINTER if before is not None else ("scalar", 0)
    state = tuple(registers), ()
    data = None if before is None else bytearray(
        before[0].to_bytes(8, "little") + before[1].to_bytes(4, "little")
        + bytes((int(before[2]), 0, 0, 0))
    )
    high, low = pcrel_values(obj, symbol, flow)
    produced = {}
    reads, writes, visited = set(), [], set()
    point = 0
    while True:
        if point in visited:
            raise StackEvidenceError("console relation has an unexplained execution cycle")
        visited.add(point)
        width, word = flow.instructions[point]
        require_console_instruction(width, word)
        if point in calls:
            if state[0][10] != registers[10]:
                raise StackEvidenceError("console predicate borrowed another state argument")
            continuation = flow.continuations[point]
            last = max(offset for offset in flow.instructions if point <= offset < continuation)
            produced[last] = ("scalar", validation.result)
            reads.update(validation.reads)
        action = memory_action(width, word)
        changed, stored = value_transfer(obj, state, point, width, word, high, low, produced)
        changed = list(changed)
        if action is not None:
            write, size, register, basis, offset = action
            base = state[0][basis]
            if base == STATE_POINTER:
                if data is None or (offset, size) not in ((0, 8), (8, 4), (12, 1)):
                    raise StackEvidenceError("console operand borrows another state field")
                if write:
                    value = state[0][register]
                    if (offset, size) == (0, 8) or value is None or value[0] != "scalar":
                        raise StackEvidenceError("console transition has an unbound field write")
                    number = value[1]
                    if not 0 <= number < 1 << (8 * size):
                        raise StackEvidenceError("console transition has a noncanonical field value")
                    data[offset:offset + size] = number.to_bytes(size, "little")
                    writes.append((offset, size, number))
                else:
                    unsigned = width == 4 and word >> 12 & 7 in (4, 5, 6)
                    changed[register] = (
                        "scalar", int.from_bytes(data[offset:offset + size], "little", signed=not unsigned),
                    )
                    reads.add((offset, size))
            elif write and (base is None or base[0] != "stack"):
                raise StackEvidenceError("console relation has an escaping memory write")
            elif not write and changed[register] is not None and changed[register][0] == "field":
                raise StackEvidenceError("console relation has an unbound memory read")
        if width == 4 and word & 0xfe00707f == 0x3033:
            left, right = state[0][word >> 15 & 31], state[0][word >> 20 & 31]
            if left is None or right is None or left[0] != "scalar" or right[0] != "scalar":
                raise StackEvidenceError("console Boolean comparison has unknown scalar operands")
            changed[word >> 7 & 31] = (
                "scalar", int((left[1] & ((1 << 64) - 1)) < (right[1] & ((1 << 64) - 1))),
            )
        state = tuple(changed), stored
        if is_return(width, word):
            if changed[2] != ("stack", 0) or changed[10] is None or changed[10][0] != "scalar":
                raise StackEvidenceError("console relation has no exact normal-return value/stack")
            after = None if data is None else (
                int.from_bytes(data[:8], "little"), int.from_bytes(data[8:12], "little"),
                bool(data[12]),
            )
            return ConsoleOutcome(before, changed[10][1], after, tuple(sorted(reads)), tuple(writes))
        successors = flow.successors[point]
        branch = branch_offset(width, word)
        if branch is not None and branch[1]:
            point = successors[0 if branch_taken(width, word, state[0]) else 1]
        elif len(successors) == 1:
            point = successors[0]
        else:
            raise StackEvidenceError("console relation has an unbound control transfer")


def console_machine_proof(root, obj):
    for unit in ("kernel/tty/uart_console_core.c", "kernel/tty/uart_console_core.h"):
        actual = read_regular(Path(root) / unit)[0].decode("utf-8")
        if tokens(actual) != tokens(reference_source(str(root), unit)):
            raise StackEvidenceError("console source/type relation differs from its fixed stage")
    symbols = function_symbols(obj)
    frames = object_frames(obj, symbols)
    if any(name not in symbols or name not in frames for name in NAMES):
        raise StackEvidenceError("console relation lacks its complete function/frame inventory")
    for name in NAMES:
        require_c_frame_movement(obj, symbols[name], frames[name])
    cases = (
        None, *((MAGIC, owner, quiesced) for owner in (1, 2, 3, 4) for quiesced in (False, True)),
        (0, 1, False), (0, 3, True),
    )
    transitions, predicates = [], []
    for before in cases:
        transition = _execute(obj, symbols[NAMES[0]], before)
        expected = 1 if before is None else 3 if before[0] != MAGIC else 0
        after = before if expected else (MAGIC, 3, True)
        writes = () if expected else ((8, 4, 3), (12, 1, 1))
        if (
            (transition.result, transition.after, transition.writes) != (expected, after, writes)
            or set(transition.reads) - {(0, 8)}
        ):
            raise StackEvidenceError("console machine does not establish the exact ready return relation")
        validation = _execute(obj, symbols[NAMES[1]], before)
        valid = before is not None and before[0] == MAGIC and (
            before[1] == 1 or before[1] in (2, 3) and before[2]
        )
        validation_error = 0 if valid else 1 if before is None else 3
        if (validation.result, validation.after, validation.writes) != (validation_error, before, ()):
            raise StackEvidenceError("console validator differs from its readonly state relation")
        predicate = _execute(obj, symbols[NAMES[2]], before, validation)
        allowed = int(valid and before[1] != 2)
        if (predicate.result, predicate.after, predicate.writes) != (allowed, before, ()):
            raise StackEvidenceError("console output guard differs from its readonly ready relation")
        transitions.append(transition)
        predicates.append(predicate)
    return ConsoleMachineProof(hashlib.sha256(obj.data).hexdigest(), tuple(transitions), tuple(predicates))

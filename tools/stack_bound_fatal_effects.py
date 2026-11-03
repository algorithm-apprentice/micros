"""Closed UART/interrupt machine effects, not a complete ready qualifier."""

from collections import Counter, deque
from dataclasses import dataclass

from tools.stack_bound_artifacts import object_frames
from tools.stack_bound_flow import function_flow, reachable_instructions
from tools.stack_bound_fatal_machine import require_srst_registers, result_zero_gate
from tools.stack_bound_machine import branch_offset, is_call, is_return, require_c_frame_movement, signed
from tools.stack_bound_objects import function_symbols
from tools.stack_bound_observer import runtime_call_inventory
from tools.stack_bound_pointer import memory_action
from tools.stack_bound_types import EdgeKind, StackEvidenceError
from tools.stack_bound_values import merge_values, pcrel_values, register_inputs, value_transfer
from tools.user_elf_instructions import _instruction_destination
from tools.stack_bound_console import (
    STATE_POINTER, branch_taken, require_console_instruction,
)


UART_BASE = 0x10000000
CALLEES = {
    "uart_flush": (),
    "uart_write_character": (),
    "uart_write_hex64_direct": ("uart_write", "uart_write_character"),
    "uart_write_direct": (
        "micros_uart_console_output_allowed", "micros_panic_entry",
        "uart_write_character", "uart_write_character",
    ),
    "uart_write": ("uart_panic_mode", "uart_write_direct", "uart_write_record"),
    "uart_write_hex64": ("uart_panic_mode", "uart_write_hex64_direct", "uart_write_hex64_record"),
    "uart_panic_mode": (),
    "uart_panic_seize": (
        "riscv_irq_save", "riscv_external_interrupt_disable",
        "micros_uart_console_panic", "riscv_mmio_fence",
    ),
    "riscv_irq_save": (),
    "riscv_external_interrupt_disable": (),
    "riscv_mmio_fence": (),
}


@dataclass(frozen=True)
class MachineEffects:
    calls: tuple
    writes: tuple
    systems: tuple
    dependencies: tuple = ()


def require_effect_return_dominance(flow, continuation):
    returns = {
        point for point, (width, word) in flow.instructions.items() if is_return(width, word)
    }
    if (
        continuation not in flow.instructions or not returns
        or returns & reachable_instructions(flow, excluded=continuation)
    ):
        raise StackEvidenceError("normal UART return bypasses its required state effect")


def _uart_callees(name, test_counter, terminal, terminal_test):
    if any(type(flag) is not bool for flag in (test_counter, terminal, terminal_test)) or (
        terminal_test and (not terminal or test_counter)
    ):
        raise StackEvidenceError("UART effect has an unknown or overlapping observation mode")
    callees = CALLEES[name]
    if name in ("uart_write_character", "uart_flush"):
        if terminal:
            callees += ("micros_panic_terminate_no_console",)
        if terminal_test:
            callees += ("micros_test_work_record", "micros_terminal_test_lsr")
    if name == "uart_write_character":
        if test_counter:
            callees += ("micros_diagnostic_test_observe_thr",)
        if terminal_test:
            callees += ("micros_terminal_test_thr",)
    if name == "uart_panic_seize" and terminal_test:
        callees += ("micros_terminal_test_seizure",)
    return callees


def _uart_repair_effects(obj, symbol, console, repairs, *, terminal_test):
    if console is None or console.name != "uart_console_state" or console.size != 16:
        raise StackEvidenceError("terminal UART repair has no exact console binding")
    if tuple(row[1:] for row in repairs) != (
        (8, 0, ("scalar", 0x554152544f574e52)), (4, 8, ("scalar", 3)), (1, 12, ("scalar", 1)),
    ):
        raise StackEvidenceError("terminal UART repair changed a semantic field, value or padding")
    flow = function_flow(obj, symbol)
    core, = (call for call in flow.calls if call.target_symbol == "micros_uart_console_panic")
    if register_inputs(obj, symbol)[core.offset][10] != (
        "symbol", console.name, console.section_index, console.value,
    ):
        raise StackEvidenceError("terminal seizure borrowed another console object")
    gate = result_zero_gate(obj, symbol, core.offset)
    returns = {point for point, instruction in flow.instructions.items() if is_return(*instruction)}
    for point, *_ in repairs:
        if (
            point in reachable_instructions(flow, start=gate.zero)
            or point in reachable_instructions(flow, excluded=gate.branch)
            or returns & reachable_instructions(flow, start=gate.nonzero, excluded=point + flow.instructions[point][0])
        ):
            raise StackEvidenceError("terminal repair does not dominate exactly its failed-console continuation")
    relations = []
    for code in (0, 3):
        result, value, trace = _terminal_execution(obj, symbol, (), {
            "riscv_irq_save": None, "riscv_external_interrupt_disable": None, "riscv_mmio_fence": None,
            "micros_uart_console_panic": code,
            **({"micros_terminal_test_seizure": None} if terminal_test else {}),
        })
        if (result, value) != ("return", ("scalar", int(code != 0))):
            raise StackEvidenceError("terminal UART repaired-status return is false or unbound")
        relations.append((code, value, trace))
    return ("console-repair", tuple(repairs), tuple(relations))


def _uart_seizure_effects(obj, symbol, writes, repairs, console, *, terminal, terminal_test):
    expected = (
        (1, 3, ("scalar", 3)), (1, 1, ("scalar", 0)),
        (1, 3, ("scalar", 131)), (1, 0, ("scalar", 2)),
        (1, 1, ("scalar", 0)), (1, 3, ("scalar", 3)),
    )
    if tuple(record[1:] for record in writes) != expected:
        raise StackEvidenceError("UART seizure changed its exact MMIO transition")
    symbols = function_symbols(obj)
    names = ("riscv_irq_save", "riscv_external_interrupt_disable", "riscv_mmio_fence")
    if any(name not in symbols for name in names):
        raise StackEvidenceError("UART seizure lacks a complete interrupt/fence provider")
    dependencies = tuple((name, uart_machine_effects(obj, symbols[name])) for name in names)
    flow = function_flow(obj, symbol)
    for call in flow.calls:
        require_effect_return_dominance(flow, flow.continuations[call.offset])
    for point, *_ in writes:
        require_effect_return_dominance(flow, point + flow.instructions[point][0])
    if terminal:
        dependencies += (_uart_repair_effects(obj, symbol, console, repairs, terminal_test=terminal_test),)
    elif repairs:
        raise StackEvidenceError("old UART seizure has an undeclared console repair")
    return dependencies


def uart_machine_effects(obj, symbol, *, test_counter=False, terminal=False, terminal_test=False, console=None):
    symbols = function_symbols(obj)
    if symbol.name not in CALLEES or symbols.get(symbol.name) != symbol:
        raise StackEvidenceError("unknown or borrowed UART effect function")
    frames = object_frames(obj, symbols)
    if symbol.name not in frames:
        raise StackEvidenceError("UART effects lack their actual compiler frame")
    require_c_frame_movement(obj, symbol, frames[symbol.name])
    flow = function_flow(obj, symbol)
    runtime_calls, hooks = runtime_call_inventory(obj, symbol)
    callees = _uart_callees(symbol.name, test_counter, terminal, terminal_test)
    if (
        Counter(call.target_symbol for call in runtime_calls) != Counter(callees)
        or any(call.kind is not EdgeKind.CALL for call in runtime_calls)
    ):
        raise StackEvidenceError("UART effect call inventory is incomplete or unbound")
    inputs = register_inputs(obj, symbol)
    writes, systems, fences, repairs = [], [], [], []
    for point, (width, word) in flow.instructions.items():
        if point not in inputs:
            raise StackEvidenceError("UART effect has an unbound machine entry/instruction")
        action = memory_action(width, word)
        if action is not None and action[0]:
            _, size, register, basis, offset = action
            base, value = inputs[point][basis], inputs[point][register]
            if base is not None and base[0] == "stack":
                if terminal and (base[1] + offset < -frames[symbol.name] or base[1] + offset + size > 0):
                    raise StackEvidenceError("terminal UART escaped its actual local-counter frame")
                continue
            if (
                terminal and symbol.name == "uart_panic_seize" and console is not None and base is not None
                and base[:3] == ("symbol", console.name, console.section_index)
            ):
                repairs.append((point, size, base[3] + offset - console.value, value))
                continue
            if base != ("scalar", UART_BASE):
                raise StackEvidenceError("UART effect has an escaping console/foreign memory write")
            writes.append((point, size, offset, value))
        if width == 4 and word & 0x7f == 0x73:
            systems.append((point, word))
        elif width == 4 and word & 0x7f == 0x0f:
            fences.append((point, word))
        elif (
            action is None and branch_offset(width, word) is None
            and not is_call(width, word) and not is_return(width, word)
        ):
            pure = (
                width == 4 and word & 0x7f in (0x13, 0x1b, 0x33, 0x3b, 0x17, 0x37)
                or width == 2 and (
                    word & 3 == 1 and word >> 13 in (0, 1, 2, 3, 4)
                    or word & 3 == 0 and word >> 13 == 0
                    or word & 3 == 2 and word >> 13 == 4 and word >> 2 & 31
                )
            )
            if not pure:
                raise StackEvidenceError("UART preservation has an unmodeled instruction/effect")
    if symbol.name == "uart_write_character":
        if len(writes) != 1 or writes[0][1:3] != (1, 0):
            raise StackEvidenceError("UART character writer lost its exact THR-only effect")
        for name in (("micros_diagnostic_test_observe_thr",) if test_counter else ()) + (
            ("micros_terminal_test_thr",) if terminal_test else ()
        ):
            observation, = (call for call in runtime_calls if call.target_symbol == name)
            store = writes[0][0]
            continuation = flow.continuations[observation.offset]
            if (
                observation.offset <= store
                or observation.offset in reachable_instructions(
                    flow, excluded=store + flow.instructions[store][0],
                )
                or store in reachable_instructions(flow, start=continuation)
                or observation.offset in reachable_instructions(flow, start=continuation)
            ):
                raise StackEvidenceError("THR observation does not follow exactly its actual store")
            require_effect_return_dominance(flow, continuation)
    elif symbol.name != "uart_panic_seize" and writes:
        raise StackEvidenceError("readonly UART preservation function writes outside its frame")
    if symbol.name == "riscv_irq_save":
        if len(systems) != 1 or systems[0][1] & ~(31 << 7) != 0x10017073:
            raise StackEvidenceError("UART seizure lacks the exact SIE-clear operation")
        require_effect_return_dominance(flow, systems[0][0] + 4)
    elif symbol.name == "riscv_external_interrupt_disable":
        if len(systems) != 1 or (
            systems[0][1] & ~(31 << 15) != 0x10403073
            or inputs[systems[0][0]][systems[0][1] >> 15 & 31] != ("scalar", 512)
        ):
            raise StackEvidenceError("UART seizure lacks the exact SEIE-clear operand")
        require_effect_return_dominance(flow, systems[0][0] + 4)
    elif systems:
        raise StackEvidenceError("UART preservation has an unbound CSR/system effect")
    if symbol.name == "riscv_mmio_fence":
        if len(fences) != 1 or fences[0][1] != 0x0ff0000f:
            raise StackEvidenceError("UART seizure lacks its exact MMIO fence")
        require_effect_return_dominance(flow, fences[0][0] + 4)
    elif fences:
        raise StackEvidenceError("UART effect has an undeclared fence operation")
    dependencies = ()
    if symbol.name == "uart_panic_seize":
        dependencies = _uart_seizure_effects(
            obj, symbol, writes, repairs, console, terminal=terminal, terminal_test=terminal_test,
        )
    return MachineEffects(
        tuple((call.offset, call.target_symbol) for call in runtime_calls),
        tuple(writes), tuple(systems), dependencies,
    )


def diagnostic_recording_guard(obj, symbol):
    if symbol.name not in ("uart_write", "uart_write_hex64"):
        raise StackEvidenceError("recording exclusion has an undeclared logical writer")
    uart_machine_effects(obj, symbol)
    flow = function_flow(obj, symbol)
    predicates = tuple(call for call in flow.calls if call.target_symbol == "uart_panic_mode")
    excluded = tuple(call for call in flow.calls if call.target_symbol == symbol.name + "_record")
    if len(predicates) != 1 or len(excluded) != 1:
        raise StackEvidenceError("recording exclusion lacks its complete selector and recording site")
    predicate, guard = predicates[0], excluded[0]
    gate = result_zero_gate(obj, symbol, predicate.offset, bits=1)
    if (
        guard.offset not in reachable_instructions(flow, start=gate.zero)
        or guard.offset in reachable_instructions(flow, start=gate.nonzero)
        or guard.offset in reachable_instructions(flow, excluded=gate.branch)
    ):
        raise StackEvidenceError("recording edge bypasses the false selector continuation")
    return predicate.offset, guard.offset, gate.branch


def sbi_machine_effects(obj, symbol):
    symbols = function_symbols(obj)
    if symbol.name not in ("sbi_system_reset", "sbi_set_timer") or symbols.get(symbol.name) != symbol:
        raise StackEvidenceError("unknown or borrowed SBI preservation function")
    frame = object_frames(obj, symbols)[symbol.name]
    require_c_frame_movement(obj, symbol, frame)
    flow, inputs = function_flow(obj, symbol), register_inputs(obj, symbol)
    runtime_calls, _ = runtime_call_inventory(obj, symbol)
    if runtime_calls:
        raise StackEvidenceError("SBI preservation wrapper has an unbound callee")
    systems = []
    for point, (width, word) in flow.instructions.items():
        if point not in inputs:
            raise StackEvidenceError("SBI preservation has an unowned machine entry")
        action = memory_action(width, word)
        if action is not None:
            _, size, _, basis, offset = action
            base = inputs[point][basis]
            if (
                base is None or base[0] != "stack" or base[1] + offset < -frame
                or base[1] + offset + size > 0
            ):
                raise StackEvidenceError("returning SBI wrapper accesses foreign supervisor memory")
        elif width == 4 and word & 0x7f == 0x73:
            if word != 0x73:
                raise StackEvidenceError("returning SBI wrapper changes supervisor interrupt state")
            systems.append((point, word))
        elif (
            width == 4 and word & 0x7f not in (0x13, 0x1b, 0x33, 0x3b, 0x17, 0x37, 0x63, 0x67)
            or width == 2 and not (
                word & 3 == 1 and word >> 13 in (0, 1, 2, 3, 4, 5, 6, 7)
                or word & 3 == 0 and word >> 13 == 0
                or word & 3 == 2 and word >> 13 == 4
            )
        ):
            raise StackEvidenceError("returning SBI wrapper has an unmodeled effect")
    if len(systems) != 1:
        raise StackEvidenceError("SBI preservation has an incomplete ecall inventory")
    if symbol.name == "sbi_system_reset":
        require_srst_registers(obj, symbol)
    return MachineEffects((), (), tuple(systems))


_OTHER_PHASE = ("other-u32-phase",)


def _control_branch(width, word, registers):
    if width == 2:
        left, right = registers[8 + (word >> 7 & 7)], ("scalar", 0)
        equal = word >> 13 == 6
    else:
        left, right = registers[word >> 15 & 31], registers[word >> 20 & 31]
        equal = word >> 12 & 7 == 0
    if _OTHER_PHASE not in (left, right):
        return branch_taken(width, word, registers)
    other = right if left == _OTHER_PHASE else left
    if (
        other not in (("scalar", 0), ("scalar", 1), ("scalar", 2))
        or width == 4 and word >> 12 & 7 not in (0, 1)
    ):
        raise StackEvidenceError("panic phase class has an unbound comparison/coercion")
    return not equal


def _control_relation(obj, symbol, frame, before):
    flow = function_flow(obj, symbol)
    runtime, _ = runtime_call_inventory(obj, symbol)
    if runtime:
        raise StackEvidenceError("portable panic transition has an unbound callee")
    registers = [None] * 32
    registers[0], registers[2] = ("scalar", 0), ("stack", 0)
    registers[10] = ("scalar", 0) if before is None else STATE_POINTER
    state, phase = (tuple(registers), ()), before
    high, low = pcrel_values(obj, symbol, flow)
    visited, reads, writes = set(), set(), []
    point = 0
    while True:
        if point in visited:
            raise StackEvidenceError("portable panic transition has an unbounded machine cycle")
        visited.add(point)
        width, word = flow.instructions[point]
        require_console_instruction(width, word)
        action = memory_action(width, word)
        changed, stored = value_transfer(obj, state, point, width, word, high, low, {})
        changed = list(changed)
        if action is not None:
            write, size, register, basis, offset = action
            base = state[0][basis]
            if base == STATE_POINTER:
                if before is None or (offset, size) != (0, 4):
                    raise StackEvidenceError("panic transition borrowed another object/field width")
                if write:
                    value = state[0][register]
                    if value is None or value[0] != "scalar" or not 0 <= value[1] <= 0xffffffff:
                        raise StackEvidenceError("panic phase write has an unbound scalar value")
                    phase = value[1]
                    writes.append((offset, size, phase))
                else:
                    changed[register] = _OTHER_PHASE if phase == "other" else ("scalar", phase)
                    reads.add((offset, size))
            elif base is not None and base[0] == "stack":
                if base[1] + offset < -frame or base[1] + offset + size > 0:
                    raise StackEvidenceError("panic transition escaped its actual compiler frame")
            else:
                raise StackEvidenceError("panic transition accesses unbound foreign storage")
        state = tuple(changed), stored
        if is_return(width, word):
            if changed[2] != ("stack", 0) or changed[10] is None or changed[10][0] != "scalar":
                raise StackEvidenceError("panic transition lacks an exact scalar/stack return")
            return before, changed[10][1], phase, tuple(sorted(reads)), tuple(writes)
        branch = branch_offset(width, word)
        successors = flow.successors[point]
        if branch is not None and branch[1]:
            point = successors[0 if _control_branch(width, word, state[0]) else 1]
        elif len(successors) == 1:
            point = successors[0]
        else:
            raise StackEvidenceError("panic transition has an unbound machine continuation")


def panic_control_effects(obj):
    symbols = function_symbols(obj)
    frames = object_frames(obj, symbols)
    result = []
    rules = (
        ("enter", "micros_panic_control_enter", ((None, 2, None), (0, 0, 1), (1, 1, 1), (2, 1, 2), ("other", 2, "other"))),
        ("reset", "micros_panic_control_request_reset", ((None, 2, None), (0, 0, 2), (1, 0, 2), (2, 1, 2), ("other", 2, "other"))),
    )
    for role, name, cases in rules:
        if name not in symbols or name not in frames:
            raise StackEvidenceError("panic machine proof lacks both actual transition owners")
        require_c_frame_movement(obj, symbols[name], frames[name])
        outcomes = []
        for before, returned, after in cases:
            actual = _control_relation(obj, symbols[name], frames[name], before)
            reads = () if before is None else ((0, 4),)
            writes = () if before == after else ((0, 4, after),)
            if actual != (before, returned, after, reads, writes):
                raise StackEvidenceError("panic machine transition differs from its complete phase relation")
            outcomes.append(actual)
        result.append((role, tuple(outcomes)))
    return tuple(result)


def _terminal_stack_effects(obj, symbol, expected_calls, systems=()):
    symbols = function_symbols(obj)
    if symbols.get(symbol.name) != symbol:
        raise StackEvidenceError("terminal effect borrowed another machine function")
    frame = object_frames(obj, symbols)[symbol.name]
    require_c_frame_movement(obj, symbol, frame)
    flow = function_flow(obj, symbol)
    runtime, _ = runtime_call_inventory(obj, symbol)
    if (
        Counter(call.target_symbol for call in runtime) != Counter(expected_calls)
        or any(call.kind is not EdgeKind.CALL for call in runtime)
    ):
        raise StackEvidenceError("terminal effect has an incomplete or unbound call inventory")
    inputs = register_inputs(obj, symbol)
    observed = []
    for point, (width, word) in flow.instructions.items():
        action = memory_action(width, word)
        if action is not None:
            _, size, _, basis, offset = action
            base = inputs[point][basis]
            if (
                base is None or base[0] != "stack"
                or base[1] + offset < -frame or base[1] + offset + size > 0
            ):
                raise StackEvidenceError("terminal scalar effect accesses foreign state or an escaped frame")
        elif width == 4 and word & 0x7f == 0x73:
            observed.append((point, word))
        elif not (width == 4 and word & 0x7f == 0x13 and word >> 12 & 7 in (3, 4)):
            require_console_instruction(width, word)
    if tuple(word for _, word in observed) != systems:
        raise StackEvidenceError("terminal effect has an unbound CSR/system instruction")
    return MachineEffects(tuple((call.offset, call.target_symbol) for call in runtime), (), tuple(observed))


def _terminal_execution(obj, symbol, arguments, returns):
    flow = function_flow(obj, symbol)
    runtime, _ = runtime_call_inventory(obj, symbol)
    calls = {call.offset: call for call in runtime}
    registers = [None] * 32
    registers[0], registers[2] = ("scalar", 0), ("stack", 0)
    for index, value in enumerate(arguments):
        registers[10 + index] = ("scalar", value)
    state = tuple(registers), ()
    high, low = pcrel_values(obj, symbol, flow)
    produced, visited, trace = {}, set(), []
    point = 0
    while True:
        if point in visited:
            raise StackEvidenceError("terminal scalar path contains an unbounded cycle")
        visited.add(point)
        width, word = flow.instructions[point]
        if point in calls:
            call = calls[point]
            trace.append((call.target_symbol, tuple(state[0][10:18])))
            if call.target_symbol in ("micros_panic_halt", "micros_panic_terminate_no_console", "panic_report_claimed"):
                return call.target_symbol, tuple(state[0][10:18]), tuple(trace)
            if call.target_symbol not in returns:
                raise StackEvidenceError("terminal scalar path consumed an unproved return value")
            value = returns[call.target_symbol]
            if value is not None:
                continuation = flow.continuations[point]
                last = max(address for address in flow.instructions if point <= address < continuation)
                produced[last] = ("scalar", value)
        if (width, word) == (4, 0x73):
            return "ecall", tuple(state[0][register] for register in (10, 11, 16, 17)), tuple(trace)
        changed, stored = value_transfer(obj, state, point, width, word, high, low, produced)
        changed = list(changed)
        value = _diagnostic_arithmetic(width, word, state[0])
        if value is not None:
            changed[_instruction_destination(word, width)] = value
        state = tuple(changed), stored
        if is_return(width, word):
            if changed[2] != ("stack", 0):
                raise StackEvidenceError("terminal scalar return does not restore its actual frame")
            return "return", changed[10], tuple(trace)
        branch = branch_offset(width, word)
        successors = flow.successors[point]
        if branch is not None and branch[1]:
            point = successors[0 if branch_taken(width, word, state[0]) else 1]
        elif len(successors) == 1:
            point = successors[0]
        else:
            raise StackEvidenceError("terminal scalar path has an unbound continuation")


def panic_runtime_effects(obj):
    symbols = function_symbols(obj)
    controls = tuple(symbol for symbol in obj.symbols if symbol.name == "panic_control" and symbol.symbol_type == 1)
    if len(controls) != 1 or controls[0].size != 4:
        raise StackEvidenceError("terminal runtime has no exact four-byte control object")
    control, = controls
    pointer = ("symbol", control.name, control.section_index, control.value)
    interrupt_calls = ("riscv_irq_save", "riscv_external_interrupt_disable")
    result = []
    for name in interrupt_calls:
        if name not in symbols:
            raise StackEvidenceError("terminal runtime lost an actual interrupt-disable provider")
        uart_machine_effects(obj, symbols[name])
    for name, core in (
        ("micros_panic_claim_first", "micros_panic_control_enter"),
        ("micros_panic_claim_reset", "micros_panic_control_request_reset"),
    ):
        symbol = symbols.get(name)
        if symbol is None:
            raise StackEvidenceError("terminal runtime omitted a claim owner")
        effects = _terminal_stack_effects(obj, symbol, (*interrupt_calls, core, "micros_panic_halt"))
        paths = []
        for returned in (0, 1, 2):
            outcome, value, trace = _terminal_execution(
                obj, symbol, (), {**dict.fromkeys(interrupt_calls), core: returned},
            )
            expected_calls = (*interrupt_calls, core) + (("micros_panic_halt",) if returned == 2 else ())
            if tuple(call for call, _ in trace) != expected_calls or trace[2][1][0] != pointer:
                raise StackEvidenceError("terminal claim bypasses interrupt disable or borrows another control object")
            if (outcome != "micros_panic_halt" if returned == 2 else (outcome, value) != ("return", ("scalar", int(returned == 0)))):
                raise StackEvidenceError("terminal claim changed first/reentry/invariant result handling")
            paths.append((returned, outcome, value, trace))
        result.append((name, effects, tuple(paths)))
    terminate = symbols.get("micros_panic_terminate_no_console")
    halt = symbols.get("micros_panic_halt")
    if terminate is None or halt is None:
        raise StackEvidenceError("terminal runtime omitted its no-console/halt boundary")
    effects = _terminal_stack_effects(obj, terminate, (*interrupt_calls, "sbi_system_reset", "micros_panic_halt"))
    outcome, _, trace = _terminal_execution(
        obj, terminate, (), {**dict.fromkeys(interrupt_calls), "sbi_system_reset": None},
    )
    if (
        outcome != "micros_panic_halt"
        or tuple(name for name, _ in trace) != (*interrupt_calls, "sbi_system_reset", "micros_panic_halt")
        or trace[2][1][:2] != (("scalar", 0), ("scalar", 1))
    ):
        raise StackEvidenceError("no-console termination changed its exact failure request or returned fallback")
    result.append((terminate.name, effects, trace))
    effects = _terminal_stack_effects(obj, halt, (), (0x10500073,))
    flow = function_flow(obj, halt)
    if any(is_return(*instruction) for instruction in flow.instructions.values()):
        raise StackEvidenceError("terminal halt has an actual return instruction")
    result.append((halt.name, effects, ()))
    return tuple(result)


def terminal_srst_effects(obj, symbol, *, test_observer=False):
    if symbol.name != "sbi_system_reset" or type(test_observer) is not bool:
        raise StackEvidenceError("terminal SRST proof has a foreign function or observation mode")
    observer = ("micros_terminal_test_reset",) if test_observer else ()
    effects = _terminal_stack_effects(
        obj, symbol, ("micros_panic_claim_reset", "micros_panic_halt", *observer), (0x73,),
    )
    paths = []
    for arguments in ((0, 1), (0, 0), (1, 1), (1, 0), (0, 0xffffffff), (0xffffffff, 1), (0xffffffff, 0xffffffff)):
        for claimed in (0, 1):
            outcome, values, trace = _terminal_execution(
                obj, symbol, arguments,
                {"micros_panic_claim_reset": claimed, **dict.fromkeys(observer)},
            )
            failure = arguments == (0, 1)
            if failure and not claimed:
                if outcome != "micros_panic_halt" or tuple(name for name, _ in trace) != (
                    "micros_panic_claim_reset", "micros_panic_halt",
                ):
                    raise StackEvidenceError("repeated failure SRST bypasses its claim or reaches firmware")
            else:
                expected = (("micros_panic_claim_reset",) if failure else ()) + observer
                if (
                    outcome != "ecall" or values != tuple(("scalar", value) for value in (*arguments, 0, 0x53525354))
                    or tuple(name for name, _ in trace) != expected
                    or observer and any(
                        value is None or value[0] != "scalar" or value[1] & 0xffffffff != argument
                        for value, argument in zip(trace[-1][1][:2], arguments)
                    )
                ):
                    raise StackEvidenceError("SRST tuple bypass, observer or successful-claim dominance is invalid")
            paths.append((arguments, claimed, outcome, values, trace))
    flow = function_flow(obj, symbol)
    ecall = effects.systems[0][0]
    returns = {point for point, instruction in flow.instructions.items() if is_return(*instruction)}
    if not returns or returns & reachable_instructions(flow, excluded=ecall + 4):
        raise StackEvidenceError("terminal SRST changed its actual returning firmware continuation")
    return MachineEffects(effects.calls, effects.writes, effects.systems, tuple(paths))


def terminal_reporter_effects(obj, symbol):
    if symbol.name != "micros_panic_report":
        raise StackEvidenceError("entry proof borrowed another reporter function")
    effects = _terminal_stack_effects(
        obj, symbol, ("micros_panic_claim_first", "micros_panic_terminate_no_console", "panic_report_claimed"),
    )
    paths = []
    for line in (12, 0xffffffff):
        arguments = (0, 0x81000000, 0x81000100, line, 0, 0x81000200)
        for first in (0, 1):
            outcome, values, trace = _terminal_execution(
                obj, symbol, arguments, {"micros_panic_claim_first": first},
            )
            target = "panic_report_claimed" if first else "micros_panic_terminate_no_console"
            if outcome != target or tuple(name for name, _ in trace) != ("micros_panic_claim_first", target):
                raise StackEvidenceError("first claim does not dominate the complete report body")
            if first:
                if any(values[index] != ("scalar", arguments[index]) for index in (0, 1, 2, 4, 5)) or (
                    values[3] is None or values[3][0] != "scalar" or values[3][1] & 0xffffffff != line
                ):
                    raise StackEvidenceError("report entry changes a captured argument before the first body")
            paths.append((arguments, first, outcome, values, trace))
    return MachineEffects(effects.calls, effects.writes, effects.systems, tuple(paths))


PRELUDE_CALLS = {
    ("kernel/platform/plic.c", "micros_plic_panic_disable"): ("micros_plic_disable_tty",),
    ("kernel/platform/plic.c", "micros_plic_disable_tty"): (
        "micros_plic_controller_disable_tty", "riscv_mmio_fence", "micros_plic_validate",
    ),
    ("kernel/platform/plic.c", "micros_plic_validate"): ("micros_plic_controller_validate",),
    ("kernel/platform/plic.c", "micros_plic_initialize"): (
        "plic_register", "plic_register", "plic_register", "plic_register",
        "micros_plic_controller_initialize", "riscv_mmio_fence", "micros_plic_validate",
    ),
    ("kernel/platform/plic.c", "plic_register"): (),
    ("kernel/platform/plic_core.c", "micros_plic_controller_disable_tty"): ("controller_shape_is_valid",),
    ("kernel/platform/plic_core.c", "micros_plic_controller_validate"): ("controller_shape_is_valid",),
    ("kernel/platform/plic_core.c", "controller_shape_is_valid"): ("registers_are_valid",),
    ("kernel/platform/plic_core.c", "registers_are_valid"): (),
    ("kernel/platform/plic_core.c", "micros_plic_controller_initialize"): ("registers_are_valid",),
    ("kernel/tty/tty_handoff_runtime.c", "micros_tty_handoff_runtime_panic"): (
        "tty_handoff_context", "micros_tty_handoff_panic",
    ),
    ("kernel/tty/tty_handoff_runtime_state.c", "tty_handoff_context"): (),
    ("kernel/tty/tty_handoff_core.c", "micros_tty_handoff_panic"): (),
}

TERMINAL_OBSERVER = "tests/qemu/terminal_failure_kernel.c"
WORK_COLLECTOR = "tests/work/work_counts.c"
OBSERVER_CALLS = {
    (TERMINAL_OBSERVER, "micros_terminal_test_lsr"): (
        "micros_terminal_test_error", "finish_capture", "micros_terminal_test_overrun",
    ),
    (TERMINAL_OBSERVER, "micros_terminal_test_thr"): ("micros_terminal_test_error",),
    (TERMINAL_OBSERVER, "micros_terminal_test_seizure"): ("micros_terminal_test_error",),
    (TERMINAL_OBSERVER, "micros_terminal_test_report"): ("micros_terminal_test_error",),
    (TERMINAL_OBSERVER, "micros_terminal_test_after_seize"): ("micros_panic_entry",),
    (TERMINAL_OBSERVER, "micros_terminal_test_reset"): ("micros_terminal_test_error", "finish_capture"),
    (TERMINAL_OBSERVER, "finish_capture"): ("micros_test_work_finish", "observe_interrupts", "micros_terminal_test_error"),
    (TERMINAL_OBSERVER, "observe_interrupts"): (),
    (TERMINAL_OBSERVER, "micros_terminal_test_error"): (),
    (TERMINAL_OBSERVER, "micros_terminal_test_overrun"): (),
    (WORK_COLLECTOR, "micros_test_work_record"): ("latch_fault",),
    (WORK_COLLECTOR, "latch_fault"): (),
    (WORK_COLLECTOR, "micros_test_work_finish"): ("capture_status", "publish_snapshot"),
    (WORK_COLLECTOR, "capture_status"): (),
    (WORK_COLLECTOR, "publish_snapshot"): (),
}


def _prelude_arithmetic(width, word, registers):
    if width == 4 and word & 0xfe00707f == 0x02000033:
        left, right = registers[word >> 15 & 31], registers[word >> 20 & 31]
        if left is not None and right is not None and left[0] == right[0] == "scalar":
            return "scalar", signed(left[1] * right[1], 64)
        return None
    if width == 2 and word & 3 == 2 and word >> 13 == 0:
        value = registers[word >> 7 & 31]
        shift = (word >> 12 & 1) << 5 | (word >> 2 & 31)
        if value is not None and value[0] == "scalar":
            return "scalar", signed(value[1] << shift, 64)
        return None
    if width == 4 and word & 0xfe00007f == 0x33 and word >> 12 & 7 in (4, 6, 7):
        operation = word >> 12 & 7
        left, right = registers[word >> 15 & 31], registers[word >> 20 & 31]
    elif width == 2 and word & 0xfc03 == 0x8c01:
        operation = (0, 4, 6, 7)[word >> 5 & 3]
        left, right = registers[8 + (word >> 7 & 7)], registers[8 + (word >> 2 & 7)]
    else:
        return _diagnostic_arithmetic(width, word, registers)
    if left is None or right is None or left[0] != "scalar" or right[0] != "scalar":
        return None
    value = (
        left[1] - right[1] if operation == 0 else left[1] ^ right[1] if operation == 4
        else left[1] | right[1] if operation == 6 else left[1] & right[1]
    )
    return "scalar", signed(value, 64)


def prelude_states(obj, symbol, sections, *, arguments=(), results=None, fields=None):
    flow = function_flow(obj, symbol)
    high, low = pcrel_values(obj, symbol, flow)
    for point, value in tuple(high.items()):
        target = value[2]
        if 0 < target[2] < len(obj.sections) and obj.sections[target[2]].flags & 1:
            if target[2] not in sections:
                raise StackEvidenceError("pre-seizure operand has no actual linked data section")
            high[point] = (*value[:2], ("scalar", sections[target[2]] + target[3]))
    registers = [None] * 32
    registers[0], registers[2] = ("scalar", 0), ("stack", 0)
    for index, value in enumerate(arguments):
        registers[10 + index] = ("scalar", value)
    produced = {}
    for point, value in (results or {}).items():
        continuation = flow.continuations.get(point)
        if continuation is None:
            raise StackEvidenceError("pre-seizure value has no actual call continuation")
        last = max(address for address in flow.instructions if point <= address < continuation)
        produced[last] = ("scalar", value)
    states = {0: (tuple(registers), ())}
    pending = deque((0,))
    while pending:
        point = pending.popleft()
        width, word = flow.instructions[point]
        changed, memory = value_transfer(obj, states[point], point, width, word, high, low, produced)
        changed = list(changed)
        action = memory_action(width, word)
        if action is not None and not action[0]:
            _, size, register, basis, offset = action
            base = high[low[point]][2] if point in low else states[point][0][basis]
            if base is not None and base[0] == "scalar" and (base[1] + offset, size) in (fields or {}):
                changed[register] = ("scalar", fields[base[1] + offset, size])
        value = _prelude_arithmetic(width, word, states[point][0])
        if value is not None:
            changed[_instruction_destination(word, width)] = value
        current = tuple(changed), memory
        successors = flow.successors[point]
        branch = branch_offset(width, word)
        if branch is not None and branch[1]:
            operands = (
                (current[0][8 + (word >> 7 & 7)], ("scalar", 0)) if width == 2
                else (current[0][word >> 15 & 31], current[0][word >> 20 & 31])
            )
            if all(value is not None and value[0] == "scalar" for value in operands):
                successors = (successors[0 if branch_taken(width, word, current[0]) else 1],)
        for successor in successors:
            old = states.get(successor)
            merged = current if old is None else merge_values(old, current)
            if old != merged:
                states[successor] = merged
                pending.append(successor)
    return states, high, low


def prelude_machine_effects(
    obj, symbol, unit, sections, *, arguments=(), results=None, fields=None, readable=(), writable=(),
):
    expected = PRELUDE_CALLS.get((unit, symbol.name))
    if expected is None:
        raise StackEvidenceError("pre-seizure effect has no fixed original source owner")
    frame = object_frames(obj, function_symbols(obj))[symbol.name]
    require_c_frame_movement(obj, symbol, frame)
    flow = function_flow(obj, symbol)
    runtime, _ = runtime_call_inventory(obj, symbol)
    if Counter(call.target_symbol for call in runtime) != Counter(expected):
        raise StackEvidenceError("pre-seizure effect changed its complete original call inventory")
    states, high, low = prelude_states(obj, symbol, sections, arguments=arguments, results=results, fields=fields)
    writes = []
    for point, (width, word) in flow.instructions.items():
        if point not in states:
            continue
        action = memory_action(width, word)
        if action is not None:
            write, size, register, basis, offset = action
            base = high[low[point]][2] if point in low else states[point][0][basis]
            if base is not None and base[0] == "stack":
                if base[1] + offset < -frame or base[1] + offset + size > 0:
                    raise StackEvidenceError("pre-seizure operand escaped its actual frame")
                continue
            if not write and base is not None and base[0] == "symbol":
                section = obj.sections[base[2]]
                if (
                    section.section_type == 1 and section.flags & 2 and not section.flags & 5
                    and 0 <= base[3] + offset <= section.size - size
                ):
                    continue
            regions = writable if write else readable
            if base is None or base[0] != "scalar" or not any(
                start <= base[1] + offset and base[1] + offset + size <= start + extent
                for start, extent in regions
            ):
                raise StackEvidenceError(
                    f"pre-seizure effect has an unbound/aliasing data operand: {unit}:{symbol.name}+{point:#x} "
                    f"{'write' if write else 'read'} {size} bytes at {base!r}+{offset}"
                )
            if write:
                writes.append((point, base[1] + offset, size, states[point][0][register]))
        elif width == 4 and word & 0x7f in (0x73, 0x0f):
            raise StackEvidenceError("pre-seizure state helper changes interrupt or firmware state")
        elif branch_offset(width, word) is None and not is_call(width, word) and not is_return(width, word):
            pure = (
                width == 4 and word & 0x7f in (0x13, 0x1b, 0x33, 0x3b, 0x17, 0x37)
                or width == 2 and (
                    word & 3 == 1 and word >> 13 in (0, 1, 2, 3, 4)
                    or word & 3 == 0 and word >> 13 == 0
                    or word & 3 == 2 and word >> 13 == 4 and word >> 2 & 31
                )
            )
            if not pure:
                raise StackEvidenceError("pre-seizure effect contains an unknown instruction")
    return MachineEffects(tuple((call.offset, call.target_symbol) for call in runtime), tuple(writes), ()), states


def _observer_storage(point, action, registers, high, low, frame, readable, writable):
    write, size, register, basis, offset = action
    base = high[low[point]][2] if point in low else registers[basis]
    if base is not None and base[0] == "stack":
        if base[1] + offset < -frame or base[1] + offset + size > 0:
            raise StackEvidenceError("terminal observer escaped its actual compiler frame")
        return None
    regions = writable if write else readable
    if base is None or base[0] != "scalar" or not any(
        start <= base[1] + offset and base[1] + offset + size <= start + extent for start, extent in regions
    ):
        raise StackEvidenceError(f"terminal observer has an unbound data effect at {point:#x}: {base!r}+{offset}")
    return point, base[1] + offset, size, registers[register] if write else None


def _snapshot_copy_effects(obj, symbol, sections, collector, snapshot):
    frame = object_frames(obj, function_symbols(obj))[symbol.name]
    require_c_frame_movement(obj, symbol, frame)
    flow = function_flow(obj, symbol)
    if flow.calls:
        raise StackEvidenceError("EV02 snapshot publication acquired a new helper/hook")
    _, high, low = prelude_states(obj, symbol, sections, arguments=(snapshot,))
    registers = [None] * 32
    registers[0], registers[2], registers[10] = ("scalar", 0), ("stack", 0), ("scalar", snapshot)
    state, point, visits, writes, count_sources = (tuple(registers), ()), 0, Counter(), [], []
    while True:
        visits[point] += 1
        if visits[point] > 27 * (14 + 1) + 2:
            raise StackEvidenceError("EV02 snapshot machine loop exceeds its fixed complete dimensions")
        width, word = flow.instructions[point]
        action = memory_action(width, word)
        if action is not None:
            effect = _observer_storage(
                point, action, state[0], high, low, frame, ((collector, 3072),), ((snapshot, 3056),),
            )
            if effect is not None and action[0]:
                writes.append(effect)
                if effect[1] >= snapshot + 32:
                    count_sources.append(effect[3])
        elif width == 4 and word & 0x7f in (0x73, 0x2f, 0x0f):
            raise StackEvidenceError("EV02 snapshot publication has an unbound system/atomic effect")
        changed, memory = value_transfer(obj, state, point, width, word, high, low, {})
        changed = list(changed)
        value = _prelude_arithmetic(width, word, state[0])
        if value is not None:
            changed[_instruction_destination(word, width)] = value
        state = tuple(changed), memory
        if is_return(width, word):
            break
        branch = branch_offset(width, word)
        successors = flow.successors[point]
        if branch is not None and branch[1]:
            point = successors[0 if branch_taken(width, word, state[0]) else 1]
        elif len(successors) == 1:
            point = successors[0]
        else:
            raise StackEvidenceError("EV02 snapshot publication has an unbound continuation")
    expected = (
        *((snapshot + offset, size) for offset, size in ((0, 4), (4, 4), (8, 8), (16, 8), (24, 4), (28, 4))),
        *((snapshot + 32 + index * 8, 8) for index in range(27 * 14)),
    )
    if tuple((address, size) for _, address, size, _ in writes) != expected or state[0][2] != ("stack", 0):
        raise StackEvidenceError("EV02 snapshot does not write exactly its complete bounded output")
    for index, value in enumerate(count_sources):
        expected_address = collector + 48 + index * 8
        if (
            value is None or value[0] != "field" or value[1][0] != "scalar" or value[3] != 8
            or value[1][1] + value[2] != expected_address
        ):
            raise StackEvidenceError("EV02 snapshot counter copy borrowed another source/index")
    return MachineEffects((), tuple(writes), (), (("complete-counter-copies", len(count_sources)),))


def terminal_observer_effects(obj, symbol, unit, sections, objects):
    if (unit, symbol.name) not in OBSERVER_CALLS:
        raise StackEvidenceError("terminal observation has an undeclared function owner")
    observation = objects["micros_terminal_test_observation"]
    collector = objects["collector"]
    snapshot = objects["micros_terminal_test_work_snapshot"]
    frame = object_frames(obj, function_symbols(obj))[symbol.name]
    require_c_frame_movement(obj, symbol, frame)
    flow = function_flow(obj, symbol)
    runtime, hooks = runtime_call_inventory(obj, symbol)
    if hooks or set(call.target_symbol for call in runtime) != set(OBSERVER_CALLS[unit, symbol.name]):
        raise StackEvidenceError("terminal observation changed its closed call set or instrumentation mode")
    if symbol.name == "publish_snapshot":
        return _snapshot_copy_effects(obj, symbol, sections, collector, snapshot)
    capture_base = 0x6100000000000000
    arguments = {
        "micros_terminal_test_report": (capture_base,),
        "micros_test_work_record": (1, 0, 1),
        "micros_test_work_finish": (snapshot,),
    }.get(symbol.name, ())
    readable = tuple((address, {
        "micros_terminal_test_observation": 104, "collector": 3072, "micros_terminal_test_work_snapshot": 3056,
        "faults_armed": 1, "recursion_injected": 1, "forced_reads": 8,
    }[name]) for name, address in objects.items()) + ((capture_base, 48),)
    writable = (
        tuple((address, size) for address, size in readable if address not in (collector, snapshot, capture_base))
        if unit == TERMINAL_OBSERVER else ((collector, 1),)
    )
    states, high, low = prelude_states(obj, symbol, sections, arguments=arguments)
    writes, systems = [], []
    for point, (width, word) in flow.instructions.items():
        if point not in states:
            continue
        action = memory_action(width, word)
        if action is not None:
            effect = _observer_storage(point, action, states[point][0], high, low, frame, readable, writable)
            if effect is not None and action[0]:
                writes.append(effect)
        elif width == 4 and word & 0x7f == 0x2f:
            operation, size = word >> 27, word >> 12 & 7
            address = states[point][0][word >> 15 & 31]
            expected = (
                (0, ("scalar", collector + 48 + 14 * 8)) if symbol.name == "micros_test_work_record"
                else (8, ("scalar", collector + 8)) if symbol.name == "latch_fault" else None
            )
            if expected is None or (operation, address) != expected or size != 3 or (
                symbol.name == "micros_test_work_record" and states[point][0][word >> 20 & 31] != ("scalar", 1)
            ):
                raise StackEvidenceError("EV02 atomic update has an unbound operation, target or work unit")
            writes.append((point, address[1], 8, ("atomic", operation)))
        elif width == 4 and word & 0x7f == 0x0f:
            if unit != WORK_COLLECTOR or word not in (0x0230000f, 0x0310000f):
                raise StackEvidenceError("terminal observation contains an unbound fence")
            systems.append((point, word))
        elif width == 4 and word & 0x7f == 0x73:
            read_csr = word >> 12 & 7 == 2 and word >> 15 & 31 == 0 and word >> 20 in (0x100, 0x104)
            wait = word == 0x10500073 and symbol.name in ("micros_terminal_test_error", "micros_terminal_test_overrun")
            if not (read_csr and symbol.name == "observe_interrupts") and not wait:
                raise StackEvidenceError("terminal observation changes supervisor state or firmware")
            systems.append((point, word))
        elif branch_offset(width, word) is None and not is_call(width, word) and not is_return(width, word):
            if width == 4 and word & 0x7f not in (0x13, 0x1b, 0x33, 0x3b, 0x17, 0x37):
                raise StackEvidenceError("terminal observation has an unknown machine instruction")
    return MachineEffects(tuple((call.offset, call.target_symbol) for call in runtime), tuple(writes), tuple(systems))


_COUNTER_INPUT = ("counter-below-maximum",)
_COUNTER_NEXT = ("counter-plus-one",)


def _diagnostic_arithmetic(width, word, registers):
    if width == 4 and word & 0x7f == 0x13:
        value = registers[word >> 15 & 31]
        operation = word >> 12 & 7
        immediate = signed(word >> 20, 12)
        if operation == 0 and value == _COUNTER_INPUT and immediate == 1:
            return _COUNTER_NEXT
        if value is not None and value[0] == "scalar":
            if operation == 3:
                mask = (1 << 64) - 1
                return ("scalar", int((value[1] & mask) < (immediate & mask)))
            if operation == 4:
                return ("scalar", signed(value[1] ^ immediate, 64))
    if width == 2 and word & 3 == 1 and word >> 13 == 0:
        value = registers[word >> 7 & 31]
        immediate = signed(((word >> 12 & 1) << 5) | (word >> 2 & 31), 6)
        if value == _COUNTER_INPUT and immediate == 1:
            return _COUNTER_NEXT
    if width == 4 and word & 0xfe00707f == 0x3033:
        left, right = registers[word >> 15 & 31], registers[word >> 20 & 31]
        if left is not None and right is not None and left[0] == right[0] == "scalar":
            mask = (1 << 64) - 1
            return ("scalar", int((left[1] & mask) < (right[1] & mask)))
    return None


def _diagnostic_branch(width, word, registers):
    from tools.stack_bound_console import branch_taken

    if width == 4:
        left, right = registers[word >> 15 & 31], registers[word >> 20 & 31]
        operation = word >> 12 & 7
        equal = operation == 0
    else:
        left, right = registers[8 + (word >> 7 & 7)], ("scalar", 0)
        operation = 0 if word >> 13 == 6 else 1
        equal = operation == 0
    for symbolic, scalar in ((left, right), (right, left)):
        if symbolic not in (_COUNTER_INPUT, _COUNTER_NEXT):
            continue
        if scalar is None or scalar[0] != "scalar" or operation not in (0, 1):
            raise StackEvidenceError("diagnostic counter has an unbound symbolic branch")
        number = scalar[1] & ((1 << 64) - 1)
        if (
            symbolic == _COUNTER_INPUT and number == (1 << 64) - 1
            or symbolic == _COUNTER_NEXT and number == 0
        ):
            return not equal
        raise StackEvidenceError("diagnostic counter tests an undeclared symbolic partition")
    return branch_taken(width, word, registers)


def _diagnostic_memory(obj, state, point, action, high, low, bound, frame, selected, stored_value):
    write, size, register, basis, offset = action
    lower = high.get(low[point]) if point in low else None
    base = lower[2] if lower is not None else state[0][basis]
    if base is not None and base[0] == "stack":
        position = base[1] + offset
        if position < -frame or position + size > 0:
            raise StackEvidenceError("diagnostic primitive escapes its actual automatic frame")
        return None
    if (
        base is None or base[0] != "symbol" or base[1] != bound.name
        or base[2] != bound.section_index
    ):
        raise StackEvidenceError("diagnostic primitive borrows a foreign memory object")
    position = base[3] + offset - bound.value
    expected = (8, 4) if selected == "selector" else (0, 8)
    if (position, size) != expected:
        raise StackEvidenceError("diagnostic primitive accesses an undeclared field")
    if write:
        if selected != "counter-increment" or state[0][register] != _COUNTER_NEXT:
            raise StackEvidenceError("diagnostic primitive has an unproved or overflowing write")
        return True, register, _COUNTER_NEXT, (point, size, position, _COUNTER_NEXT)
    value = ("scalar", 3) if selected == "selector" else stored_value
    return False, register, value, (point, size, position, value)


def _diagnostic_machine_case(obj, symbol, bound, selected):
    from tools.stack_bound_console import require_console_instruction

    symbols = function_symbols(obj)
    if symbols.get(symbol.name) != symbol or bound not in obj.symbols or bound.symbol_type != 1:
        raise StackEvidenceError("diagnostic machine proof has an unowned function or object")
    frame = object_frames(obj, symbols)[symbol.name]
    require_c_frame_movement(obj, symbol, frame)
    flow = function_flow(obj, symbol)
    runtime, _ = runtime_call_inventory(obj, symbol)
    if runtime:
        raise StackEvidenceError("diagnostic primitive has an unbound runtime callee")
    high, low = pcrel_values(obj, symbol, flow)
    registers = [None] * 32
    registers[0], registers[2] = ("scalar", 0), ("stack", 0)
    registers[1] = ("entry-return-address",)
    state = tuple(registers), ()
    stored_value = _COUNTER_INPUT if selected == "counter-increment" else ("scalar", -1)
    reads, writes, visited = [], [], set()
    point = 0
    while True:
        if point in visited:
            raise StackEvidenceError("diagnostic primitive has an unexplained execution cycle")
        visited.add(point)
        width, word = flow.instructions[point]
        if not (width == 4 and word & 0x7f == 0x13 and word >> 12 & 7 in (3, 4)):
            require_console_instruction(width, word)
        changed, memory = value_transfer(obj, state, point, width, word, high, low, {})
        changed = list(changed)
        action = memory_action(width, word)
        if action is not None:
            effect = _diagnostic_memory(
                obj, state, point, action, high, low, bound, frame, selected, stored_value,
            )
            if effect is not None:
                write, register, value, record = effect
                if write:
                    stored_value = value
                    writes.append(record)
                else:
                    changed[register] = value
                    reads.append(record)
        value = _diagnostic_arithmetic(width, word, state[0])
        if value is not None:
            changed[_instruction_destination(word, width)] = value
        changed[0] = ("scalar", 0)
        state = tuple(changed), memory
        if is_return(width, word):
            if changed[2] != ("stack", 0):
                raise StackEvidenceError("diagnostic primitive does not restore its actual stack")
            return tuple(reads), tuple(writes), changed[10]
        successors = flow.successors[point]
        branch = branch_offset(width, word)
        if branch is not None and branch[1]:
            point = successors[0 if _diagnostic_branch(width, word, state[0]) else 1]
        elif len(successors) == 1:
            point = successors[0]
        else:
            raise StackEvidenceError("diagnostic primitive has an unbound control transfer")


def diagnostic_selector_effects(obj, symbol, console):
    if symbol.name != "uart_panic_mode" or console.name != "uart_console_state" or console.size != 16:
        raise StackEvidenceError("PANIC selector has a foreign function or console layout")
    reads, writes, result = _diagnostic_machine_case(obj, symbol, console, "selector")
    if result != ("scalar", 1) or len(reads) != 1 or writes:
        raise StackEvidenceError("PANIC selector does not read the actual owner and return true")
    return MachineEffects((), (), (), (("ready-owner", reads),))


def diagnostic_counter_effects(obj, symbol, counter):
    if (
        symbol.name != "micros_diagnostic_test_observe_thr"
        or counter.name != "diagnostic_thr_writes" or counter.size != 8
    ):
        raise StackEvidenceError("THR observer has a foreign function or counter layout")
    saturated = _diagnostic_machine_case(obj, symbol, counter, "counter-saturated")
    incremented = _diagnostic_machine_case(obj, symbol, counter, "counter-increment")
    if (
        not saturated[0] or saturated[1] or not incremented[0] or len(incremented[1]) != 1
        or len(saturated[0]) > 2 or len(incremented[0]) > 2
    ):
        raise StackEvidenceError("THR observer does not perform exactly one saturating increment")
    return MachineEffects(
        (), incremented[1], (),
        (("maximum", saturated[0], saturated[1]), ("below-maximum", incremented[0], incremented[1])),
    )


DIAGNOSTIC_READONLY_CALLEES = {
    "micros_diagnostic_history_valid": (),
    "micros_diagnostic_panic_size": ("micros_diagnostic_history_valid",),
    "micros_diagnostic_panic_byte_prevalidated": (),
    "write_history_byte": ("uart_write",),
    "micros_diagnostic_panic_report": (
        "micros_diagnostic_panic_size",
        "uart_write", "uart_write", "uart_write", "uart_write", "uart_write", "uart_write",
        "uart_write_hex64", "uart_write_hex64",
        "micros_diagnostic_panic_byte_prevalidated", "write_history_byte",
    ),
}


def diagnostic_readonly_effects(obj, symbol):
    symbols = function_symbols(obj)
    if symbols.get(symbol.name) != symbol or symbol.name not in DIAGNOSTIC_READONLY_CALLEES:
        raise StackEvidenceError("unknown diagnostic history preservation function")
    frame = object_frames(obj, symbols)[symbol.name]
    require_c_frame_movement(obj, symbol, frame)
    runtime, _ = runtime_call_inventory(obj, symbol)
    if (
        Counter(call.target_symbol for call in runtime) != Counter(DIAGNOSTIC_READONLY_CALLEES[symbol.name])
        or any(call.kind is not EdgeKind.CALL for call in runtime)
    ):
        raise StackEvidenceError("diagnostic history has an unbound preservation call")
    flow, inputs = function_flow(obj, symbol), register_inputs(obj, symbol)
    for point, (width, word) in flow.instructions.items():
        if point not in inputs:
            raise StackEvidenceError("diagnostic history has an unowned machine entry")
        action = memory_action(width, word)
        if action is not None and action[0]:
            _, size, _, basis, offset = action
            base = inputs[point][basis]
            if (
                base is None or base[0] != "stack"
                or base[1] + offset < -frame or base[1] + offset + size > 0
            ):
                raise StackEvidenceError("diagnostic history writes outside its exact automatic frame")
        if (
            action is None and branch_offset(width, word) is None
            and not is_call(width, word) and not is_return(width, word)
        ):
            pure = (
                width == 4 and word & 0x7f in (0x13, 0x1b, 0x33, 0x3b, 0x17, 0x37)
                or width == 2 and (
                    word & 3 == 1 and word >> 13 in (0, 1, 2, 3, 4)
                    or word & 3 == 0 and word >> 13 == 0
                    or word & 3 == 2 and word >> 13 == 4 and word >> 2 & 31
                )
            )
            if not pure:
                raise StackEvidenceError("diagnostic history has an unbound system or machine effect")
    return MachineEffects(tuple((call.offset, call.target_symbol) for call in runtime), (), ())

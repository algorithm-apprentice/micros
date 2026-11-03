"""Closed UART/interrupt machine effects, not a complete ready qualifier."""

from collections import Counter
from dataclasses import dataclass

from tools.stack_bound_artifacts import object_frames
from tools.stack_bound_flow import function_flow, reachable_instructions
from tools.stack_bound_fatal_machine import require_srst_registers, result_zero_gate
from tools.stack_bound_machine import branch_offset, is_call, is_return, require_c_frame_movement, signed
from tools.stack_bound_objects import function_symbols
from tools.stack_bound_observer import runtime_call_inventory
from tools.stack_bound_pointer import memory_action
from tools.stack_bound_types import EdgeKind, StackEvidenceError
from tools.stack_bound_values import pcrel_values, register_inputs, value_transfer
from tools.user_elf_instructions import _instruction_destination


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


def uart_machine_effects(obj, symbol, *, test_counter=False):
    symbols = function_symbols(obj)
    if type(test_counter) is not bool or symbol.name not in CALLEES or symbols.get(symbol.name) != symbol:
        raise StackEvidenceError("unknown or borrowed UART effect function")
    frames = object_frames(obj, symbols)
    if symbol.name not in frames:
        raise StackEvidenceError("UART effects lack their actual compiler frame")
    require_c_frame_movement(obj, symbol, frames[symbol.name])
    flow = function_flow(obj, symbol)
    runtime_calls, hooks = runtime_call_inventory(obj, symbol)
    callees = CALLEES[symbol.name]
    if test_counter and symbol.name == "uart_write_character":
        callees = ("micros_diagnostic_test_observe_thr",)
    if (
        Counter(call.target_symbol for call in runtime_calls) != Counter(callees)
        or any(call.kind is not EdgeKind.CALL for call in runtime_calls)
    ):
        raise StackEvidenceError("UART effect call inventory is incomplete or unbound")
    inputs = register_inputs(obj, symbol)
    writes, systems, fences = [], [], []
    for point, (width, word) in flow.instructions.items():
        if point not in inputs:
            raise StackEvidenceError("UART effect has an unbound machine entry/instruction")
        action = memory_action(width, word)
        if action is not None and action[0]:
            _, size, register, basis, offset = action
            base, value = inputs[point][basis], inputs[point][register]
            if base is not None and base[0] == "stack":
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
    if symbol.name == "uart_panic_seize":
        expected = (
            (1, 3, ("scalar", 3)), (1, 1, ("scalar", 0)),
            (1, 3, ("scalar", 131)), (1, 0, ("scalar", 2)),
            (1, 1, ("scalar", 0)), (1, 3, ("scalar", 3)),
        )
        if tuple(record[1:] for record in writes) != expected:
            raise StackEvidenceError("UART seizure changed its exact MMIO transition")
    elif symbol.name == "uart_write_character":
        if len(writes) != 1 or writes[0][1:3] != (1, 0):
            raise StackEvidenceError("UART character writer lost its exact THR-only effect")
        if test_counter:
            observation = runtime_calls[0]
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
    elif writes:
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
        names = ("riscv_irq_save", "riscv_external_interrupt_disable", "riscv_mmio_fence")
        if any(name not in symbols for name in names):
            raise StackEvidenceError("UART seizure lacks a complete interrupt/fence provider")
        dependencies = tuple((name, uart_machine_effects(obj, symbols[name])) for name in names)
        for call in flow.calls:
            require_effect_return_dominance(flow, flow.continuations[call.offset])
        for point, *_ in writes:
            require_effect_return_dominance(flow, point + flow.instructions[point][0])
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

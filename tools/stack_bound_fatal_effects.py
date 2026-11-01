"""Closed UART/interrupt machine effects, not a complete ready qualifier."""

from collections import Counter
from dataclasses import dataclass

from tools.stack_bound_artifacts import object_frames
from tools.stack_bound_flow import function_flow, reachable_instructions
from tools.stack_bound_fatal_machine import require_srst_registers
from tools.stack_bound_machine import branch_offset, is_call, is_return, require_c_frame_movement
from tools.stack_bound_objects import function_symbols
from tools.stack_bound_observer import runtime_call_inventory
from tools.stack_bound_pointer import memory_action
from tools.stack_bound_types import EdgeKind, StackEvidenceError
from tools.stack_bound_values import register_inputs


UART_BASE = 0x10000000
CALLEES = {
    "uart_flush": (),
    "uart_write_character": (),
    "uart_write_hex64": ("uart_write", "uart_write_character"),
    "uart_write": (
        "micros_uart_console_output_allowed", "micros_panic_entry",
        "uart_write_character", "uart_write_character",
    ),
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


def uart_machine_effects(obj, symbol):
    symbols = function_symbols(obj)
    if symbol.name not in CALLEES or symbols.get(symbol.name) != symbol:
        raise StackEvidenceError("unknown or borrowed UART effect function")
    frames = object_frames(obj, symbols)
    if symbol.name not in frames:
        raise StackEvidenceError("UART effects lack their actual compiler frame")
    require_c_frame_movement(obj, symbol, frames[symbol.name])
    flow = function_flow(obj, symbol)
    runtime_calls, hooks = runtime_call_inventory(obj, symbol)
    if (
        Counter(call.target_symbol for call in runtime_calls) != Counter(CALLEES[symbol.name])
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

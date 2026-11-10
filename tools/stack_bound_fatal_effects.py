"""Closed UART/interrupt machine effects, not a complete ready qualifier."""

from collections import Counter, deque
from dataclasses import dataclass
from functools import lru_cache

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


TRACE_CORE_CALLS = {
    "trace_extent": (),
    "trace_separate": (),
    "trace_layout": ("trace_extent",) * 4 + ("trace_separate",) * 6,
    "trace_contains": (),
    "trace_select": ("trace_contains",),
    "trace_hart": (),
    "micros_panic_trace_owner_select": ("trace_layout", "trace_hart") + ("trace_contains",) * 4 + ("trace_select",) * 4,
    "trace_frame": (),
    "micros_panic_trace_cursor_open": ("trace_contains", "trace_separate", "trace_frame"),
    "trace_advance": ("trace_frame",),
    "micros_panic_trace_cursor_plan": ("trace_advance", "trace_frame"),
    "micros_panic_trace_cursor_consume": (),
}
TRACE_CORE_ARGUMENTS = {
    "trace_layout": ((0, 168, False),),
    "trace_select": ((5, 4, True), (6, 24, True)),
    "trace_hart": ((0, 168, False),),
    "micros_panic_trace_owner_select": ((0, 168, False), (1, 4, True), (2, 24, True)),
    "micros_panic_trace_cursor_open": ((1, 24, False), (6, 56, True)),
    "trace_advance": ((0, 56, True),),
    "micros_panic_trace_cursor_plan": ((0, 56, True), (1, 8, True), (2, 8, True)),
    "micros_panic_trace_cursor_consume": ((0, 56, True),),
}


def _trace_affine(width, word, registers):
    if width == 4 and word & 0x707f == 0x13:
        base, offset = registers[word >> 15 & 31], signed(word >> 20, 12)
    elif width == 2 and word & 3 == 1 and word >> 13 == 0:
        base = registers[word >> 7 & 31]
        offset = signed((word >> 12 & 1) << 5 | (word >> 2 & 31), 6)
    else:
        return None
    if base is None or base[0] not in ("argument", "field", "offset"):
        return None
    if base[0] == "offset":
        base, offset = base[1], base[2] + offset
    return base if offset == 0 else ("offset", base, offset)


def _trace_handle_transfer(state, changed, memory, width, word):
    registers, stored = state
    changed, memory = list(changed), dict(memory)
    full, low, high, placed = (("trace-boot-handle",), ("trace-handle-low",),
                               ("trace-handle-high",), ("trace-handle-high-placed",))
    action = memory_action(width, word)
    if action is not None:
        write, size, register, basis, offset = action
        base = registers[basis]
        if base is not None and base[0] == "stack" and size == 4:
            location = base[1] + offset
            value = registers[register]
            if write and value in (full, low, high):
                memory[location] = 4, low if value == full else value
            elif not write and width == 4 and word >> 12 & 7 == 6:
                for address, (extent, item) in stored:
                    if address <= location and location + 4 <= address + extent:
                        if extent == 8 and item == full and location - address in (0, 4):
                            changed[register] = low if location == address else high
                        elif extent == 4 and item in (low, high):
                            changed[register] = item
    left = right = None
    if width == 2 and word & 3 == 2 and word >> 13 == 0:
        register = word >> 7 & 31
        if ((word >> 12 & 1) << 5 | (word >> 2 & 31)) == 32 and registers[register] == high:
            changed[register] = placed
    elif width == 2 and word & 0xec03 == 0x8001:
        register = 8 + (word >> 7 & 7)
        if ((word >> 12 & 1) << 5 | (word >> 2 & 31)) == 32 and registers[register] == full:
            changed[register] = high
    elif width == 4 and word & 0xfc00707f in (0x1013, 0x5013) and word >> 20 & 63 == 32:
        register, value = word >> 7 & 31, registers[word >> 15 & 31]
        if word >> 12 & 7 == 1 and value == high:
            changed[register] = placed
        elif word >> 12 & 7 == 5 and value == full:
            changed[register] = high
    if width == 2 and word & 0xfc63 == 0x8c41:
        left, right = registers[8 + (word >> 7 & 7)], registers[8 + (word >> 2 & 7)]
    elif width == 4 and word & 0xfe00707f == 0x6033:
        left, right = registers[word >> 15 & 31], registers[word >> 20 & 31]
    if (left, right) in ((low, placed), (placed, low)):
        changed[_instruction_destination(word, width)] = full
    return tuple(changed), tuple(sorted(memory.items()))


def _trace_zero_result(width, word, registers):
    if width == 4 and word & 0x707f in (0x1b, 0x3013):
        source = registers[word >> 15 & 31]
        if source is not None and source[0] == "result" and source[2] == 32:
            if word & 0x707f == 0x1b and word >> 20 == 0:
                return source
            if word & 0x707f == 0x3013 and word >> 20 == 1:
                return "result-is-zero", source[1]
    if width == 2 and word & 0xf07f == 0x2001:
        source = registers[word >> 7 & 31]
        if source is not None and source[0] == "result" and source[2] == 32:
            return source
    return None


def _trace_csr_difference(width, word, registers):
    if width == 4 and word & 0xfe00707f == 0x4033:
        left, right = registers[word >> 15 & 31], registers[word >> 20 & 31]
    elif width == 2 and word & 0xfc63 == 0x8c21:
        left, right = registers[8 + (word >> 7 & 7)], registers[8 + (word >> 2 & 7)]
    else:
        return None
    for source, expected in ((left, right), (right, left)):
        if source in (("csr", 0x141), ("csr", 0x142), ("csr", 0x143)):
            return "trace-csr-difference", source, expected
    return None


def _trace_fault_stack_word(state, width, word):
    action = memory_action(width, word)
    if action is None:
        return
    _, size, _, basis, offset = action
    base = state[0][basis]
    if base is None or base[0] != "stack":
        return
    address = base[1] + offset
    for location, (extent, value) in state[1]:
        if (
            (value[0] == "trace-csr-difference" or value[0] == "csr" and value[1] in (0x141, 0x142, 0x143))
            and max(address, location) < min(address + size, location + extent)
            and (address, size) != (location, 8)
        ):
            raise StackEvidenceError("trace fault witness truncates or partially overwrites an actual 64-bit CSR word")


def trace_register_inputs(obj, symbol, *, results=None, outputs=None):
    flow = function_flow(obj, symbol)
    high, low = pcrel_values(obj, symbol, flow)
    calls = {call.offset: call for call in flow.calls}
    if not set(results or {}) <= set(calls) or not set(outputs or {}) <= set(calls):
        raise StackEvidenceError("trace operand summary borrowed another call site")
    produced, stores = {}, {}
    frame = object_frames(obj, function_symbols(obj))[symbol.name]
    for point in set(results or {}) | set(outputs or {}):
        last = max(address for address in flow.instructions if point <= address < flow.continuations[point])
        if point in (results or {}):
            produced[last] = results[point]
        if point in (outputs or {}):
            stores[last] = outputs[point]
    registers = [None] * 32
    registers[0], registers[2] = ("scalar", 0), ("stack", 0)
    registers[1] = ("entry-return-address",)
    registers[8] = ("entry-frame-pointer",)
    for argument in range(8):
        registers[10 + argument] = ("argument", argument, ())
    states = {0: (tuple(registers), ())}
    pending = deque((0,))
    while pending:
        point = pending.popleft()
        width, word = flow.instructions[point]
        _trace_fault_stack_word(states[point], width, word)
        changed, memory = value_transfer(obj, states[point], point, width, word, high, low, produced)
        changed, memory = _trace_handle_transfer(states[point], changed, memory, width, word)
        if point in stores:
            memory = dict(memory)
            for argument, fields in stores[point]:
                base = states[point][0][10 + argument]
                if base is None or base[0] != "stack":
                    raise StackEvidenceError("trace callee output is not an actual caller-owned local cell")
                for offset, size, value in fields:
                    location = base[1] + offset
                    if size not in (1, 2, 4, 8) or location < -frame or location + size > -16:
                        raise StackEvidenceError("trace callee output escaped its actual caller frame")
                    memory = {address: item for address, item in memory.items()
                              if address + item[0] <= location or location + size <= address}
                    memory[location] = size, value
            memory = tuple(sorted(memory.items()))
        affine = _trace_affine(width, word, states[point][0])
        comparison = _trace_zero_result(width, word, states[point][0])
        difference = _trace_csr_difference(width, word, states[point][0])
        csr_read = width == 4 and word & 0x7f == 0x73 and word >> 12 & 7 == 2 and word >> 15 & 31 == 0
        if (affine is not None and point not in low) or comparison is not None or difference is not None or csr_read:
            changed = list(changed)
            changed[_instruction_destination(word, width)] = (
                ("csr", word >> 20) if csr_read else difference if difference is not None
                else comparison if comparison is not None else affine
            )
            changed[0] = ("scalar", 0)
            changed = tuple(changed)
        current = changed, memory
        for successor in flow.successors[point]:
            old = states.get(successor)
            merged = current if old is None else merge_values(old, current)
            if old != merged:
                states[successor] = merged
                pending.append(successor)
    return {point: state[0] for point, state in states.items()}


def trace_frame_record_effects(obj, symbol):
    frame = object_frames(obj, function_symbols(obj))[symbol.name]
    if frame < 16 or frame % 16:
        raise StackEvidenceError("traceable C function lacks one complete aligned psABI record")
    require_c_frame_movement(obj, symbol, frame)
    flow, inputs = function_flow(obj, symbol), trace_register_inputs(obj, symbol)
    records, cfa = [], []
    for point, (width, word) in flow.instructions.items():
        action = memory_action(width, word)
        if action is not None and action[0]:
            _, size, register, basis, offset = action
            base = inputs[point][basis]
            if base is not None and base[0] == "stack":
                location = base[1] + offset
                if max(location, -16) < min(location + size, 0):
                    records.append((point, location, size, inputs[point][register]))
        if (
            _instruction_destination(word, width) == 8 and point + width in inputs
            and inputs[point][8] != ("stack", 0) and inputs[point + width][8] == ("stack", 0)
        ):
            cfa.append((point, point + width))
    expected = ((-16, 8, ("entry-frame-pointer",)), (-8, 8, ("entry-return-address",)))
    if len(records) != 2 or tuple(sorted(row[1:] for row in records)) != expected or len(cfa) != 1:
        raise StackEvidenceError("traceable C prologue lost or overwrote its exact saved PC/frame-pointer record")
    point, continuation = cfa[0]
    if any(store >= point for store, *_ in records):
        raise StackEvidenceError("traceable C function installs its CFA before preserving the inherited frame register")
    returns = {offset for offset, instruction in flow.instructions.items() if is_return(*instruction)}
    if returns & reachable_instructions(flow, excluded=continuation):
        raise StackEvidenceError("traceable C return bypasses its actual frame-record prologue")
    for call in flow.calls:
        if inputs[call.offset][8] != ("stack", 0) or call.offset in reachable_instructions(flow, excluded=continuation):
            raise StackEvidenceError("traceable C call lost its actual entry-SP frame register")
    return "psabi-frame-record", frame, tuple(records), cfa[0]


def _trace_plan_effects(obj, symbol, inputs, writes):
    flow = function_flow(obj, symbol)
    predicate, = (call for call in flow.calls if call.target_symbol == "trace_frame")
    if inputs[predicate.offset][10:13] != tuple(
        ("field", ("argument", 0, ()), offset, 8) for offset in (0, 8, 32)
    ):
        raise StackEvidenceError("trace plan did not admit its actual lower/root/current-frame values")
    gate = result_zero_gate(obj, symbol, predicate.offset, bits=1)
    addresses = tuple(row for row in writes if row[1] in (1, 2))
    expected = tuple(
        (argument, 0, 8, ("offset", ("field", ("argument", 0, ()), 32, 8), displacement))
        for argument, displacement in ((1, -8), (2, -16))
    )
    if tuple(row[1:] for row in addresses) != expected:
        raise StackEvidenceError("trace plan did not publish exactly PC=frame-8 then link=frame-16")
    for point, *_ in addresses:
        if (
            point in reachable_instructions(flow, start=gate.zero)
            or point in reachable_instructions(flow, excluded=gate.branch)
            or point not in reachable_instructions(flow, start=gate.nonzero)
        ):
            raise StackEvidenceError("trace address output bypasses its actual successful frame admission")
    return ("admitted-word-addresses", predicate.offset, gate.branch, addresses)


def _trace_core_function(obj, symbol, *, counted):
    frame = object_frames(obj, function_symbols(obj))[symbol.name]
    require_c_frame_movement(obj, symbol, frame)
    flow = function_flow(obj, symbol)
    calls, _ = runtime_call_inventory(obj, symbol)
    expected = TRACE_CORE_CALLS[symbol.name] + (
        ("micros_test_work_record",) if counted and symbol.name == "trace_advance" else ()
    )
    if Counter(call.target_symbol for call in calls) != Counter(expected) or any(
        call.kind is not EdgeKind.CALL for call in calls
    ):
        raise StackEvidenceError("trace core has an undeclared call, copy/fill, tail transfer or work site")
    inputs = trace_register_inputs(obj, symbol)
    arguments = {number: (size, writable) for number, size, writable in TRACE_CORE_ARGUMENTS.get(symbol.name, ())}
    writes = []
    for point, (width, word) in flow.instructions.items():
        if point not in inputs:
            raise StackEvidenceError("trace core has an unowned instruction entry")
        action = memory_action(width, word)
        if action is not None:
            write, size, register, basis, offset = action
            base = inputs[point][basis]
            if base is not None and base[0] == "stack":
                if base[1] + offset < -frame or base[1] + offset + size > 0:
                    raise StackEvidenceError("trace core escaped its actual compiler frame")
                continue
            if (
                base is None or base[0] != "argument" or base[2] != () or base[1] not in arguments
                or offset < 0 or offset + size > arguments[base[1]][0]
                or write and not arguments[base[1]][1]
            ):
                raise StackEvidenceError("trace core dereferences an address value or escapes its exact typed input/output")
            if write:
                writes.append((point, base[1], offset, size, inputs[point][register]))
        elif branch_offset(width, word) is None and not is_call(width, word) and not is_return(width, word):
            pure = (
                width == 4 and word & 0x7f in (0x13, 0x1b, 0x33, 0x3b, 0x17, 0x37)
                or width == 2 and (
                    word & 3 == 1 and word >> 13 in (0, 1, 2, 3, 4)
                    or word & 3 == 0 and word >> 13 == 0
                    or word & 3 == 2 and word >> 13 == 0 and word >> 7 & 31
                    or word & 3 == 2 and word >> 13 == 4 and word >> 2 & 31
                )
            )
            if not pure:
                raise StackEvidenceError(
                    f"trace core has an unbound machine effect: {symbol.name}+{point:#x} width={width} word={word:#x}"
                )
    if symbol.name == "trace_advance":
        advances = tuple(row for row in writes if row[1:4] == (0, 32, 8))
        if len(advances) != 1 or advances[0][4] != ("field", ("argument", 0, ()), 40, 8):
            raise StackEvidenceError("trace advance does not copy exactly the admitted predecessor into its current frame")
        if any(row[1:4] not in ((0, 32, 8), (0, 52, 4)) for row in writes):
            raise StackEvidenceError("trace advance changed its immutable bounds or another cursor field")
        if counted:
            observation, = (call for call in calls if call.target_symbol == "micros_test_work_record")
            if inputs[observation.offset][10:13] != tuple(("scalar", value) for value in (1, 3, 1)):
                raise StackEvidenceError("trace advance borrowed another owner, primitive or work amount")
            store = advances[0][0]
            if (
                observation.offset <= store
                or observation.offset in reachable_instructions(flow, excluded=store + flow.instructions[store][0])
                or store in reachable_instructions(flow, start=flow.continuations[observation.offset])
            ):
                raise StackEvidenceError("trace link observation does not follow exactly its actual advance")
    relations = (trace_frame_record_effects(obj, symbol),)
    if symbol.name == "micros_panic_trace_cursor_plan":
        relations += (_trace_plan_effects(obj, symbol, inputs, writes),)
    return MachineEffects(tuple((call.offset, call.target_symbol) for call in calls), tuple(writes), (), relations)


def trace_core_effects(obj, *, counted=False):
    if type(counted) is not bool:
        raise StackEvidenceError("trace core has no exact work-observation mode")
    symbols = function_symbols(obj)
    if set(symbols) != set(TRACE_CORE_CALLS) or any(
        section.flags & 3 == 3 and section.size for section in obj.sections
    ):
        raise StackEvidenceError("trace core changed its closed function set or introduced mutable static storage")
    return {name: _trace_core_function(obj, symbol, counted=counted) for name, symbol in symbols.items()}


TRACE_TARGET_CALLS = {
    "trace_view_initialize": (),
    "trace_resolve_owners": (
        "micros_kernel_object_runtime_registry", "micros_kernel_object_runtime_boot_hart_handle",
        "micros_hart_resolve", "micros_thread_resolve", "micros_process_resolve",
    ),
    "trace_owner_name": (),
    "trace_stop_name": (),
    "trace_emit_frame": ("uart_write",) * 4 + ("uart_write_hex64",) * 3,
    "micros_panic_trace_report": (
        "micros_panic_trace_frame_pointer", "trace_view_initialize", "trace_resolve_owners",
        "micros_panic_trace_owner_select", "micros_panic_trace_cursor_open", "trace_owner_name",
        "micros_panic_trace_cursor_plan", "micros_panic_trace_load_word", "micros_panic_trace_load_word",
        "micros_panic_trace_cursor_consume", "trace_emit_frame", "trace_stop_name",
    ) + ("uart_write",) * 7 + ("uart_write_hex64",),
}
TRACE_OBJECT_READS = {
    "registry": frozenset(((16, 8), (40, 8))),
    "hart": frozenset(((0, 8), (8, 8), (16, 8), (24, 8), (72, 1), (80, 8), (88, 8), (96, 2), (96, 8))),
    "thread": frozenset(((8, 8), (16, 1), (24, 8), (32, 8))),
    "process": frozenset(),
}


def _trace_call_summaries(flow):
    results, outputs = {}, {}
    for call in flow.calls:
        target, point = call.target_symbol, call.offset
        if target == "micros_kernel_object_runtime_registry":
            results[point] = ("trace-object", "registry")
        elif target == "micros_kernel_object_runtime_boot_hart_handle":
            results[point] = ("trace-boot-handle",)
        elif target in ("micros_hart_resolve", "micros_thread_resolve", "micros_process_resolve"):
            role = {"micros_hart_resolve": "hart", "micros_thread_resolve": "thread", "micros_process_resolve": "process"}[target]
            outputs[point] = ((2, ((0, 8, ("trace-object", role)),)),)
            results[point] = ("result", point, 32)
        elif target == "micros_panic_trace_frame_pointer":
            results[point] = ("trace-frame-pointer",)
        elif target == "micros_panic_trace_owner_select":
            outputs[point] = (
                (1, ((0, 4, ("result", (point, 1), 32)),)),
                (2, tuple((offset, 8, ("trace-bound", offset)) for offset in (0, 8, 16))),
            )
        elif target == "micros_panic_trace_cursor_open":
            outputs[point] = ((6, (
                (32, 8, ("trace-cursor-frame",)),
                (48, 4, ("result", ("cursor-field", 48), 32)), (52, 4, ("result", ("cursor-field", 52), 32)),
            )),)
        elif target == "micros_panic_trace_cursor_plan":
            outputs[point] = (
                (0, ((32, 8, ("trace-cursor-frame",)), (52, 4, ("result", ("cursor-field", 52), 32)))),
                (1, ((0, 8, ("trace-word-address", 0)),)), (2, ((0, 8, ("trace-word-address", 1)),)),
            )
            results[point] = ("result", point, 1)
        elif target == "micros_panic_trace_load_word":
            results[point] = ("trace-word-value", point)
        elif target == "micros_panic_trace_cursor_consume":
            outputs[point] = ((0, (
                (48, 4, ("result", ("cursor-field", 48), 32)), (52, 4, ("result", ("cursor-field", 52), 32)),
            )),)
            results[point] = ("result", point, 1)
    return results, outputs


def _trace_target_function(obj, symbol, *, trace_test):
    frame = object_frames(obj, function_symbols(obj))[symbol.name]
    require_c_frame_movement(obj, symbol, frame)
    flow = function_flow(obj, symbol)
    calls, _ = runtime_call_inventory(obj, symbol)
    expected = TRACE_TARGET_CALLS[symbol.name]
    if trace_test and symbol.name == "micros_panic_trace_report":
        expected += (
            "micros_panic_trace_test_enter", "micros_panic_trace_test_owner",
            "micros_panic_trace_test_header", "micros_panic_trace_test_started",
            "micros_panic_trace_test_link", "micros_panic_trace_test_before_word",
            "micros_panic_trace_test_word", "micros_panic_trace_test_word", "micros_panic_trace_test_complete",
            "micros_test_work_record", "micros_test_work_record",
        )
    if Counter(call.target_symbol for call in calls) != Counter(expected) or any(
        call.kind is not EdgeKind.CALL for call in calls
    ):
        raise StackEvidenceError("trace target has an undeclared call, full validator, copy/fill or tail transfer")
    results, outputs = _trace_call_summaries(flow)
    inputs = trace_register_inputs(obj, symbol, results=results, outputs=outputs)
    writes, systems = [], []
    for point, (width, word) in flow.instructions.items():
        if point not in inputs:
            raise StackEvidenceError("trace target has an unowned instruction entry")
        action = memory_action(width, word)
        if action is not None:
            write, size, register, basis, offset = action
            base = inputs[point][basis]
            if base is not None and base[0] == "stack":
                if base[1] + offset < -frame or base[1] + offset + size > 0:
                    raise StackEvidenceError("trace target escaped its actual compiler frame")
                continue
            if base == ("argument", 0, ()) and symbol.name in ("trace_view_initialize", "trace_resolve_owners"):
                if offset < 0 or offset + size > 168:
                    raise StackEvidenceError("trace target escaped its exact caller-owned view")
            elif not write and base is not None and base[0] == "trace-object":
                if (offset, size) not in TRACE_OBJECT_READS[base[1]]:
                    raise StackEvidenceError("trace target reads outside the selected immutable registry/hart/thread fields")
            else:
                raise StackEvidenceError(
                    f"trace target has an unbound memory operand: {symbol.name}+{point:#x} "
                    f"{'write' if write else 'read'} {size} at {base!r}+{offset}"
                )
            if write:
                writes.append((point, offset, size, inputs[point][register]))
        elif width == 4 and word & 0x7f == 0x73:
            if symbol.name != "micros_panic_trace_report" or word >> 20 != 0x140 or word >> 12 & 7 != 2 or word >> 15 & 31:
                raise StackEvidenceError("trace target changes privileged state or reads an undeclared CSR")
            systems.append((point, word))
        elif branch_offset(width, word) is None and not is_call(width, word) and not is_return(width, word):
            pure = (
                width == 4 and word & 0x7f in (0x13, 0x1b, 0x33, 0x3b, 0x17, 0x37)
                or width == 2 and (
                    word & 3 == 1 and word >> 13 in (0, 1, 2, 3, 4)
                    or word & 3 == 0 and word >> 13 == 0
                    or word & 3 == 2 and word >> 13 == 0 and word >> 7 & 31
                    or word & 3 == 2 and word >> 13 == 4 and word >> 2 & 31
                )
            )
            if not pure:
                raise StackEvidenceError("trace target has an unknown fence, atomic or machine effect")
    if len(systems) != int(symbol.name == "micros_panic_trace_report"):
        raise StackEvidenceError("trace target lost its unique actual sscratch read")
    return MachineEffects(tuple((call.offset, call.target_symbol) for call in calls), tuple(writes), tuple(systems)), inputs


def _trace_report_custody(obj, symbol, inputs):
    flow = function_flow(obj, symbol)
    frame = object_frames(obj, function_symbols(obj))[symbol.name]

    def call(name):
        selected = tuple(item for item in flow.calls if item.target_symbol == name)
        if len(selected) != 1:
            raise StackEvidenceError("trace report lacks its exact unique helper: " + name)
        return selected[0]

    initialize, resolve = call("trace_view_initialize"), call("trace_resolve_owners")
    owner, opened = call("micros_panic_trace_owner_select"), call("micros_panic_trace_cursor_open")
    plan, consume, emit = call("micros_panic_trace_cursor_plan"), call("micros_panic_trace_cursor_consume"), call("trace_emit_frame")
    view = inputs[initialize.offset][10]
    owner_cell, bounds = inputs[owner.offset][11:13]
    cursor = inputs[opened.offset][16]
    pc_cell, link_cell = inputs[plan.offset][11:13]
    cells = ((view, 168), (owner_cell, 4), (bounds, 24), (cursor, 56), (pc_cell, 8), (link_cell, 8))
    for index, (cell, size) in enumerate(cells):
        if cell is None or cell[0] != "stack" or cell[1] < -frame or cell[1] + size > -16:
            raise StackEvidenceError("trace report passed a foreign or incomplete local object")
        if any(other is not None and other[0] == "stack" and max(cell[1], other[1]) < min(cell[1] + size, other[1] + extent)
               for other, extent in cells[index + 1:]):
            raise StackEvidenceError("trace report aliased distinct live owner/cursor/output objects")
    if (
        inputs[initialize.offset][11:13] != (("stack", -frame), ("csr", 0x140))
        or inputs[resolve.offset][10] != view or inputs[owner.offset][10] != view
        or inputs[opened.offset][10:16] != (
            ("result", (owner.offset, 1), 32), bounds, ("stack", -frame), ("trace-frame-pointer",),
            ("symbol", "__kernel_text_start", 0, 0), ("symbol", "__kernel_text_end", 0, 0),
        )
        or inputs[plan.offset][10] != cursor or inputs[consume.offset][10] != cursor
    ):
        raise StackEvidenceError("trace report borrowed its SP/FP, CSR, linker text or authenticated local owner")
    words = tuple(item for item in flow.calls if item.target_symbol == "micros_panic_trace_load_word")
    if len(words) != 2 or tuple(inputs[item.offset][10] for item in words) != (
        ("trace-word-address", 0), ("trace-word-address", 1),
    ):
        raise StackEvidenceError("trace report does not load its actual planned PC word before its link word")
    gate = result_zero_gate(obj, symbol, plan.offset, bits=1)
    for item in words:
        if (
            item.offset in reachable_instructions(flow, start=gate.zero)
            or item.offset in reachable_instructions(flow, excluded=gate.branch)
            or item.offset not in reachable_instructions(flow, start=gate.nonzero)
        ):
            raise StackEvidenceError("trace word load bypasses successful actual cursor admission")
    if words[1].offset in reachable_instructions(flow, excluded=flow.continuations[words[0].offset]):
        raise StackEvidenceError("trace second word bypasses the first load's normal return")
    if inputs[consume.offset][11:13] != tuple(("trace-word-value", item.offset) for item in words):
        raise StackEvidenceError("trace cursor does not consume both actual completed word values")
    accepted = result_zero_gate(obj, symbol, consume.offset, bits=1)
    if (
        emit.offset in reachable_instructions(flow, start=accepted.zero, excluded=plan.offset)
        or emit.offset in reachable_instructions(flow, excluded=accepted.branch)
        or plan.offset in reachable_instructions(flow, start=accepted.nonzero, excluded=flow.continuations[emit.offset])
        or inputs[emit.offset][11:13] != (("trace-word-value", words[0].offset), ("trace-cursor-frame",))
    ):
        raise StackEvidenceError("trace emission lost its actual PC/frame or another advance bypasses physical output")
    return ("local-owner-and-word-custody", cells, plan.offset, tuple(item.offset for item in words), consume.offset, emit.offset)


def _trace_resolver_custody(obj, symbol, inputs, writes):
    flow = function_flow(obj, symbol)
    identities = {
        "micros_hart_resolve": ("trace-boot-handle",),
        "micros_thread_resolve": ("field", ("trace-object", "hart"), 96, 8),
        "micros_process_resolve": ("field", ("trace-object", "thread"), 8, 8),
    }
    bound, guards = [], {}
    for call in flow.calls:
        if call.target_symbol in identities:
            if inputs[call.offset][10:12] != (("trace-object", "registry"), identities[call.target_symbol]):
                raise StackEvidenceError(
                    f"trace lookup borrowed a registry, current generation or owner: {call.target_symbol} "
                    f"{inputs[call.offset][10:12]!r}"
                )
            bound.append((call.offset, call.target_symbol, inputs[call.offset][10:13]))
            if call.target_symbol != "micros_process_resolve":
                role = "hart" if call.target_symbol == "micros_hart_resolve" else "thread"
                gate = result_zero_gate(obj, symbol, call.offset)
                guards[role] = gate.zero, gate.nonzero, gate.branch
        elif call.target_symbol == "micros_kernel_object_runtime_registry":
            gate = result_zero_gate(obj, symbol, call.offset, bits=64)
            guards["registry"] = gate.nonzero, gate.zero, gate.branch
    reads = []
    for point, (width, word) in flow.instructions.items():
        action = memory_action(width, word)
        if action is None or action[0]:
            continue
        base = inputs[point][action[3]]
        if base is None or base[0] != "trace-object":
            continue
        success, failed, branch = guards[base[1]]
        if (
            point in reachable_instructions(flow, start=failed)
            or point in reachable_instructions(flow, excluded=branch)
            or point not in reachable_instructions(flow, start=success)
        ):
            raise StackEvidenceError("trace metadata read bypasses its actual nonnull or generation-success guard")
        reads.append((point, base[1], action[4], action[1], branch))
    process, = (call for call in flow.calls if call.target_symbol == "micros_process_resolve")
    process_flags = tuple(row for row in writes if row[1] == 160)
    if len(process_flags) != 1 or process_flags[0][2:] != (1, ("result-is-zero", process.offset)):
        raise StackEvidenceError("trace process-live result does not reflect its actual selected-owner resolver success")
    for point, offset, _, _ in writes:
        role = (
            "thread" if offset in (136, 144, 159, 161)
            else "hart" if offset in (16, 88, 96, 104, 112, 120, 128, 152, 156, 157, 158) else None
        )
        if role is not None:
            success, failed, branch = guards[role]
            if point in reachable_instructions(flow, start=failed) or point in reachable_instructions(flow, excluded=branch):
                raise StackEvidenceError("trace owner publication bypasses actual generation-success admission")
    return ("selected-generation-custody", tuple(bound), tuple(reads), process_flags)


def _trace_observation_custody(obj, symbol, inputs):
    flow = function_flow(obj, symbol)
    calls = {}
    for call in flow.calls:
        calls.setdefault(call.target_symbol, []).append(call)

    def follows(producer, consumer):
        if consumer.offset <= producer.offset or consumer.offset in reachable_instructions(
            flow, excluded=flow.continuations[producer.offset],
        ):
            raise StackEvidenceError("trace observation precedes or bypasses its actual primitive's normal return")
        return producer.offset, consumer.offset

    words = calls["micros_panic_trace_load_word"]
    hits = calls["micros_test_work_record"]
    observed = calls["micros_panic_trace_test_word"]
    witnesses = []
    for word, hit, observation in zip(words, hits, observed):
        if inputs[hit.offset][10:13] != tuple(("scalar", value) for value in (1, 1, 1)):
            raise StackEvidenceError("trace word observation borrowed another owner, primitive or work amount")
        witnesses.extend((follows(word, hit), follows(hit, observation)))
        if hit.offset in reachable_instructions(
            flow, start=flow.continuations[observation.offset], excluded=word.offset,
        ):
            raise StackEvidenceError("trace observation repeats without another actual word load")
    owner, = calls["micros_panic_trace_owner_select"]
    enter, = calls["micros_panic_trace_test_enter"]
    selected, = calls["micros_panic_trace_test_owner"]
    if inputs[enter.offset][10:12] != (
        ("stack", -object_frames(obj, function_symbols(obj))[symbol.name]), ("trace-frame-pointer",),
    ) or inputs[selected.offset][10:13] != (
        ("result", (owner.offset, 1), 32), ("trace-bound", 0), ("trace-bound", 16),
    ):
        raise StackEvidenceError("trace entry/owner observation borrowed its actual coordinates or selected bounds")
    plan, = calls["micros_panic_trace_cursor_plan"]
    writes = calls["uart_write"]
    headers = tuple(call for call in writes if call.offset < plan.offset)
    if len(headers) != 3:
        raise StackEvidenceError("trace header has no exact complete formatter call inventory")
    header, = calls["micros_panic_trace_test_header"]
    started, = calls["micros_panic_trace_test_started"]
    complete, = calls["micros_panic_trace_test_complete"]
    if inputs[complete.offset][10:12] != (
        ("result", ("cursor-field", 48), 32), ("result", ("cursor-field", 52), 32),
    ):
        raise StackEvidenceError("trace completion borrowed its current cursor count or stop value")
    witnesses.extend((
        follows(headers[-1], header), follows(header, started), follows(started, plan),
        follows(writes[-1], complete),
    ))
    return "post-primitive-observations", tuple(witnesses), (enter.offset, selected.offset, complete.offset)


def trace_target_effects(obj, *, trace_test=False):
    if type(trace_test) is not bool:
        raise StackEvidenceError("trace target has no exact observation mode")
    symbols = function_symbols(obj)
    if set(symbols) != set(TRACE_TARGET_CALLS) or any(section.flags & 3 == 3 and section.size for section in obj.sections):
        raise StackEvidenceError("trace target changed its closed functions or added mutable static state")
    result = {}
    for name, symbol in symbols.items():
        effects, inputs = _trace_target_function(obj, symbol, trace_test=trace_test)
        relations = (trace_frame_record_effects(obj, symbol),)
        if name == "micros_panic_trace_report":
            relations += (_trace_report_custody(obj, symbol, inputs),)
            if trace_test:
                relations += (_trace_observation_custody(obj, symbol, inputs),)
        elif name == "trace_resolve_owners":
            relations += (_trace_resolver_custody(obj, symbol, inputs, effects.writes),)
        result[name] = MachineEffects(effects.calls, effects.writes, effects.systems, relations)
    return result


def trace_object_layout(root):
    import os
    from pathlib import Path
    import re

    from tests.host.panic_trace_source import fixed_source
    from tools.stack_artifact_files import read_regular
    from tools.validation_context import target_compiler
    from tools.validation_inputs import _file_identity

    root = Path(root)
    pending, checked = ["kernel/objects/kernel_object_private.h"], set()
    while pending:
        name = pending.pop()
        if name in checked:
            continue
        data = read_regular(root / name)[0].decode()
        if data != fixed_source(name):
            raise StackEvidenceError("trace ownership layout changed a fixed predecessor header")
        checked.add(name)
        pending.extend("include/" + name if name.startswith("micros/") else name
                       for name in re.findall(r'^#include "([^"]+)"$', data, flags=re.MULTILINE))
    compiler = str(target_compiler(dict(os.environ)).resolve(strict=True))
    return dict(_trace_layout_object(str(root), compiler, _file_identity(Path(compiler))["sha256"]))


@lru_cache(maxsize=4)
def _trace_layout_object(root, compiler, compiler_sha256):
    from pathlib import Path
    import subprocess
    import tempfile

    from tools.stack_bound_objects import load_object, section_bytes
    from tools.validation_inputs import _file_identity

    expressions = {
        "registry_bytes": "sizeof(struct micros_kernel_objects)",
        "magic": "MICROS_KERNEL_OBJECTS_MAGIC",
        "live": "MICROS_KERNEL_OBJECT_SLOT_LIVE",
        "success": "MICROS_KERNEL_OBJECT_OK",
        "uninitialized": "MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED",
        "process_generation_max": "MICROS_PROCESS_GENERATION_MAX",
        "thread_bottom": "offsetof(struct micros_thread, kernel_stack_bottom)",
        "thread_top": "offsetof(struct micros_thread, kernel_stack_top)",
        "hart_current_generation": "offsetof(struct micros_hart, current_thread) + offsetof(struct micros_thread_handle, generation)",
        "hart_primary_bottom": "offsetof(struct micros_hart, trap) + offsetof(struct micros_hart_trap_anchor, primary_stack_bottom)",
        "hart_primary_top": "offsetof(struct micros_hart, trap) + offsetof(struct micros_hart_trap_anchor, primary_stack_top)",
        "hart_trap_installed": "offsetof(struct micros_hart, trap_installed)",
        "hart_hardware_id": "offsetof(struct micros_hart, hardware_id)",
    }
    for role, plural in (("process", "processes"), ("thread", "threads"), ("hart", "harts")):
        expressions.update({
            role + "_bytes": "sizeof(struct micros_" + role + ")",
            role + "_table": "offsetof(struct micros_kernel_objects, " + plural + ")",
            role + "_state": "offsetof(struct micros_" + role + ", slot_state)",
            role + "_generation": "offsetof(struct micros_" + role + ", generation)",
            role + "_capacity": "MICROS_" + role.upper() + "_CAPACITY",
        })
    source = (
        '#include <stddef.h>\n#include "kernel/objects/kernel_object_private.h"\n'
        + "".join("const uint64_t trace_layout_" + name + " = " + expression + ";\n"
                  for name, expression in expressions.items())
    )
    with tempfile.TemporaryDirectory(prefix="micros-trace-object-layout-") as directory:
        path, output = Path(directory) / "layout.c", Path(directory) / "layout.obj"
        path.write_text(source)
        result = subprocess.run(
            (compiler, "--target=riscv64-unknown-elf", "-std=c17", "-march=rv64imac_zicsr_zifencei", "-mabi=lp64",
             "-ffreestanding", "-fno-builtin", "-fdata-sections", "-I" + root, "-I" + str(Path(root) / "include"),
             "-c", str(path), "-o", str(output)),
            capture_output=True, text=True, timeout=30,
        )
        if result.returncode or _file_identity(Path(compiler))["sha256"] != compiler_sha256:
            raise StackEvidenceError("fixed trace ownership-layout compilation failed or changed: " + result.stderr)
        obj = load_object(output)
        values = []
        for name in expressions:
            selected = tuple(symbol for symbol in obj.symbols if symbol.name == "trace_layout_" + name)
            if len(selected) != 1 or selected[0].size != 8 or selected[0].symbol_type != 1:
                raise StackEvidenceError("trace layout has no exact compiler-produced uint64 object")
            symbol = selected[0]
            section = obj.sections[symbol.section_index]
            if section.flags != 2 or section.section_type != 1 or symbol.value + 8 > section.size:
                raise StackEvidenceError("trace layout constant has a writable or incomplete artifact")
            values.append((name, int.from_bytes(section_bytes(obj, section)[symbol.value:symbol.value + 8], "little")))
        return tuple(values)


TRACE_RESOLVER_CALLS = {
    "micros_kernel_object_runtime_registry": (),
    "micros_kernel_object_runtime_boot_hart_handle": (),
    "micros_kernel_object_runtime_test_registry": (),
    "micros_kernel_object_runtime_boot_hart": ("micros_hart_resolve",),
    "micros_hart_resolve": ("kernel_object_require_initialized", "kernel_object_hart_handle_is_valid"),
    "micros_thread_resolve": ("kernel_object_require_initialized", "kernel_object_thread_handle_is_valid"),
    "micros_process_resolve": ("kernel_object_require_initialized", "kernel_object_process_handle_is_valid"),
    "kernel_object_require_initialized": (),
    "kernel_object_hart_handle_is_valid": (),
    "kernel_object_thread_handle_is_valid": (),
    "kernel_object_process_handle_is_valid": (),
}


def _trace_selected_case(obj, symbol, arguments, returns, readable=(), writable=(), fields=None, sections=None):
    frame = object_frames(obj, function_symbols(obj))[symbol.name]
    flow = function_flow(obj, symbol)
    calls, _ = runtime_call_inventory(obj, symbol)
    expected = TRACE_RESOLVER_CALLS[symbol.name]
    if Counter(call.target_symbol for call in calls) != Counter(expected) or any(call.kind is not EdgeKind.CALL for call in calls):
        raise StackEvidenceError("trace selected resolver acquired an unbound call or table scan")
    results = {call.offset: returns[call.target_symbol] for call in calls}
    states, high, low = prelude_states(obj, symbol, sections or {}, arguments=arguments, results=results, fields=fields)
    writes, reads = [], []
    for point, (width, word) in flow.instructions.items():
        if point not in states:
            continue
        action = memory_action(width, word)
        if action is not None:
            base = high[low[point]][2] if point in low else states[point][0][action[3]]
            if not action[0] and base is not None and base[0] == "symbol" and 0 < base[2] < len(obj.sections):
                section = obj.sections[base[2]]
                if (
                    section.section_type == 1 and section.flags & 2 and not section.flags & 5
                    and 0 <= base[3] + action[4] <= section.size - action[1]
                ):
                    continue
            effect = _observer_storage(point, action, states[point][0], high, low, frame, readable, writable)
            if effect is not None:
                (writes if action[0] else reads).append(effect)
        elif branch_offset(width, word) is None and not is_call(width, word) and not is_return(width, word):
            pure = (
                width == 4 and word & 0x7f in (0x13, 0x1b, 0x33, 0x3b, 0x17, 0x37)
                or width == 2 and (
                    word & 3 == 1 and word >> 13 in (0, 1, 2, 3, 4)
                    or word & 3 == 0 and word >> 13 == 0
                    or word & 3 == 2 and word >> 13 == 0 and word >> 7 & 31
                    or word & 3 == 2 and word >> 13 == 4 and word >> 2 & 31
                )
            )
            if not pure:
                raise StackEvidenceError("trace resolver acquired a privileged, atomic or foreign machine effect")
    returned = tuple(states[point][0][10] for point, instruction in flow.instructions.items()
                     if point in states and is_return(*instruction))
    return tuple(reads), tuple(writes), returned


def _trace_handle_effects(obj, symbol, layout):
    role = symbol.name.removeprefix("kernel_object_").removesuffix("_handle_is_valid")
    capacity = layout[role + "_capacity"]
    maximum = layout["process_generation_max"] if role == "process" else 0xffffffff
    cases = []
    for slot in (0, capacity - 1, capacity, 0xffff):
        for generation in (0, 1, maximum, min(maximum + 1, 0xffffffff)):
            reads, writes, returned = _trace_selected_case(obj, symbol, (slot | generation << 32,), {})
            expected = ("scalar", int(slot < capacity and 0 < generation <= maximum))
            if reads or writes or not returned or any(value != expected for value in returned):
                raise StackEvidenceError(
                    f"trace handle predicate changed actual admission: {role} slot={slot} generation={generation} "
                    f"returns={returned!r} expected={expected!r}"
                )
            cases.append((slot, generation, expected))
    return MachineEffects((), (), (), (("actual-handle-shape-classes", tuple(cases)), trace_frame_record_effects(obj, symbol)))


def _trace_resolved_slot_effects(obj, symbol, role, layout):
    registry, output = 0x6200000000000000, 0x6300000000000000
    table, size, capacity = (layout[role + suffix] for suffix in ("_table", "_bytes", "_capacity"))
    valid = "kernel_object_" + role + "_handle_is_valid"
    records, sites = [], None
    for slot in range(capacity):
        address = registry + table + slot * size
        readable = ((address + layout[role + "_state"], 4), (address + layout[role + "_generation"], 4))
        reads, writes, _ = _trace_selected_case(
            obj, symbol, (registry, slot | 1 << 32, output),
            {"kernel_object_require_initialized": 0, valid: 1}, readable, ((output, 8),),
        )
        if (
            len(writes) != 1 or writes[0][1:] != (output, 8, ("scalar", address))
            or {row[1:3] for row in reads} != set(readable)
        ):
            raise StackEvidenceError("trace resolver publishes another slot or writes beyond its one pointer output")
        current_sites = tuple(row[0] for row in reads), writes[0][0]
        if sites is not None and current_sites != sites:
            raise StackEvidenceError("trace selected-slot effects do not share one complete machine path")
        sites = current_sites
        records.append((slot, table + slot * size))
    for initialized, shape in ((layout["uninitialized"], 1), (0, 0)):
        reads, writes, _ = _trace_selected_case(
            obj, symbol, (registry, 1 << 32, output),
            {"kernel_object_require_initialized": initialized, valid: shape},
        )
        if reads or writes:
            raise StackEvidenceError("trace resolver touches a record or output before successful admission")
    return MachineEffects(
        tuple((call.offset, call.target_symbol) for call in function_flow(obj, symbol).calls),
        ((sites[1], "caller-output", 2, 0, 8),), (),
        (("complete-selected-slot-custody", role, tuple(records), sites), trace_frame_record_effects(obj, symbol)),
    )


def trace_resolver_effects(obj, unit, layout):
    roles = {
        "kernel/objects/kernel_object_hart.c": ("hart",),
        "kernel/objects/kernel_object_thread.c": ("thread",),
        "kernel/objects/kernel_object_process.c": ("process",),
    }
    symbols = function_symbols(obj)
    if unit in roles:
        role, = roles[unit]
        name = "micros_" + role + "_resolve"
        if name not in symbols:
            raise StackEvidenceError("trace selected resolver has no actual function owner")
        return {name: _trace_resolved_slot_effects(obj, symbols[name], role, layout)}
    if unit != "kernel/objects/kernel_object_identity.c":
        raise StackEvidenceError("trace resolver borrowed an undeclared source unit")
    result = {}
    for role in ("hart", "thread", "process"):
        name = "kernel_object_" + role + "_handle_is_valid"
        result[name] = _trace_handle_effects(obj, symbols[name], layout)
    symbol = symbols["kernel_object_require_initialized"]
    address = 0x6200000000000000
    cases = []
    for magic in (layout["magic"], layout["magic"] ^ 1):
        reads, writes, returned = _trace_selected_case(
            obj, symbol, (address,), {}, ((address, 8),), fields={(address, 8): magic},
        )
        expected = ("scalar", 0 if magic == layout["magic"] else layout["uninitialized"])
        if writes or len(reads) != 1 or not returned or any(value != expected for value in returned):
            raise StackEvidenceError("trace registry initialization no longer uses its exact original magic")
        cases.append((magic, expected))
    result[symbol.name] = MachineEffects((), (), (), (("initialization-magic", tuple(cases)), trace_frame_record_effects(obj, symbol)))
    return result


def _trace_fixture_hart_accessor(obj, sections, layout, objects):
    symbol = function_symbols(obj)["micros_kernel_object_runtime_boot_hart"]
    flow = function_flow(obj, symbol)
    call, = flow.calls
    ready, handle, registry = (objects[name] for name in ("runtime_ready", "boot_hart_handle", "kernel_objects"))
    frame = object_frames(obj, function_symbols(obj))[symbol.name]
    cases = []
    for enabled in (0, 1):
        for slot in range(layout["hart_capacity"]):
            for status in (0, 1):
                value = slot | 0xffffffff << 32
                fields = {
                    (ready, 1): enabled, (handle, 8): value, (handle, 2): slot,
                    (handle, 4): slot, (handle + 4, 4): 0xffffffff,
                }
                reads, writes, returned = _trace_selected_case(
                    obj, symbol, (), {"micros_hart_resolve": status},
                    ((ready, 1), (handle, 8)), fields=fields, sections=sections,
                )
                expected = registry + layout["hart_table"] + slot * layout["hart_bytes"] if enabled and status == 0 else 0
                if writes or not returned or any(current != ("scalar", expected) for current in returned):
                    raise StackEvidenceError("trace fixture hart getter borrowed another slot or bypassed successful resolution")
                states, _, _ = prelude_states(
                    obj, symbol, sections, results={call.offset: status}, fields=fields,
                )
                if enabled:
                    arguments = states[call.offset][0][10:13]
                    if (
                        arguments[0] != ("scalar", registry) or arguments[1] is None or arguments[1][0] != "scalar"
                        or arguments[1][1] & 0xffffffff0000ffff != value
                        or arguments[2] is None or arguments[2][0] != "stack"
                        or not -frame <= arguments[2][1] <= -24
                    ):
                        raise StackEvidenceError("trace fixture hart getter borrowed its registry, handle or output cell")
                elif call.offset in states:
                    raise StackEvidenceError("trace fixture hart getter resolves an uninitialized runtime")
                cases.append((enabled, slot, status, expected, reads))
    return MachineEffects(
        ((call.offset, call.target_symbol),), (), (),
        (("fixture-selected-hart", tuple(cases)), trace_frame_record_effects(obj, symbol)),
    )


def trace_accessor_effects(obj, sections, layout, *, fixture=False):
    if type(fixture) is not bool:
        raise StackEvidenceError("trace accessor has no exact fixture mode")
    objects = {}
    for name, size, alignment in (
        ("kernel_objects", layout["registry_bytes"], 8), ("boot_hart_handle", 8, 4), ("runtime_ready", 1, 1),
    ):
        selected = tuple(symbol for symbol in obj.symbols if symbol.name == name and symbol.symbol_type == 1)
        if len(selected) != 1:
            raise StackEvidenceError("trace runtime getter lacks its exact singleton object")
        symbol = selected[0]
        if (
            symbol.binding != 0 or symbol.size != size or symbol.section_index not in sections
            or symbol.value % alignment
        ):
            raise StackEvidenceError("trace runtime getter changed its private object type or ownership")
        section = obj.sections[symbol.section_index]
        if section.section_type != 8 or section.flags != 3 or symbol.value + size > section.size:
            raise StackEvidenceError("trace runtime getter has an initialized, foreign or incomplete singleton")
        objects[name] = sections[symbol.section_index] + symbol.value
    ready, handle, registry = (objects[name] for name in ("runtime_ready", "boot_hart_handle", "kernel_objects"))
    readable = ((ready, 1), (handle, 8))
    result = {}
    names = ("micros_kernel_object_runtime_registry", "micros_kernel_object_runtime_boot_hart_handle") + (
        ("micros_kernel_object_runtime_test_registry",) if fixture else ()
    )
    for name in names:
        symbol = function_symbols(obj)[name]
        cases = []
        for enabled in (0, 1):
            for value in (0, 0x1234567800000007, 0xffffffffa55a0007):
                fields = {
                    (ready, 1): enabled, (handle, 8): value, (handle, 4): value & 0xffffffff,
                    (handle + 4, 4): value >> 32, (handle, 2): value & 0xffff,
                }
                reads, writes, returned = _trace_selected_case(
                    obj, symbol, (), {}, readable, fields=fields, sections=sections,
                )
                expected = registry if name.endswith("_registry") else value
                expected = expected if enabled else 0
                mask = 0xffffffffffffffff if name.endswith("_registry") or not enabled else 0xffffffff0000ffff
                if writes or not returned or any(
                    current is None or current[0] != "scalar" or current[1] & mask != expected & mask
                    for current in returned
                ):
                    raise StackEvidenceError(
                        f"trace accessor changed its fixed identity: {name} ready={enabled} handle={value:#x} "
                        f"returns={returned!r} expected={expected:#x} writes={writes!r}"
                    )
                cases.append((enabled, value, expected, mask, returned, reads))
        result[name] = MachineEffects(
            (), (), (), (("fixed-runtime-accessor", tuple(cases)), trace_frame_record_effects(obj, symbol)),
        )
    if fixture:
        result["micros_kernel_object_runtime_boot_hart"] = _trace_fixture_hart_accessor(obj, sections, layout, objects)
    return result


TRACE_FIXTURE = "tests/qemu/panic_trace_kernel.c"
TRACE_FIXTURE_CALLS = {
    "active": (),
    "require": ("micros_panic_trace_test_error",),
    "micros_panic_trace_test_lsr": ("require",),
    "micros_panic_trace_test_thr": ("active", "require", "require"),
    "micros_panic_trace_test_seizure": ("active", "require"),
    "micros_panic_trace_test_report": ("active", "require"),
    "micros_panic_trace_test_after_core": ("active", "require", "require"),
    "micros_panic_trace_test_enter": ("active", "require"),
    "micros_panic_trace_test_owner": ("active", "require", "require"),
    "micros_panic_trace_test_header": ("require",),
    "micros_panic_trace_test_before_word": ("require", "micros_panic_entry"),
    "micros_panic_trace_test_word": ("require",),
    "micros_panic_trace_test_link": ("require",),
    "micros_panic_trace_test_complete": ("require",),
    "micros_panic_trace_test_reset": ("active", "micros_test_work_finish") + ("require",) * 7,
}
TRACE_FIXTURE_OBJECTS = {
    "micros_panic_trace_test_scenario": 4,
    "micros_panic_trace_test_observation": 192,
    "micros_panic_trace_test_work_snapshot": 3056,
    "micros_panic_trace_test_history": 12012,
    "objects": 8, "hart": 8, "selected_thread": 8, "fixture_hart_id": 8,
    "forced_reads": 8, "physical_writes": 8, "corruption_injected": 1, "recursion_injected": 1,
}
TRACE_OBSERVATION_WRITES = {
    "micros_panic_trace_test_seizure": ((24, 8),),
    "micros_panic_trace_test_report": ((16, 8), (72, 8)),
    "micros_panic_trace_test_enter": ((32, 8), (80, 8), (88, 8), (128, 8), (136, 8), (144, 8)),
    "micros_panic_trace_test_owner": ((96, 8), (104, 8), (112, 4)),
    "micros_panic_trace_test_header": ((8, 4),),
    "micros_panic_trace_test_word": ((48, 8),),
    "micros_panic_trace_test_link": ((56, 8),),
    "micros_panic_trace_test_complete": ((40, 8), (116, 4), (120, 4)),
    "micros_panic_trace_test_reset": ((8, 4), (12, 4), (64, 8), (152, 8), (160, 8), (168, 8), (176, 4), (180, 4)),
}


def _trace_fixture_storage(obj, sections):
    result = {}
    for name, size in TRACE_FIXTURE_OBJECTS.items():
        symbols = tuple(symbol for symbol in obj.symbols if symbol.name == name and symbol.symbol_type == 1)
        if len(symbols) != 1 or symbols[0].size != size or symbols[0].section_index not in sections:
            raise StackEvidenceError("trace fixture lost a complete typed observation/input object: " + name)
        symbol = symbols[0]
        section = obj.sections[symbol.section_index]
        if section.flags != 3 or section.section_type != 8 or symbol.value + size > section.size:
            raise StackEvidenceError("trace fixture object has unbound storage")
        result[name] = sections[symbol.section_index] + symbol.value
    return result


def _trace_fixture_require(obj, sections):
    symbol = function_symbols(obj)["require"]
    flow = function_flow(obj, symbol)
    if len(flow.calls) != 1 or flow.calls[0].target_symbol != "micros_panic_trace_test_error":
        raise StackEvidenceError("trace fixture assertion lost its nonreturning error boundary")
    failure = flow.calls[0].offset
    witnesses = []
    for argument in (0, 1):
        states, _, _ = prelude_states(obj, symbol, sections, arguments=(argument,))
        reached, pending = set(), [0]
        while pending:
            point = pending.pop()
            if point in reached or point not in states:
                continue
            reached.add(point)
            if point != failure:
                successors = flow.successors[point]
                width, word = flow.instructions[point]
                branch = branch_offset(width, word)
                if branch is not None and branch[1]:
                    successors = (successors[0 if branch_taken(width, word, states[point][0]) else 1],)
                pending.extend(successors)
        returned = tuple(point for point in reached if is_return(*flow.instructions[point]))
        if (failure in reached) != (argument == 0) or bool(returned) != bool(argument):
            raise StackEvidenceError("trace fixture assertion returns on failure or rejects its true input")
        witnesses.append((argument, tuple(sorted(reached))))
    return tuple(witnesses)


def _trace_fixture_slot_guard(obj, sections, objects, layout, aliases):
    symbol = function_symbols(obj)["prepare_thread_stack"]
    flow = function_flow(obj, symbol)
    selected = objects["selected_thread"]
    results = {call.offset: 0 for call in flow.calls if (
        call.target_symbol != "require" and call.offset in flow.continuations
    )}
    common = {(objects["objects"], 8): aliases["registry"], (objects["hart"], 8): aliases["hart"]}
    candidates = {call.offset for call in flow.calls if call.target_symbol == "require"}
    good = None
    for slot in (*range(layout["thread_capacity"]), 0xffff):
        fields = {**common, (selected, 2): slot, (selected, 4): slot,
                  (selected, 8): slot | 1 << 32, (selected + 4, 4): 1}
        states, high, low = prelude_states(
            obj, symbol, sections, results=results, fields=fields,
            external_values={
                "micros_thread_kernel_stacks": aliases["bank"],
                "micros_thread_kernel_stacks_end": aliases["bank"] + layout["thread_capacity"] * 16384,
            },
        )
        if slot == 1:
            good = states, high, low
        else:
            candidates &= {point for point in candidates if states[point][0][10] == ("scalar", 0)}
    states, high, low = good
    candidates = {point for point in candidates if states[point][0][10] != ("scalar", 0)}
    if len(candidates) != 1:
        raise StackEvidenceError("trace constructor lacks the actual selected-slot-one assertion")
    guard, = candidates
    require_effect_return_dominance(flow, flow.continuations[guard])
    argument_counts = {
        "require": 1, "micros_kernel_object_runtime_boot_hart_handle": 0,
        "micros_process_create": 2, "micros_thread_create": 3,
        "micros_thread_attach_execution_context": 5, "micros_scheduler_accounting_initialize": 3,
        "micros_thread_scheduler_admit": 6, "micros_hart_plan_user_return": 3,
        "micros_hart_commit_user_return": 2,
    }
    if set(call.target_symbol for call in flow.calls) != set(argument_counts):
        raise StackEvidenceError("trace constructor changed its closed setup API set")
    outputs = []
    for call in flow.calls:
        arguments = states[call.offset][0][10:10 + argument_counts[call.target_symbol]]
        for index, value in enumerate(arguments):
            if value != ("scalar", selected):
                continue
            if call.target_symbol != "micros_thread_create" or index != 2:
                raise StackEvidenceError("trace constructor escaped its selected-thread handle output")
            if guard in reachable_instructions(flow, excluded=flow.continuations[call.offset]):
                raise StackEvidenceError("trace selected-slot assertion bypasses its actual handle producer")
            outputs.append(call.offset)
    if len(outputs) != 1:
        raise StackEvidenceError("trace constructor has no unique selected-thread handle producer")
    for point, (width, word) in flow.instructions.items():
        action = memory_action(width, word)
        if action is None or not action[0] or point not in states:
            continue
        _, size, _, basis, offset = action
        base = high[low[point]][2] if point in low else states[point][0][basis]
        if base is not None and base[0] == "scalar" and max(base[1] + offset, selected) < min(base[1] + offset + size, selected + 8):
            raise StackEvidenceError("trace constructor overwrites its proved selected-thread handle")
    return ("selected-slot-one", tuple(outputs), guard, layout["thread_capacity"], 0xffff)


def trace_fixture_alias_effects(obj, sections, layout, aliases):
    objects = _trace_fixture_storage(obj, sections)
    symbol = function_symbols(obj)["micros_panic_trace_test_run"]
    flow = function_flow(obj, symbol)
    states, high, low = prelude_states(obj, symbol, sections)
    armed = tuple(call for call in flow.calls if call.target_symbol == "micros_panic_trace_test_armed")
    if len(armed) != 1:
        raise StackEvidenceError("trace constructor lost its exact actual arming boundary")
    result = []
    for target, getter in (
        ("objects", "micros_kernel_object_runtime_test_registry"),
        ("hart", "micros_kernel_object_runtime_boot_hart"),
    ):
        calls = tuple(call for call in flow.calls if call.target_symbol == getter)
        if len(calls) != 1:
            raise StackEvidenceError("trace constructor borrowed another registry or hart getter")
        call, = calls
        inputs = register_inputs(obj, symbol, result_call=call.offset, result_bits=64)
        writes = []
        for point, (width, word) in flow.instructions.items():
            action = memory_action(width, word)
            if action is None or not action[0] or point not in states:
                continue
            _, size, register, basis, offset = action
            base = high[low[point]][2] if point in low else states[point][0][basis]
            if base is None or base[0] != "scalar" or base[1] + offset != objects[target]:
                continue
            if size != 8 or inputs[point][register] != ("result", call.offset, 64):
                raise StackEvidenceError("trace constructor lost actual getter-to-alias result custody")
            if armed[0].offset in reachable_instructions(flow, excluded=point):
                raise StackEvidenceError("trace arming bypasses its actual alias store")
            writes.append(point)
        if len(writes) != 1:
            raise StackEvidenceError("trace constructor lost its unique private alias store")
        result.append((target, getter, call.offset, objects[target], tuple(writes)))
    result.append(("assertion", _trace_fixture_require(obj, sections)))
    result.append(_trace_fixture_slot_guard(obj, sections, objects, layout, aliases))
    for name, target in (
        ("trace_chain_one", "micros_panic_entry"),
        ("trace_chain_two", "trace_chain_one"),
        ("trace_chain_three", "trace_chain_two"),
    ):
        symbol = function_symbols(obj)[name]
        calls = function_flow(obj, symbol).calls
        if len(calls) != 1 or calls[0].target_symbol != target:
            raise StackEvidenceError("trace real C chain changed its closed entry inventory")
        states, high, low = prelude_states(obj, symbol, sections)
        frame = object_frames(obj, function_symbols(obj))[name]
        for point, (width, word) in function_flow(obj, symbol).instructions.items():
            action = memory_action(width, word)
            if action is not None:
                _observer_storage(
                    point, action, states[point][0], high, low, frame, ((objects["fixture_hart_id"], 8),), (),
                )
            elif width == 4 and word & 0x7f in (0x73, 0x2f, 0x0f):
                raise StackEvidenceError("trace real C chain changes supervisor or atomic state")
        result.append((name, calls[0].offset, target, trace_frame_record_effects(obj, symbol)))
    return tuple(result)


def _trace_fixture_case(obj, symbol, sections, objects, layout, aliases, scenario):
    observation = objects["micros_panic_trace_test_observation"]
    snapshot = objects["micros_panic_trace_test_work_snapshot"]
    capture = 0x6100000000000000
    frame = object_frames(obj, function_symbols(obj))[symbol.name]
    flow = function_flow(obj, symbol)
    calls, hooks = runtime_call_inventory(obj, symbol)
    if hooks or Counter(call.target_symbol for call in calls) != Counter(TRACE_FIXTURE_CALLS[symbol.name]):
        raise StackEvidenceError("trace fixture changed its closed qualified call set: " + symbol.name)
    writable = tuple((observation + offset, size) for offset, size in TRACE_OBSERVATION_WRITES.get(symbol.name, ()))
    private = {
        "micros_panic_trace_test_lsr": "forced_reads",
        "micros_panic_trace_test_thr": "physical_writes",
        "micros_panic_trace_test_before_word": "recursion_injected",
    }.get(symbol.name)
    if private is not None:
        writable += ((objects[private], TRACE_FIXTURE_OBJECTS[private]),)
    if symbol.name == "micros_panic_trace_test_after_core" and scenario in (2, 13, 14):
        writable += ((objects["corruption_injected"], 1),)
        if scenario == 2:
            writable += ((aliases["registry"], 8),)
        elif scenario == 13:
            writable += ((aliases["hart"] + layout["hart_current_generation"], 4),)
        else:
            writable += tuple((aliases["thread"] + layout[name], 8) for name in ("thread_bottom", "thread_top"))
            writable += tuple((aliases["hart"] + layout[name], 8) for name in ("hart_primary_bottom", "hart_primary_top"))
    readable = tuple((address, TRACE_FIXTURE_OBJECTS[name]) for name, address in objects.items()) + (
        (capture, 48), (aliases["registry"], 8), (aliases["hart"] + layout["hart_current_generation"], 4),
    )
    fields = {
        (objects["objects"], 8): aliases["registry"], (objects["hart"], 8): aliases["hart"],
        (objects["selected_thread"], 2): 1, (observation + 4, 4): scenario,
        (observation + 8, 4): 1 if symbol.name.endswith(("_header", "_after_core", "_enter", "_owner", "_report", "_seizure")) else 2,
    }
    arguments = (capture,) if symbol.name.endswith("_report") else ((0, int(scenario != 18)) if symbol.name.endswith("_reset") else ())
    results = {call.offset: 1 if call.target_symbol == "active" else 0
               for call in calls if call.target_symbol in ("active", "micros_test_work_finish")}
    states, high, low = prelude_states(
        obj, symbol, sections, arguments=arguments, results=results, fields=fields,
        external_values={"micros_thread_kernel_stacks": aliases["bank"], "micros_panic_trace_test_load_fault": aliases["fault"]},
    )
    writes, systems = [], []
    for point, (width, word) in flow.instructions.items():
        if point not in states:
            continue
        action = memory_action(width, word)
        if action is not None:
            effect = _observer_storage(point, action, states[point][0], high, low, frame, readable, writable)
            if effect is not None and action[0]:
                writes.append(effect)
        elif width == 4 and word & 0x7f == 0x73:
            allowed = (
                (0x100, 0x104, 0x140) if symbol.name.endswith("_enter")
                else (0x100, 0x104, 0x140, 0x141, 0x142, 0x143) if symbol.name.endswith("_reset") else ()
            )
            if word >> 12 & 7 != 2 or word >> 15 & 31 or word >> 20 not in allowed:
                raise StackEvidenceError("trace fixture changes privileged state or reads a foreign CSR")
            systems.append((point, word))
        elif branch_offset(width, word) is None and not is_call(width, word) and not is_return(width, word):
            pure = (
                width == 4 and word & 0x7f in (0x13, 0x1b, 0x33, 0x3b, 0x17, 0x37)
                or width == 2 and (
                    word & 3 == 1 and word >> 13 in (0, 1, 2, 3, 4)
                    or word & 3 == 0 and word >> 13 == 0
                    or word & 3 == 2 and word >> 13 == 0 and word >> 7 & 31
                    or word & 3 == 2 and word >> 13 == 4 and word >> 2 & 31
                )
            )
            if not pure:
                raise StackEvidenceError("trace fixture has an unbound atomic, fence or instruction")
    for call in calls:
        if call.target_symbol == "micros_test_work_finish" and states[call.offset][0][10] != ("scalar", snapshot):
            raise StackEvidenceError("trace fixture borrowed the 192-byte observation as its 3056-byte work snapshot")
    return MachineEffects(tuple((call.offset, call.target_symbol) for call in calls), tuple(writes), tuple(systems))


def _trace_fault_registers(width, word):
    if width == 4:
        opcode = word & 0x7f
        if opcode in (0x13, 0x1b):
            return (word >> 15 & 31,)
        if opcode in (0x33, 0x3b, 0x63):
            return word >> 15 & 31, word >> 20 & 31
        return ()
    quadrant, operation = word & 3, word >> 13
    if quadrant == 1 and operation in (0, 1):
        return (word >> 7 & 31,)
    if quadrant == 1 and operation == 4:
        first = 8 + (word >> 7 & 7)
        return (first, 8 + (word >> 2 & 7)) if word >> 10 & 3 == 3 else (first,)
    if quadrant == 1 and operation in (6, 7):
        return (8 + (word >> 7 & 7),)
    if quadrant == 2 and operation == 0:
        return (word >> 7 & 31,)
    if quadrant == 2 and operation == 4 and word >> 2 & 31:
        return (word >> 7 & 31, word >> 2 & 31) if word >> 12 & 1 else (word >> 2 & 31,)
    return ()


def _trace_fault_comparisons(obj, symbol):
    flow, inputs = function_flow(obj, symbol), trace_register_inputs(obj, symbol)
    expected = {
        0x141: ("symbol", "micros_panic_trace_test_load_fault", 0, 0),
        0x142: ("scalar", 13), 0x143: ("scalar", 0),
    }
    comparisons, reads = [], Counter()
    for point, (width, word) in flow.instructions.items():
        registers = inputs[point]
        if width == 4 and word & 0x707f == 0x2073 and word >> 15 & 31 == 0 and word >> 20 in expected:
            reads[word >> 20] += 1
        action = memory_action(width, word)
        if action is not None:
            write, size, register, basis, _ = action
            value = registers[register] if write else None
            if value is not None and (value[0] == "trace-csr-difference" or value[0] == "csr" and value[1] in expected):
                if size != 8 or registers[basis] is None or registers[basis][0] != "stack":
                    raise StackEvidenceError("trace fault witness escaped or truncated its actual CSR value")
            continue
        if any(call.offset == point for call in flow.calls) and registers[10] is not None:
            value = registers[10]
            if value[0] == "trace-csr-difference" or value[0] == "csr" and value[1] in expected:
                raise StackEvidenceError("trace fault assertion consumes a raw CSR rather than its exact predicate")
        used = tuple(registers[index] for index in _trace_fault_registers(width, word))
        witnesses = tuple(value for value in used if value is not None and (
            value[0] == "trace-csr-difference" or value[0] == "csr" and value[1] in expected
        ))
        if not witnesses:
            continue
        move = (
            width == 4 and word & 0x707f == 0x13 and word >> 20 == 0
            or width == 2 and word & 0xf003 == 0x8002 and word >> 2 & 31
        )
        if move:
            continue
        if len(witnesses) != 1:
            raise StackEvidenceError("trace fault predicate mixes actual CSR identities")
        value, = witnesses
        csr = value[1][1] if value[0] == "trace-csr-difference" else value[1]
        other = tuple(item for item in used if item != value)
        desired = ("scalar", 0) if value[0] == "trace-csr-difference" else expected[csr]
        if value[0] == "trace-csr-difference" and value[2] != expected[csr]:
            raise StackEvidenceError("trace fault predicate borrowed a different expected value")
        xor = width == 4 and word & 0xfe00707f == 0x4033 or width == 2 and word & 0xfc63 == 0x8c21
        branch = (
            width == 4 and word & 0x7f == 0x63 and word >> 12 & 7 in (0, 1)
            or width == 2 and word & 3 == 1 and word >> 13 in (6, 7)
        )
        zero = width == 4 and word & 0x707f == 0x3013 and word >> 20 == 1
        if (
            xor and value[0] == "csr" and other == (desired,)
            or branch and (other or (("scalar", 0),)) == (desired,)
            or zero and desired == ("scalar", 0)
        ):
            comparisons.append((point, csr, value, desired))
        else:
            raise StackEvidenceError("trace fault CSR is not used solely by its exact equality predicate")
    if reads != Counter({number: 1 for number in expected}) or {row[1] for row in comparisons} != set(expected):
        raise StackEvidenceError("trace fault witness omitted its actual cause, null address or linked PC comparison")
    return tuple(comparisons)


def _trace_fixture_csr_outputs(obj, sections, objects):
    observation = objects["micros_panic_trace_test_observation"]
    result = []
    for name, first in (("micros_panic_trace_test_enter", 128), ("micros_panic_trace_test_reset", 152)):
        symbol = function_symbols(obj)[name]
        flow, inputs = function_flow(obj, symbol), trace_register_inputs(obj, symbol)
        states, high, low = prelude_states(obj, symbol, sections)
        expected = {observation + first + index * 8: ("csr", csr) for index, csr in enumerate((0x100, 0x104, 0x140))}
        writes = []
        for point, (width, word) in flow.instructions.items():
            action = memory_action(width, word)
            if action is None or not action[0]:
                continue
            _, size, register, basis, offset = action
            base = high[low[point]][2] if point in low else states[point][0][basis]
            address = base[1] + offset if base is not None and base[0] == "scalar" else None
            if address in expected:
                if size != 8 or inputs[point][register] != expected[address]:
                    raise StackEvidenceError("trace observer published borrowed interrupt or stack-anchor state")
                writes.append((point, address, expected[address]))
        if Counter(row[1] for row in writes) != Counter(expected.keys()):
            raise StackEvidenceError("trace observer omitted an actual interrupt-state publication")
        result.append((name, tuple(writes)))
    return tuple(result)


def _trace_case_returns(flow, states, excluded):
    reached, pending = set(), [0]
    while pending:
        point = pending.pop()
        if point == excluded or point in reached or point not in states:
            continue
        reached.add(point)
        width, word = flow.instructions[point]
        successors = flow.successors[point]
        branch = branch_offset(width, word)
        if branch is not None and branch[1]:
            registers = states[point][0]
            values = (
                (registers[8 + (word >> 7 & 7)], ("scalar", 0)) if width == 2
                else (registers[word >> 15 & 31], registers[word >> 20 & 31])
            )
            if all(value is not None and value[0] == "scalar" for value in values):
                successors = (successors[0 if branch_taken(width, word, registers) else 1],)
        pending.extend(successors)
    return tuple(sorted(point for point in reached if is_return(*flow.instructions[point])))


def _trace_fixture_fault_custody(obj, sections, objects, fault):
    symbol = function_symbols(obj)["micros_panic_trace_test_reset"]
    flow = function_flow(obj, symbol)
    comparisons = _trace_fault_comparisons(obj, symbol)
    results = {call.offset: 1 if call.target_symbol == "active" else 0
               for call in flow.calls if call.target_symbol in ("active", "micros_test_work_finish")}
    fields = {(objects["micros_panic_trace_test_observation"] + 4, 4): 15}
    cases = []
    for mask in range(8):
        csrs = {number: value ^ int(bool(mask & 1 << index))
                for index, (number, value) in enumerate(((0x141, fault), (0x142, 13), (0x143, 0)))}
        states, _, _ = prelude_states(
            obj, symbol, sections, results=results, fields=fields,
            external_values={"micros_panic_trace_test_load_fault": fault}, csr_values=csrs,
        )
        predicates = {call.offset: states[call.offset][0][10]
                      for call in flow.calls if call.target_symbol == "require" and call.offset in states}
        if mask == 0:
            admitted = {point for point, value in predicates.items() if value == ("scalar", 1)}
        else:
            rejected = tuple(point for point in admitted if predicates.get(point) == ("scalar", 0))
            if not rejected or not _trace_case_returns(flow, states, None):
                raise StackEvidenceError("trace reset can accept a wrong actual load-fault CSR tuple")
            for point in rejected:
                if _trace_case_returns(flow, states, flow.continuations[point]):
                    raise StackEvidenceError("trace reset bypasses its actual fault-tuple assertion")
        cases.append((mask, tuple(sorted(csrs.items())), tuple(sorted(predicates.items()))))
    return comparisons, tuple(cases)


def trace_fixture_effects(obj, sections, layout, aliases):
    if set(aliases) != {"registry", "hart", "thread", "bank", "fault"} or any(
        type(value) is not int or not 0 < value <= 0xffffffffffffffff for value in aliases.values()
    ):
        raise StackEvidenceError("trace fixture lacks its complete actual input-alias bindings")
    objects = _trace_fixture_storage(obj, sections)
    result = {}
    for name in TRACE_FIXTURE_CALLS:
        symbol = function_symbols(obj).get(name)
        if symbol is None:
            raise StackEvidenceError("trace fixture omitted a qualified observer")
        cases = tuple(_trace_fixture_case(obj, symbol, sections, objects, layout, aliases, scenario) for scenario in range(1, 19))
        result[name] = MachineEffects(
            cases[0].calls, tuple((index + 1, case.writes) for index, case in enumerate(cases)),
            tuple((index + 1, case.systems) for index, case in enumerate(cases)),
            (("closed-scenario-observer-effects", 18), trace_frame_record_effects(obj, symbol)),
        )
    reset = result["micros_panic_trace_test_reset"]
    result["micros_panic_trace_test_reset"] = MachineEffects(
        reset.calls, reset.writes, reset.systems,
        (*reset.dependencies, ("actual-csr-publication", _trace_fixture_csr_outputs(obj, sections, objects)),
         ("actual-null-load-csr-tuple", _trace_fixture_fault_custody(obj, sections, objects, aliases["fault"]))),
    )
    return result


def _trace_collector_case(obj, symbol, sections, collector, snapshot, kind):
    frame = object_frames(obj, function_symbols(obj))[symbol.name]
    require_c_frame_movement(obj, symbol, frame)
    flow = function_flow(obj, symbol)
    calls, hooks = runtime_call_inventory(obj, symbol)
    expected_calls = ("latch_fault",) * 3 if symbol.name == "micros_test_work_record" else OBSERVER_CALLS[WORK_COLLECTOR, symbol.name]
    if hooks or Counter(call.target_symbol for call in calls) != Counter(expected_calls):
        raise StackEvidenceError("trace collector changed its closed EV02 call set")
    if symbol.name == "publish_snapshot":
        return _snapshot_copy_effects(obj, symbol, sections, collector, snapshot)
    arguments = {
        "micros_test_work_record": (1, kind, 1),
        "micros_test_work_finish": (snapshot,),
    }.get(symbol.name, ())
    states, high, low = prelude_states(obj, symbol, sections, arguments=arguments)
    writes, systems = [], []
    for point, (width, word) in flow.instructions.items():
        if point not in states:
            continue
        action = memory_action(width, word)
        if action is not None:
            effect = _observer_storage(
                point, action, states[point][0], high, low, frame, ((collector, 3072),), ((collector, 1),),
            )
            if effect is not None and action[0]:
                writes.append(effect)
        elif width == 4 and word & 0x7f == 0x2f:
            operation, size = word >> 27, word >> 12 & 7
            address = states[point][0][word >> 15 & 31]
            expected = (
                (0, ("scalar", collector + 48 + (14 + kind) * 8)) if symbol.name == "micros_test_work_record"
                else (8, ("scalar", collector + 8)) if symbol.name == "latch_fault" else None
            )
            if (
                expected is None or (operation, address) != expected or size != 3 or word >> 25 & 3
                or symbol.name == "micros_test_work_record" and states[point][0][word >> 20 & 31] != ("scalar", 1)
            ):
                raise StackEvidenceError("trace EV02 update borrowed a work kind, target, unit or atomic mode")
            writes.append((point, address[1], 8, ("atomic", operation)))
        elif width == 4 and word & 0x7f == 0x0f:
            if word not in (0x0230000f, 0x0310000f):
                raise StackEvidenceError("trace collector has an unbound memory-order fence")
            systems.append((point, word))
        elif width == 4 and word & 0x7f == 0x73:
            raise StackEvidenceError("trace collector changes supervisor or firmware state")
        elif branch_offset(width, word) is None and not is_call(width, word) and not is_return(width, word):
            pure = (
                width == 4 and word & 0x7f in (0x13, 0x1b, 0x33, 0x3b, 0x17, 0x37)
                or width == 2 and (
                    word & 3 == 1 and word >> 13 in (0, 1, 2, 3, 4)
                    or word & 3 == 0 and word >> 13 == 0
                    or word & 3 == 2 and word >> 13 == 0 and word >> 7 & 31
                    or word & 3 == 2 and word >> 13 == 4 and word >> 2 & 31
                )
            )
            if not pure:
                raise StackEvidenceError("trace collector has an unknown instruction")
    for call in calls:
        if call.target_symbol == "publish_snapshot" and states[call.offset][0][10] != ("scalar", snapshot):
            raise StackEvidenceError("trace collector borrowed another snapshot destination")
    return MachineEffects(tuple((call.offset, call.target_symbol) for call in calls), tuple(writes), tuple(systems))


def trace_collector_effects(obj, sections, collector, snapshot):
    symbols = tuple(symbol for symbol in obj.symbols if symbol.name == "collector" and symbol.symbol_type == 1)
    if len(symbols) != 1:
        raise StackEvidenceError("trace collector has no unique storage")
    symbol, = symbols
    if (
        symbol.size != 3072 or symbol.section_index not in sections
        or obj.sections[symbol.section_index].section_type != 8
        or obj.sections[symbol.section_index].flags != 3
        or sections[symbol.section_index] + symbol.value != collector
        or type(snapshot) is not int or not 0 < snapshot <= 0xffffffffffffffff - 3056 or snapshot % 8
        or max(collector, snapshot) < min(collector + 3072, snapshot + 3056)
    ):
        raise StackEvidenceError("trace collector and complete snapshot have unbound or aliasing extents")
    result = {}
    for unit, name in OBSERVER_CALLS:
        if unit != WORK_COLLECTOR:
            continue
        symbol = function_symbols(obj).get(name)
        if symbol is None:
            raise StackEvidenceError("trace collector omitted a transitive EV02 function")
        kinds = (0, 1, 3) if name == "micros_test_work_record" else (0,)
        cases = tuple(_trace_collector_case(obj, symbol, sections, collector, snapshot, kind) for kind in kinds)
        result[name] = MachineEffects(
            cases[0].calls, tuple((kind, case.writes) for kind, case in zip(kinds, cases)),
            tuple((kind, case.systems) for kind, case in zip(kinds, cases)),
            (("diagnostic-unit-work-kinds", kinds), trace_frame_record_effects(obj, symbol),
             *((kind, case.dependencies) for kind, case in zip(kinds, cases))),
        )
    return result


TRACE_FAULT_FUNCTIONS = (
    ("kernel/objects/kernel_object_identity.c", "kernel_object_require_initialized"),
    ("kernel/objects/kernel_object_hart.c", "micros_hart_resolve_context"),
    ("kernel/objects/kernel_object_runtime.c", "micros_kernel_object_runtime_hart_from_context"),
    ("kernel/trap/trap_context.c", "trap_frame_is_on_primary_stack"),
    ("kernel/trap/trap_context.c", "trap_read_sscratch"),
    ("kernel/trap/trap_context.c", "trap_read_tp"),
    ("kernel/trap/trap_context.c", "trap_resolve_trap_hart"),
    ("kernel/trap/trap_route_core.c", "micros_trap_interrupt_route_classify"),
    ("kernel/trap/trap_test_hooks.c", "trap_test_hooks_supervisor_faults"),
    ("kernel/tests/trap/trap_test.c", "trap_handle_supervisor_test"),
    ("kernel/trap/trap_interrupt.c", "trap_handle_supervisor_interrupt"),
    ("kernel/trap/trap.c", "micros_trap_dispatch"),
)


def _trace_fault_c_case(obj, symbol, sections, arguments, fields, callees, hart, output):
    from tools.stack_bound_machine import CALLER_SAVED

    flow = function_flow(obj, symbol)
    frame = object_frames(obj, function_symbols(obj))[symbol.name]
    high, low = pcrel_values(obj, symbol, flow)
    for point, value in tuple(high.items()):
        target = value[2]
        if 0 < target[2] < len(obj.sections) and obj.sections[target[2]].flags & 1:
            if target[2] not in sections:
                raise StackEvidenceError("trace fault route borrowed an unlinked data object")
            high[point] = (*value[:2], ("scalar", sections[target[2]] + target[3]))
    registers = [None] * 32
    registers[0], registers[2], registers[4] = ("scalar", 0), ("stack", 0), ("scalar", hart)
    for index, value in enumerate(arguments):
        registers[10 + index] = ("scalar", value)
    state, point, visits, reads, writes, calls, systems = (tuple(registers), ()), 0, Counter(), [], [], [], []
    inventory = {call.offset: call for call in flow.calls}
    while True:
        visits[point] += 1
        if visits[point] > 10:
            raise StackEvidenceError("trace fault context lookup exceeds the actual eight-hart bound")
        if point in inventory:
            call = inventory[point]
            if call.target_symbol == "micros_trap_panic_entry":
                if symbol.name != "micros_trap_dispatch" or state[0][10] != ("scalar", 0) or state[0][14] != ("scalar", arguments[0]):
                    raise StackEvidenceError("trace load-fault panic borrowed its actual hart or complete trap capture")
                calls.append((point, call.target_symbol, state[0][10:15]))
                return ("panic", point), MachineEffects(tuple(calls), tuple(writes), tuple(systems), tuple(reads))
            summary = callees.get(call.target_symbol)
            if summary is None or call.offset not in flow.continuations:
                raise StackEvidenceError("trace load-fault route reaches an unbound call: " + call.target_symbol)
            expected, result, outputs = summary
            actual = state[0][10:10 + len(expected)]
            for value, wanted in zip(actual, expected):
                if wanted == "local-output":
                    if value is None or value[0] != "stack" or not -frame <= value[1] <= -24:
                        raise StackEvidenceError("trace fault resolver output is not one complete caller-owned local cell")
                elif value != ("scalar", wanted):
                    raise StackEvidenceError("trace fault route borrowed a callee input: " + call.target_symbol)
            memory = dict(state[1])
            for argument, value in outputs:
                target = actual[argument]
                if expected[argument] != "local-output":
                    raise StackEvidenceError("trace fault call summary writes a foreign output")
                memory = {address: item for address, item in memory.items()
                          if address + item[0] <= target[1] or target[1] + 8 <= address}
                memory[target[1]] = 8, ("scalar", value)
            changed = list(state[0])
            for register in CALLER_SAVED:
                changed[register] = None
            changed[10] = ("scalar", result)
            calls.append((point, call.target_symbol, actual))
            state = tuple(changed), tuple(sorted(memory.items()))
            point = flow.continuations[point]
            continue
        width, word = flow.instructions[point]
        if is_return(width, word):
            if state[0][2] != ("stack", 0):
                raise StackEvidenceError("trace fault route loses its actual live caller frame")
            return state[0][10], MachineEffects(tuple(calls), tuple(writes), tuple(systems), tuple(reads))
        changed, memory = value_transfer(obj, state, point, width, word, high, low, {})
        changed = list(changed)
        action = memory_action(width, word)
        if action is not None:
            write, size, register, basis, offset = action
            base = high[low[point]][2] if point in low else state[0][basis]
            if base is not None and base[0] == "stack":
                if not -frame <= base[1] + offset <= -size:
                    raise StackEvidenceError("trace fault route escaped its actual compiler frame")
            elif base is not None and base[0] == "scalar":
                address = base[1] + offset
                if write:
                    if output is None or (address, size) != (output, 8) or state[0][register] != ("scalar", hart):
                        raise StackEvidenceError("trace fault route changes protected state or borrows a resolver output")
                    writes.append((point, address, size, state[0][register]))
                else:
                    containing = tuple((start, extent, value) for (start, extent), value in fields.items()
                                       if start <= address and address + size <= start + extent)
                    if len(containing) != 1:
                        raise StackEvidenceError(
                            f"trace fault route has an unbound metadata read: {symbol.name}+{point:#x} {address:#x}/{size}"
                        )
                    start, _, value = containing[0]
                    value = value >> (8 * (address - start)) & ((1 << (size * 8)) - 1)
                    unsigned = width == 4 and word >> 12 & 7 in (4, 5, 6)
                    changed[register] = ("scalar", value if unsigned else signed(value, size * 8))
                    reads.append((point, address, size, value))
            elif (
                write or base is None or base[0] != "symbol" or not 0 < base[2] < len(obj.sections)
                or obj.sections[base[2]].section_type != 1 or not obj.sections[base[2]].flags & 2
                or obj.sections[base[2]].flags & 5
            ):
                raise StackEvidenceError("trace fault route has an unknown pointer effect")
        elif width == 4 and word & 0x7f == 0x73:
            if word & 0x707f != 0x2073 or word >> 15 & 31 or word >> 20 != 0x140 or symbol.name != "trap_read_sscratch":
                raise StackEvidenceError("trace fault route changes supervisor state")
            changed[word >> 7 & 31] = ("scalar", 0)
            systems.append((point, word))
        elif width == 4 and word & 0x7f not in (0x13, 0x1b, 0x33, 0x3b, 0x17, 0x37, 0x63, 0x6f):
            raise StackEvidenceError("trace fault route acquired an unbound instruction")
        value = _prelude_arithmetic(width, word, state[0])
        if value is not None:
            changed[_instruction_destination(word, width)] = value
        changed[0] = ("scalar", 0)
        state = tuple(changed), memory
        successors = flow.successors[point]
        branch = branch_offset(width, word)
        if branch is not None and branch[1]:
            point = successors[0 if branch_taken(width, word, state[0]) else 1]
        elif len(successors) == 1:
            point = successors[0]
        else:
            raise StackEvidenceError("trace fault route lost its finite actual continuation")


def trace_fault_prefix_effects(objects, sections, layout, primary_top):
    runtime = "kernel/objects/kernel_object_runtime.c"
    addresses = {}
    for name in ("kernel_objects", "runtime_ready"):
        symbol, = (value for value in objects[runtime].symbols if value.name == name and value.symbol_type == 1)
        addresses[name] = sections[runtime][symbol.section_index] + symbol.value
    registry, ready = addresses["kernel_objects"], addresses["runtime_ready"]
    frame, output = primary_top - 288, 0x6200000000000000
    result = []
    for slot in range(layout["hart_capacity"]):
        hart = registry + layout["hart_table"] + slot * layout["hart_bytes"]
        fields = {
            (registry, 8): layout["magic"], (ready, 1): 1,
            (frame + 248, 8): 0x100, (frame + 264, 8): 13, (frame + 280, 8): hart,
            (hart + layout["hart_trap_installed"], 1): 1, (hart + layout["hart_hardware_id"], 8): 0,
            (hart + layout["hart_generation"], 4): 1,
            (hart + layout["hart_primary_bottom"], 8): primary_top - 16384,
            (hart + layout["hart_primary_top"], 8): primary_top,
            **{(registry + layout["hart_table"] + index * layout["hart_bytes"] + layout["hart_state"], 4):
               layout["live"] if index == slot else 0 for index in range(layout["hart_capacity"])},
        }
        cases = {
            "kernel_object_require_initialized": ((registry,), 0, ()),
            "micros_hart_resolve_context": ((registry, hart, "local-output"), 0, ((2, hart),)),
            "micros_kernel_object_runtime_hart_from_context": ((hart,), hart, ()),
            "trap_frame_is_on_primary_stack": ((hart, frame), 1, ()),
            "trap_read_sscratch": ((), 0, ()),
            "trap_read_tp": ((), hart, ()),
            "trap_resolve_trap_hart": ((frame,), hart, ()),
            "micros_trap_interrupt_route_classify": ((1, 13), 0, ()),
            "trap_test_hooks_supervisor_faults": ((hart, frame), 0, ()),
            "trap_handle_supervisor_test": ((hart, frame), 0, ()),
            "trap_handle_supervisor_interrupt": ((hart, frame, 0), 0, ()),
        }
        witnesses = []
        proved = {}
        for unit, name in TRACE_FAULT_FUNCTIONS:
            obj, symbol = objects[unit], function_symbols(objects[unit])[name]
            arguments = (frame,) if name == "micros_trap_dispatch" else tuple(
                output if value == "local-output" else value for value in cases[name][0]
            )
            returned, effects = _trace_fault_c_case(
                obj, symbol, sections[unit], arguments, fields, proved, hart,
                output if name == "micros_hart_resolve_context" else None,
            )
            if name == "micros_trap_dispatch":
                if returned[0] != "panic":
                    raise StackEvidenceError("actual supervisor load fault can bypass the retained panic entry")
            elif returned != ("scalar", cases[name][1]):
                raise StackEvidenceError("trace fault route changed its actual return: " + name)
            if name == "micros_hart_resolve_context" and len(effects.writes) != 1:
                raise StackEvidenceError("trace fault context lookup omitted its exact selected output")
            if name != "micros_hart_resolve_context" and effects.writes:
                raise StackEvidenceError("trace fault routing mutates nonlocal state")
            if name == "trap_read_sscratch" and len(effects.systems) != 1:
                raise StackEvidenceError("trace fault routing lost its actual cleared stack-anchor CSR")
            witnesses.append((unit, name, returned, effects, trace_frame_record_effects(obj, symbol)))
            if name in cases:
                proved[name] = cases[name]
        result.append((slot, hart, frame, tuple(witnesses)))
    return tuple(result)


def _uart_callees(name, test_counter, terminal, terminal_test, trace_test):
    if any(type(flag) is not bool for flag in (test_counter, terminal, terminal_test, trace_test)) or (
        (terminal_test or trace_test) and not terminal
        or sum((test_counter, terminal_test, trace_test)) > 1
    ):
        raise StackEvidenceError("UART effect has an unknown or overlapping observation mode")
    callees = CALLEES[name]
    if name in ("uart_write_character", "uart_flush"):
        if terminal:
            callees += ("micros_panic_terminate_no_console",)
        if terminal_test or trace_test:
            callees += ("micros_test_work_record",)
        if terminal_test:
            callees += ("micros_terminal_test_lsr",)
        elif trace_test and name == "uart_write_character":
            callees += ("micros_panic_trace_test_lsr",)
    if name == "uart_write_character":
        if test_counter:
            callees += ("micros_diagnostic_test_observe_thr",)
        if terminal_test:
            callees += ("micros_terminal_test_thr",)
        elif trace_test:
            callees += ("micros_panic_trace_test_thr",)
    if name == "uart_panic_seize" and terminal_test:
        callees += ("micros_terminal_test_seizure",)
    elif name == "uart_panic_seize" and trace_test:
        callees += ("micros_panic_trace_test_seizure",)
    return callees


def _uart_repair_effects(obj, symbol, console, repairs, *, terminal_test, trace_test):
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
            **({"micros_panic_trace_test_seizure": None} if trace_test else {}),
        })
        if (result, value) != ("return", ("scalar", int(code != 0))):
            raise StackEvidenceError("terminal UART repaired-status return is false or unbound")
        relations.append((code, value, trace))
    return ("console-repair", tuple(repairs), tuple(relations))


def _uart_seizure_effects(obj, symbol, writes, repairs, console, *, terminal, terminal_test, trace_test):
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
        dependencies += (_uart_repair_effects(obj, symbol, console, repairs, terminal_test=terminal_test, trace_test=trace_test),)
    elif repairs:
        raise StackEvidenceError("old UART seizure has an undeclared console repair")
    return dependencies


def uart_machine_effects(obj, symbol, *, test_counter=False, terminal=False, terminal_test=False, trace_test=False, console=None):
    symbols = function_symbols(obj)
    if symbol.name not in CALLEES or symbols.get(symbol.name) != symbol:
        raise StackEvidenceError("unknown or borrowed UART effect function")
    frames = object_frames(obj, symbols)
    if symbol.name not in frames:
        raise StackEvidenceError("UART effects lack their actual compiler frame")
    require_c_frame_movement(obj, symbol, frames[symbol.name])
    flow = function_flow(obj, symbol)
    runtime_calls, hooks = runtime_call_inventory(obj, symbol)
    callees = _uart_callees(symbol.name, test_counter, terminal, terminal_test, trace_test)
    if (
        Counter(call.target_symbol for call in runtime_calls) != Counter(callees)
        or any(call.kind is not EdgeKind.CALL for call in runtime_calls)
    ):
        raise StackEvidenceError("UART effect call inventory is incomplete or unbound")
    inputs = register_inputs(obj, symbol)
    writes, systems, fences, repairs, lsr_reads = [], [], [], [], []
    for point, (width, word) in flow.instructions.items():
        if point not in inputs:
            raise StackEvidenceError("UART effect has an unbound machine entry/instruction")
        action = memory_action(width, word)
        if action is not None and not action[0] and action[1] == 1 and action[4] == 5:
            if inputs[point][action[3]] == ("scalar", UART_BASE):
                lsr_reads.append(point)
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
        ) + (
            ("micros_panic_trace_test_thr",) if trace_test else ()
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
    if trace_test and symbol.name in ("uart_write_character", "uart_flush"):
        if len(lsr_reads) != 1:
            raise StackEvidenceError("trace UART wait lost its one actual status-read instruction")
        if symbol.name == "uart_write_character":
            observation, = (call for call in runtime_calls if call.target_symbol == "micros_panic_trace_test_lsr")
            if (
                inputs[observation.offset][10] != ("field", ("scalar", UART_BASE), 5, 1)
                or observation.offset in reachable_instructions(flow, excluded=lsr_reads[0] + flow.instructions[lsr_reads[0]][0])
            ):
                raise StackEvidenceError("trace THRE filter does not consume its actual completed UART status read")
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
            trace_test=trace_test,
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


def terminal_srst_effects(obj, symbol, *, test_observer=False, trace_observer=False):
    if (
        symbol.name != "sbi_system_reset" or type(test_observer) is not bool or type(trace_observer) is not bool
        or test_observer and trace_observer
    ):
        raise StackEvidenceError("terminal SRST proof has a foreign function or observation mode")
    observer = ("micros_terminal_test_reset",) if test_observer else (
        ("micros_panic_trace_test_reset",) if trace_observer else ()
    )
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
    if width == 4 and word & 0xfe00007f == 0x02000033 and word >> 12 & 7 in (0, 3, 5, 7):
        left, right = registers[word >> 15 & 31], registers[word >> 20 & 31]
        if left is None or right is None or left[0] != "scalar" or right[0] != "scalar":
            return None
        operation = word >> 12 & 7
        if operation == 0:
            return "scalar", signed(left[1] * right[1], 64)
        left, right = left[1] & 0xffffffffffffffff, right[1] & 0xffffffffffffffff
        if operation == 3:
            value = (left * right) >> 64
        elif operation == 5:
            value = left // right if right else 0xffffffffffffffff
        else:
            value = left % right if right else left
        return "scalar", signed(value, 64)
    if width == 2 and word & 3 == 2 and word >> 13 == 0:
        value = registers[word >> 7 & 31]
        shift = (word >> 12 & 1) << 5 | (word >> 2 & 31)
        if value is not None and value[0] == "scalar":
            return "scalar", signed(value[1] << shift, 64)
        return None
    if width == 2 and word & 3 == 1 and word >> 13 == 4 and word >> 10 & 3 in (0, 1):
        value = registers[8 + (word >> 7 & 7)]
        if value is not None and value[0] == "scalar":
            shift = (word >> 12 & 1) << 5 | (word >> 2 & 31)
            source = signed(value[1], 64) if word >> 10 & 3 == 1 else value[1] & 0xffffffffffffffff
            return "scalar", signed(source >> shift, 64)
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


def prelude_states(obj, symbol, sections, *, arguments=(), results=None, fields=None, external_values=None, csr_values=None):
    flow = function_flow(obj, symbol)
    high, low = pcrel_values(obj, symbol, flow)
    for point, value in tuple(high.items()):
        target = value[2]
        if target[2] == 0 and target[1] in (external_values or {}):
            high[point] = (*value[:2], ("scalar", external_values[target[1]] + target[3]))
        elif 0 < target[2] < len(obj.sections) and obj.sections[target[2]].flags & 1:
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
        if (
            width == 4 and word & 0x7f == 0x73 and word >> 12 & 7 == 2 and word >> 15 & 31 == 0
            and word >> 20 in (csr_values or {})
        ):
            changed[word >> 7 & 31] = ("scalar", csr_values[word >> 20])
            changed[0] = ("scalar", 0)
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

"""Source/object/linked authority for the finite ADR-0086 C qualifier."""

from collections import Counter
from dataclasses import dataclass
import hashlib
from pathlib import Path
import re

from tests.host.c_source_records import (
    c_lexical_text, function_records, matching_lexical, tokens,
)
from tests.host.work_count_lexical import compound_layout, positional_tokens
from tools.stack_artifact_files import read_regular
from tools.stack_bound_console import MAGIC, console_machine_proof
from tools.stack_bound_context import ContextBindings
from tools.stack_bound_fatal_effects import CALLEES, sbi_machine_effects, uart_machine_effects
from tools.stack_bound_fatal_firmware import resolve_default_firmware
from tools.stack_bound_fatal_machine import (
    require_srst_registers, result_zero_gate, zero_success_returns,
)
from tools.stack_bound_flow import (
    function_flow, normal_return_dominated_calls, reachable_instructions,
)
from tools.stack_bound_artifacts import object_frames
from tools.stack_bound_bindings import reference_source
from tools.stack_bound_link import FunctionLocation, join_function_locations
from tools.stack_bound_objects import function_symbols, load_object, section_bytes
from tools.stack_bound_observer import HOOK_SYMBOLS, observer_frame_evidence, runtime_call_inventory
from tools.stack_bound_pointer import memory_action
from tools.stack_bound_relocations import verify_linked_object
from tools.stack_bound_types import EdgeKind, FunctionKey, StackEvidenceError
from tools.stack_bound_values import register_inputs
from tools.user_elf_parse import load_elf


UART = "arch/riscv64/uart.c"
CONSOLE = "kernel/tty/uart_console_core.c"
REPORT = "kernel/diagnostic/panic.c"
SBI = "arch/riscv64/sbi.c"
SOURCE_UNITS = (
    UART, CONSOLE, REPORT, SBI, "arch/riscv64/interrupt.h",
    "arch/riscv64/platform.h", "arch/riscv64/panic_context.h",
    "kernel/tty/uart_console_core.h", "include/micros/panic.h",
)
PRESERVERS = (
    (UART, "uart_write"), (UART, "uart_write_hex64"),
    (UART, "uart_write_character"), (UART, "uart_flush"),
    (CONSOLE, "micros_uart_console_output_allowed"),
    (CONSOLE, "micros_uart_console_validate"), (SBI, "sbi_system_reset"),
)
CALL_TARGETS = {
    **{(UART, name): tuple(set(targets)) for name, targets in CALLEES.items()},
    (REPORT, "micros_panic_seize"): (
        "riscv_irq_save", "riscv_external_interrupt_disable", "micros_plic_panic_disable",
        "micros_tty_handoff_runtime_panic", "uart_panic_seize", "uart_write", "uart_flush",
    ),
    (REPORT, "micros_panic_report"): (
        "micros_panic_seize", "uart_write", "uart_write_hex64", "uart_flush", "sbi_system_reset",
    ),
    (CONSOLE, "micros_uart_console_output_allowed"): ("micros_uart_console_validate",),
    (CONSOLE, "micros_uart_console_panic"): (),
    (CONSOLE, "micros_uart_console_validate"): (),
    (SBI, "sbi_set_timer"): (), (SBI, "sbi_system_reset"): (),
}


@dataclass(frozen=True)
class SourceCall:
    function: FunctionKey
    offset: int
    target: str
    source: str
    primitive: tuple
    compounds: tuple
    occurrence: int


@dataclass(frozen=True)
class FatalFunction:
    function: FunctionKey
    frame: int
    object_sha256: str
    calls: tuple
    writes: tuple = ()
    systems: tuple = ()


@dataclass(frozen=True)
class ConsoleObject:
    symbol: str
    address: int
    size: int
    object_name: str
    section: str


@dataclass(frozen=True)
class FatalContextProof:
    bindings: ContextBindings
    console: ConsoleObject
    establishment: tuple[SourceCall, ...]
    preservation: tuple[FatalFunction, ...]
    guard: SourceCall
    sbi_inventory: tuple[FatalFunction, ...]
    firmware: object
    compiler_hooks: tuple = ()


def owned_file(directory, relative):
    path = Path(directory)
    for part in Path(relative).parts:
        path /= part
        if path.is_symlink():
            raise StackEvidenceError("fatal proof has a linked source/artifact owner")
    return path


def definition_line(source, name):
    lexical = c_lexical_text(source)
    lines = []
    for match in re.finditer(r"\b" + re.escape(name) + r"\s*\(", lexical):
        opening = lexical.find("(", match.start())
        closing = matching_lexical(lexical, opening)
        following = closing + 1
        while following < len(lexical) and lexical[following].isspace():
            following += 1
        if following < len(lexical) and lexical[following] == "{":
            lines.append(lexical[:match.start()].count("\n") + 1)
    if len(lines) != 1:
        raise StackEvidenceError("fatal source has no unique complete function definition")
    return lines[0]


def source_calls(root, key, origin, machine_calls):
    reference = reference_source(str(root), origin.source)
    actual = read_regular(owned_file(root, origin.source))[0].decode("utf-8")
    expected = function_records(reference).get(key.symbol)
    record = function_records(actual).get(key.symbol)
    if (
        expected is None or record is None or origin.line != definition_line(reference, key.symbol)
        or record.guards != expected.guards
        or tokens(record.signature) != tokens(expected.signature)
        or tokens(record.body) != tokens(expected.body)
    ):
        raise StackEvidenceError("fatal function borrowed a source/body/line origin")
    body = positional_tokens(expected.body)
    values = tuple(token.value for token in body)
    stacks, closings = compound_layout(values)
    names = CALL_TARGETS.get((key.translation_unit, key.symbol))
    if names is None or any(call.target_symbol is None for call in machine_calls):
        raise StackEvidenceError("fatal qualifier has an unbound indirect call")
    result, counts = [], Counter()
    lexical = c_lexical_text(expected.body)
    for index, token in enumerate(body[:-1]):
        target = "micros_panic_entry" if token.value == "MICROS_PANIC" else token.value
        if target not in names or values[index + 1] != "(":
            continue
        closing = matching_lexical(lexical, body[index + 1].start)
        fragment = tokens(expected.body[token.start:closing + 1])
        compounds = tuple(
            values[opening + 1:closings[opening]] for opening in stacks[index]
        )
        result.append((target, fragment, compounds, counts[target]))
        counts[target] += 1
    if tuple(row[0] for row in result) != tuple(call.target_symbol for call in machine_calls):
        raise StackEvidenceError("fatal source and complete machine-call occurrence inventories differ")
    return tuple(SourceCall(
        key, call.offset, target, origin.source, fragment, compounds, occurrence,
    ) for call, (target, fragment, compounds, occurrence) in zip(machine_calls, result))


def reporter_effects(obj, symbol):
    flow, inputs = function_flow(obj, symbol), register_inputs(obj, symbol)
    writes, systems = [], []
    for point, (width, word) in flow.instructions.items():
        if point not in inputs:
            raise StackEvidenceError("fatal reporter has an unowned machine entry")
        action = memory_action(width, word)
        if action is not None and action[0]:
            _, size, register, basis, offset = action
            base, value = inputs[point][basis], inputs[point][register]
            if base is not None and base[0] == "stack":
                continue
            if (
                symbol.name != "micros_panic_seize" or size != 1 or offset != 0
                or base is None or base[0:2] != ("symbol", "panic_seizure_failure_emitted")
                or value != ("scalar", 1)
            ):
                raise StackEvidenceError("fatal formatting mutates an unbound state/alias")
            writes.append((point, size, offset, value))
        if width == 4 and word & 0x7f == 0x73:
            if symbol.name != "micros_panic_report" or word != 0x10500073:
                raise StackEvidenceError("fatal formatting changes interrupt/firmware state")
            systems.append((point, word))
        elif action is None and width == 4 and word & 0x7f not in (
            0x13, 0x1b, 0x33, 0x3b, 0x17, 0x37, 0x63, 0x67,
        ):
            raise StackEvidenceError("fatal formatting has an unmodeled machine effect")
    if (len(writes), len(systems)) != (
        (1, 0) if symbol.name == "micros_panic_seize" else (0, 1)
    ):
        raise StackEvidenceError("fatal formatting write/system inventory is incomplete")
    return tuple(writes), tuple(systems)


def derive_fatal_context(root, directory, catalog, *, qemu):
    root, directory = Path(root), Path(directory)
    cache = read_regular(owned_file(directory, "CMakeCache.txt"))[0].decode("utf-8").splitlines()
    if [line for line in cache if line.startswith("MICROS_QEMU:")] != [
        "MICROS_QEMU:FILEPATH=" + str(qemu),
    ]:
        raise StackEvidenceError("fatal firmware resolver borrowed another configured QEMU")
    observed = [line.partition("=")[2] for line in cache if line.startswith(
        "MICROS_STACK_BUILD_MODE:"
    )] == ["observed"]
    for unit in SOURCE_UNITS:
        actual = read_regular(owned_file(root, unit))[0].decode("utf-8")
        if tokens(actual) != tokens(reference_source(str(root), unit)):
            raise StackEvidenceError("fatal source/type/CSR contract changed from its fixed stage")
    frames = {row.function: row for row in catalog.frames}
    definitions = {row.function: row for row in catalog.definitions}
    names = dict(catalog.objects)
    origins = {row.function: row for row in catalog.origins}
    if (
        any(len(mapping) != len(rows) for mapping, rows in (
            (frames, catalog.frames), (definitions, catalog.definitions),
            (names, catalog.objects), (origins, catalog.origins),
        )) or any(set(mapping) != set(frames) for mapping in (definitions, names, origins))
    ):
        raise StackEvidenceError("fatal catalog lacks its complete frame/object/origin inventory")
    identities = {(key.variant, key.image) for key in frames}
    if len(identities) != 1:
        raise StackEvidenceError("fatal qualifier borrowed another executable/variant")
    variant, image_name = next(iter(identities))
    if image_name != "kernel/micros.elf":
        raise StackEvidenceError("fatal qualifier is not a kernel image")
    image = load_elf(owned_file(directory, image_name))
    text = read_regular(owned_file(directory, "kernel/micros.map"))[0].decode("utf-8")
    objects, sections, records, flows, sources, hook_sites = {}, {}, {}, {}, {}, []
    required = (
        *PRESERVERS, (UART, "uart_panic_seize"), (CONSOLE, "micros_uart_console_panic"),
        (REPORT, "micros_panic_seize"), (REPORT, "micros_panic_report"),
        (SBI, "sbi_set_timer"),
        *((UART, name) for name in ("riscv_irq_save", "riscv_external_interrupt_disable", "riscv_mmio_fence")),
    )
    for unit, name in required:
        key = FunctionKey(variant, image_name, unit, name)
        relative = "CMakeFiles/micros.dir/" + unit + ".obj"
        if key not in frames or names[key] != relative:
            raise StackEvidenceError("fatal qualifier lacks an exact required function/object owner")
        if relative not in objects:
            obj = load_object(owned_file(directory, relative))
            objects[relative] = obj
            sections[relative] = verify_linked_object(obj, relative, image, text)
        obj = objects[relative]
        symbol = function_symbols(obj).get(name)
        sizes = object_frames(obj, function_symbols(obj))
        frame, definition = frames[key], definitions[key]
        if (
            symbol is None or frame.classification != "static" or sizes.get(name) != frame.bytes_used
            or definition.size != symbol.size or definition.global_linkage != (symbol.binding != 0)
        ):
            raise StackEvidenceError("fatal compiler frame or linked definition differs from its object")
        location = FunctionLocation(
            key, relative, obj.sections[symbol.section_index].name, symbol.value, symbol.size,
        )
        if join_function_locations((location,), text, image.symbols).get(key) != definition.address:
            raise StackEvidenceError("fatal function has a borrowed linked address/range")
        flow = function_flow(obj, symbol)
        observed = tuple(sorted((
            (call.offset, call.targets[0].symbol, call.kind) for call in catalog.calls
            if call.caller == key and len(call.targets) == 1
        ), key=lambda row: row[0])) + tuple(
            (call.offset, call.target_symbol, call.kind) for call in catalog.pending if call.caller == key
        )
        if tuple(sorted(observed)) != tuple(sorted(
            (call.offset, call.target_symbol, call.kind) for call in flow.calls
        )):
            raise StackEvidenceError("fatal catalog omitted or changed an actual machine-call site")
        runtime, hooks = runtime_call_inventory(obj, symbol) if observed else (flow.calls, ())
        source = source_calls(root, key, origins[key], runtime)
        hook_sites.extend((key, hook) for hook in hooks)
        records[key] = obj, symbol, frame
        flows[key], sources[key] = flow, source
    console_obj = objects["CMakeFiles/micros.dir/" + UART + ".obj"]
    console_symbols = tuple(symbol for symbol in console_obj.symbols if symbol.name == "uart_console_state")
    if len(console_symbols) != 1:
        raise StackEvidenceError("fatal fact has no unique actual UART console")
    console_symbol, = console_symbols
    console_section = console_obj.sections[console_symbol.section_index]
    relative = "CMakeFiles/micros.dir/" + UART + ".obj"
    if (
        console_symbol.binding != 0 or console_symbol.symbol_type != 1
        or console_symbol.size != 16 or console_symbol.value != 0
        or console_section.name != ".data.uart_console_state" or console_section.flags != 3
        or section_bytes(console_obj, console_section) != (
            MAGIC.to_bytes(8, "little") + (1).to_bytes(4, "little") + bytes(4)
        ) or console_symbol.section_index not in sections[relative]
    ):
        raise StackEvidenceError("fatal fact borrowed another console layout/initial state")
    console = ConsoleObject(
        console_symbol.name, sections[relative][console_symbol.section_index], 16,
        relative, console_section.name,
    )
    console_machine_proof(root, objects["CMakeFiles/micros.dir/" + CONSOLE + ".obj"])
    preserving = []
    for unit, name in PRESERVERS:
        key = FunctionKey(variant, image_name, unit, name)
        obj, symbol, frame = records[key]
        effects = (
            uart_machine_effects(obj, symbol) if unit == UART else
            sbi_machine_effects(obj, symbol) if unit == SBI else None
        )
        preserving.append(FatalFunction(
            key, frame.bytes_used, hashlib.sha256(obj.data).hexdigest(), sources[key],
            () if effects is None else effects.writes, () if effects is None else effects.systems,
        ))
    if observed:
        name = "CMakeFiles/micros.dir/tests/stack/observer.S.obj"
        obj = load_object(owned_file(directory, name))
        observer = observer_frame_evidence(root, obj, variant, image_name, True)
        verify_linked_object(obj, name, image, text)
        locations = tuple(FunctionLocation(
            frame.function, name,
            obj.sections[function_symbols(obj)[frame.function.symbol].section_index].name,
            function_symbols(obj)[frame.function.symbol].value,
            function_symbols(obj)[frame.function.symbol].size,
        ) for frame in observer.frames)
        if len(join_function_locations(locations, text, image.symbols)) != 2:
            raise StackEvidenceError("fatal preservation lacks both exact linked observer leaves")
        preserving.extend(FatalFunction(
            frame.function, 0, hashlib.sha256(obj.data).hexdigest(), (),
        ) for frame in observer.frames)
    uart_key = FunctionKey(variant, image_name, UART, "uart_panic_seize")
    obj, symbol, _ = records[uart_key]
    uart_machine_effects(obj, symbol)
    console_call, = (call for call in flows[uart_key].calls if call.target_symbol == "micros_uart_console_panic")
    inputs = register_inputs(obj, symbol)
    if inputs[console_call.offset][10] != (
        "symbol", console_symbol.name, console_symbol.section_index, console_symbol.value,
    ):
        raise StackEvidenceError("UART seizure borrowed a same-shaped foreign console argument")
    zero_success_returns(obj, symbol, console_call.offset)
    establishment = []
    for name, producer_name in (
        ("micros_panic_seize", "uart_panic_seize"),
        ("micros_panic_report", "micros_panic_seize"),
    ):
        key = FunctionKey(variant, image_name, REPORT, name)
        obj, symbol, _ = records[key]
        reporter_effects(obj, symbol)
        producer, = (call for call in flows[key].calls if call.target_symbol == producer_name)
        dominated = normal_return_dominated_calls(obj, symbol, producer.offset)
        declared = tuple(row for row in sources[key] if row.offset > producer.offset)
        if observed:
            declared += tuple(SourceCall(
                key, hook.offset, hook.target_symbol, origins[key].source,
                ("compiler-hook", hook.target_symbol, "self", "entry-return-address"), (), 0,
            ) for caller, hook in hook_sites if caller == key and hook.offset > producer.offset)
        if dominated != frozenset(row.offset for row in declared):
            raise StackEvidenceError("fatal post-transition source/machine dominance inventory differs")
        allowed = {"uart_write", "uart_write_hex64", "uart_flush", "sbi_system_reset", *HOOK_SYMBOLS}
        if any(row.target not in allowed for row in declared):
            raise StackEvidenceError("fatal ready continuation reaches an unbound preservation call")
        establishment.extend(declared)
    guard_key = FunctionKey(variant, image_name, UART, "uart_write")
    obj, symbol, _ = records[guard_key]
    predicate, = (call for call in flows[guard_key].calls if call.target_symbol == "micros_uart_console_output_allowed")
    if register_inputs(obj, symbol)[predicate.offset][10] != (
        "symbol", console_symbol.name, console_symbol.section_index, console_symbol.value,
    ):
        raise StackEvidenceError("ready ownership guard borrowed another console argument")
    gate = result_zero_gate(obj, symbol, predicate.offset, bits=1)
    guard, = (row for row in sources[guard_key] if row.target == "micros_panic_entry")
    if (
        guard.offset not in reachable_instructions(flows[guard_key], start=gate.zero)
        or guard.offset in reachable_instructions(flows[guard_key], start=gate.nonzero)
        or guard.offset in reachable_instructions(flows[guard_key], excluded=gate.branch)
    ):
        raise StackEvidenceError("infeasible UART panic edge bypasses its exact predicate/result guard")
    sbi_inventory = []
    for key, relative in catalog.objects:
        if relative not in objects:
            objects[relative] = load_object(owned_file(directory, relative))
        obj = objects[relative]
        symbol = function_symbols(obj)[key.symbol]
        flow = function_flow(obj, symbol)
        ecalls = tuple(point for point, instruction in flow.instructions.items() if instruction == (4, 0x73))
        if not ecalls:
            continue
        if key.translation_unit != SBI or key.symbol not in ("sbi_set_timer", "sbi_system_reset") or len(ecalls) != 1:
            raise StackEvidenceError("fatal firmware scope has an unknown C SBI/SSE ecall")
        if key.symbol == "sbi_system_reset":
            require_srst_registers(obj, symbol)
        else:
            values = register_inputs(obj, symbol)[ecalls[0]]
            if (values[16], values[17]) != (("scalar", 0), ("scalar", 0x54494d45)):
                raise StackEvidenceError("TIME wrapper changed the closed SBI extension inventory")
        sbi_machine_effects(obj, symbol)
        sbi_inventory.append(FatalFunction(
            key, frames[key].bytes_used, hashlib.sha256(obj.data).hexdigest(), sources[key],
            systems=tuple((point, 0x73) for point in ecalls),
        ))
    report_key = FunctionKey(variant, image_name, REPORT, "micros_panic_report")
    reset, = (row for row in sources[report_key] if row.target == "sbi_system_reset")
    values = register_inputs(*records[report_key][:2])[reset.offset]
    if (values[10], values[11]) != (("scalar", 0), ("scalar", 1)):
        raise StackEvidenceError("ready reset site changed its scalar shutdown/failure operands")
    firmware = resolve_default_firmware(qemu)
    return FatalContextProof(
        ContextBindings(
            tuple((row.function, row.offset) for row in establishment),
            tuple(row.function for row in preserving), (guard.function, guard.offset),
        ),
        console, tuple(establishment), tuple(preserving), guard, tuple(sbi_inventory), firmware,
        tuple(hook_sites),
    )

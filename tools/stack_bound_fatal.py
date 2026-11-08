"""Source/object/linked authority for the finite ADR-0086 C qualifier."""

from collections import Counter
from dataclasses import dataclass, replace
import hashlib
import json
from pathlib import Path
import re
import shlex

from tests.host.c_source_records import (
    c_lexical_text, function_records, matching_lexical, tokens,
)
from tests.host.work_count_lexical import compound_layout, positional_tokens
from tests.host.diagnostic_history_source import (
    FATAL_SOURCE_CONTRACTS, HISTORY_LAYOUT, checked_fatal_function,
    THR_OBSERVATION, diagnostic_header_source, effective_thr_body, project_fatal_report, project_fatal_uart,
)
from tests.host.terminal_failure_source import (
    CONTROL_SOURCES, PANIC_ENTRY_WRAPPER, effective_terminal_body, project_terminal_runtime,
)
from tests.host.panic_trace_source import (
    FATAL_EFFECT_CONTRACT as TRACE_CONTRACT, RECIPE_ADMISSION as TRACE_RECIPE,
    effective_trace_body, fixed_source as trace_reference_source, project_trace_runtime, trace_function_records,
)
from tools.stack_artifact_files import read_regular
from tools.stack_bound_console import MAGIC, console_machine_proof
from tools.stack_bound_context import ContextBindings, Qualifier
from tools.stack_bound_fatal_effects import (
    CALLEES, DIAGNOSTIC_READONLY_CALLEES, diagnostic_counter_effects,
    diagnostic_readonly_effects, diagnostic_recording_guard, diagnostic_selector_effects,
    MachineEffects, OBSERVER_CALLS, PRELUDE_CALLS, TERMINAL_OBSERVER, WORK_COLLECTOR,
    panic_control_effects, panic_runtime_effects, prelude_machine_effects, prelude_states,
    sbi_machine_effects, terminal_reporter_effects, terminal_srst_effects, uart_machine_effects,
    terminal_observer_effects,
    TRACE_CORE_CALLS, TRACE_FAULT_FUNCTIONS, TRACE_FIXTURE, TRACE_FIXTURE_CALLS, TRACE_FIXTURE_OBJECTS,
    TRACE_RESOLVER_CALLS, TRACE_TARGET_CALLS,
    trace_accessor_effects, trace_collector_effects, trace_core_effects,
    trace_fault_prefix_effects, trace_fixture_alias_effects, trace_fixture_effects, trace_frame_record_effects,
    trace_object_layout, trace_resolver_effects, trace_target_effects,
)
from tools.stack_bound_fatal_firmware import resolve_default_firmware
from tools.stack_bound_fatal_machine import (
    require_srst_registers, result_zero_gate, zero_success_returns,
)
from tools.stack_bound_flow import (
    function_flow, normal_return_dominated_calls, reachable_instructions,
)
from tools.stack_bound_artifacts import object_frames
from tools.stack_bound_assembly import assembly_object_evidence, trace_trap_effects
from tools.stack_bound_bindings import reference_source
from tools.stack_bound_link import FunctionLocation, join_function_locations
from tools.stack_bound_machine import is_return
from tools.stack_bound_objects import function_symbols, load_object, section_bytes
from tools.stack_bound_observer import HOOK_SYMBOLS, observer_frame_evidence, runtime_call_inventory
from tools.stack_bound_pointer import memory_action
from tools.stack_bound_relocations import linked_offset, verify_linked_object
from tools.stack_bound_types import EdgeKind, FunctionKey, StackEvidenceError
from tools.stack_bound_values import pcrel_values, register_inputs
from tools.user_elf_parse import load_elf


UART = "arch/riscv64/uart.c"
CONSOLE = "kernel/tty/uart_console_core.c"
REPORT = "kernel/diagnostic/panic.c"
SBI = "arch/riscv64/sbi.c"
HISTORY = "kernel/diagnostic/diagnostic_history.c"
HISTORY_CORE = "kernel/diagnostic/diagnostic_history_core.c"
HISTORY_REPORT = "kernel/diagnostic/panic_history.c"
THR_COUNTER = "tests/qemu/diagnostic_history_kernel.c"
CONTROL = "kernel/diagnostic/panic_control.c"
CONTROL_CORE = "kernel/diagnostic/panic_control_core.c"
PLIC = "kernel/platform/plic.c"
PLIC_CORE = "kernel/platform/plic_core.c"
TTY_RUNTIME = "kernel/tty/tty_handoff_runtime.c"
TTY_CONTEXT = "kernel/tty/tty_handoff_runtime_state.c"
TTY_CORE = "kernel/tty/tty_handoff_core.c"
TRACE_CORE = "kernel/diagnostic/panic_trace_core.c"
TRACE = "kernel/diagnostic/panic_trace.c"
TRACE_SELECTED = TRACE_CONTRACT["selected_resolvers"]
TRACE_FIXTURE_GETTERS = (
    ("kernel/objects/kernel_object_runtime.c", "micros_kernel_object_runtime_test_registry"),
    ("kernel/objects/kernel_object_runtime.c", "micros_kernel_object_runtime_boot_hart"),
)
TRACE_PRESERVERS = (
    *((TRACE_CORE, name) for name in TRACE_CORE_CALLS),
    *((TRACE, name) for name in TRACE_TARGET_CALLS),
    *TRACE_SELECTED,
)
SOURCE_UNITS = (
    UART, CONSOLE, REPORT, SBI, "arch/riscv64/interrupt.h",
    "arch/riscv64/platform.h", "arch/riscv64/panic_context.h",
    "kernel/tty/uart_console_core.h", "include/micros/panic.h",
    *CONTROL_SOURCES, PLIC, PLIC_CORE, TTY_RUNTIME, TTY_CONTEXT, TTY_CORE,
    "kernel/platform/plic.h", "kernel/platform/plic_core.h",
    "kernel/tty/tty_handoff_runtime.h", "kernel/tty/tty_handoff_runtime_internal.h", "kernel/tty/tty_handoff_core.h",
)
PRELUDE_SUPPORT = (
    (PLIC, "micros_plic_initialize"), (PLIC, "plic_register"),
    (PLIC_CORE, "micros_plic_controller_initialize"),
)
PRESERVERS = (
    *TRACE_PRESERVERS,
    (UART, "uart_write"), (UART, "uart_write_hex64"),
    (UART, "uart_write_direct"), (UART, "uart_write_hex64_direct"), (UART, "uart_panic_mode"),
    (UART, "uart_write_character"), (UART, "uart_flush"),
    (CONSOLE, "micros_uart_console_output_allowed"),
    (CONSOLE, "micros_uart_console_validate"), (SBI, "sbi_system_reset"),
    (HISTORY_CORE, "micros_diagnostic_history_valid"),
    (HISTORY, "micros_diagnostic_panic_size"), (HISTORY, "micros_diagnostic_panic_byte_prevalidated"),
    (HISTORY_REPORT, "write_history_byte"), (HISTORY_REPORT, "micros_diagnostic_panic_report"),
    (CONTROL_CORE, "micros_panic_control_enter"), (CONTROL_CORE, "micros_panic_control_request_reset"),
    (CONTROL, "micros_panic_claim_first"), (CONTROL, "micros_panic_claim_reset"),
    (CONTROL, "micros_panic_terminate_no_console"), (CONTROL, "micros_panic_halt"),
    (REPORT, "micros_panic_report"), (REPORT, "panic_report_claimed"), (REPORT, "micros_panic_seize"),
    (UART, "uart_panic_seize"), (CONSOLE, "micros_uart_console_panic"),
    *((unit, name) for unit, name in PRELUDE_CALLS if (unit, name) not in PRELUDE_SUPPORT),
    *((unit, name) for unit, names in (
        (UART, ("riscv_irq_save", "riscv_external_interrupt_disable", "riscv_mmio_fence")),
        (CONTROL, ("riscv_irq_save", "riscv_external_interrupt_disable")),
        (REPORT, ("riscv_irq_save", "riscv_external_interrupt_disable")),
        (PLIC, ("riscv_mmio_fence",)),
    ) for name in names),
)
CALL_TARGETS = {
    **{(TRACE_CORE, name): tuple(dict.fromkeys(callees)) for name, callees in TRACE_CORE_CALLS.items()},
    **{(TRACE, name): tuple(dict.fromkeys(callees)) for name, callees in TRACE_TARGET_CALLS.items()},
    **{(unit, name): TRACE_RESOLVER_CALLS[name] for unit, name in TRACE_SELECTED},
    **{(UART, name): tuple(dict.fromkeys(targets)) for name, targets in CALLEES.items()},
    (REPORT, "micros_panic_seize"): (
        "riscv_irq_save", "riscv_external_interrupt_disable", "micros_plic_panic_disable",
        "micros_tty_handoff_runtime_panic", "uart_panic_seize", "uart_write", "uart_flush",
    ),
    (REPORT, "panic_report_claimed"): (
        "micros_panic_seize", "uart_write", "uart_write_hex64", "uart_flush", "sbi_system_reset",
        "micros_diagnostic_panic_report", "micros_panic_trace_report",
    ),
    (REPORT, "micros_panic_report"): (
        "micros_panic_claim_first", "micros_panic_terminate_no_console", "panic_report_claimed",
    ),
    (CONSOLE, "micros_uart_console_output_allowed"): ("micros_uart_console_validate",),
    (CONSOLE, "micros_uart_console_panic"): (),
    (CONSOLE, "micros_uart_console_validate"): (),
    (SBI, "sbi_set_timer"): (),
    (SBI, "sbi_system_reset"): ("micros_panic_claim_reset", "micros_panic_halt"),
    (CONTROL_CORE, "micros_panic_control_enter"): (),
    (CONTROL_CORE, "micros_panic_control_request_reset"): (),
    (CONTROL, "micros_panic_claim_first"): (
        "riscv_irq_save", "riscv_external_interrupt_disable", "micros_panic_control_enter", "micros_panic_halt",
    ),
    (CONTROL, "micros_panic_claim_reset"): (
        "riscv_irq_save", "riscv_external_interrupt_disable", "micros_panic_control_request_reset", "micros_panic_halt",
    ),
    (CONTROL, "micros_panic_terminate_no_console"): (
        "riscv_irq_save", "riscv_external_interrupt_disable", "sbi_system_reset", "micros_panic_halt",
    ),
    (CONTROL, "micros_panic_halt"): (),
    **PRELUDE_CALLS,
    **OBSERVER_CALLS,
    **{(TRACE_FIXTURE, name): tuple(dict.fromkeys(callees)) for name, callees in TRACE_FIXTURE_CALLS.items()},
    TRACE_FIXTURE_GETTERS[0]: (),
    TRACE_FIXTURE_GETTERS[1]: ("micros_hart_resolve",),
    (CONTROL, "riscv_irq_save"): (), (CONTROL, "riscv_external_interrupt_disable"): (),
    (REPORT, "riscv_irq_save"): (), (REPORT, "riscv_external_interrupt_disable"): (),
    (PLIC, "riscv_mmio_fence"): (),
    **{
        (unit, name): tuple(dict.fromkeys(DIAGNOSTIC_READONLY_CALLEES[name]))
        for unit, name in PRESERVERS if name in DIAGNOSTIC_READONLY_CALLEES
    },
    (THR_COUNTER, "micros_diagnostic_test_observe_thr"): (),
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
    relations: tuple = ()


@dataclass(frozen=True)
class BoundObject:
    symbol: str
    address: int
    size: int
    object_name: str
    section: str


@dataclass(frozen=True)
class FatalContextProof:
    version: int
    bindings: ContextBindings
    console: BoundObject
    history: BoundObject
    establishment: tuple[SourceCall, ...]
    preservation: tuple[FatalFunction, ...]
    exclusions: tuple
    sbi_inventory: tuple[FatalFunction, ...]
    firmware: object
    control: BoundObject
    latch_establishment: tuple
    prelude: tuple
    compiler_hooks: tuple = ()
    test_counter: BoundObject | None = None
    test_objects: tuple = ()
    test_arguments: tuple = ()
    trace_register: tuple = ()


@dataclass(frozen=True)
class FatalExclusion:
    role: str
    predicate: SourceCall
    edge: SourceCall
    required: Qualifier = Qualifier.UART_PANIC_READY


@dataclass(frozen=True)
class FatalArtifacts:
    variant: str
    image_name: str
    image: object
    map_text: str
    frames: dict
    origins: dict
    objects: dict
    sections: dict
    records: dict
    flows: dict
    sources: dict
    hook_sites: tuple
    preservers: tuple
    terminal_test: bool = False
    trace_test: bool = False
    nested_test: bool = False


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


def checked_source_record(root, key, origin, *, tty_test=False, terminal_test=False, trace_test=False, nested_test=False):
    if any(type(value) is not bool for value in (tty_test, terminal_test, trace_test)) or sum((tty_test, terminal_test, trace_test)) > 1:
        raise StackEvidenceError("fatal source borrowed overlapping or untyped observation configurations")
    actual = read_regular(owned_file(root, origin.source))[0].decode("utf-8")
    try:
        records = trace_function_records(origin.source, actual) if key.translation_unit in (TRACE_CORE, TRACE) else function_records(actual)
    except ValueError as error:
        raise StackEvidenceError(str(error)) from error
    record = records.get(key.symbol)
    if record is None or origin.line != definition_line(actual, key.symbol):
        raise StackEvidenceError("fatal function borrowed a source/body/line origin")
    if key.translation_unit in (TRACE_CORE, TRACE):
        expected = TRACE_CORE_CALLS if key.translation_unit == TRACE_CORE else TRACE_TARGET_CALLS
        if origin.source != key.translation_unit or record.guards or set(records) != set(expected):
            raise StackEvidenceError("trace private function borrowed another source, owner set or definition scope")
        lexical = c_lexical_text(record.body)
        if "[" in lexical or re.search(r"\b(?:for|while|goto)\b", lexical) and key.symbol != "micros_panic_trace_report":
            raise StackEvidenceError("trace private helper acquired a depth array, scan or additional loop")
        try:
            body = effective_trace_body(record.body, trace_test=trace_test, nested_test=nested_test)
        except ValueError as error:
            raise StackEvidenceError(str(error)) from error
        return replace(record, body=body)
    if key.translation_unit == TERMINAL_OBSERVER:
        if not terminal_test or origin.source != TERMINAL_OBSERVER or (key.translation_unit, key.symbol) not in OBSERVER_CALLS or record.guards:
            raise StackEvidenceError("terminal observer has an unbound source/function/configuration")
        return record
    if key.translation_unit == TRACE_FIXTURE:
        if not trace_test or origin.source != TRACE_FIXTURE or key.symbol not in TRACE_FIXTURE_CALLS or record.guards:
            raise StackEvidenceError("trace fixture borrowed another qualified source or configuration")
        body = record.body
        if key.symbol == "micros_panic_trace_test_reset":
            guard = "#ifdef MICROS_TEST_PANIC_TRACE_COUNTS\n"
            if body.count(guard) != 1 or body.count("#endif\n") != 1:
                raise StackEvidenceError("trace reset observer lost its exact actual-fault guard")
            body = body.replace(guard, "", 1).replace("#endif\n", "", 1)
        if re.search(r"^\s*#", body, re.MULTILINE):
            raise StackEvidenceError("trace observer retains an unbound preprocessor scope")
        return replace(record, body=body)
    if (key.translation_unit, key.symbol) in TRACE_FIXTURE_GETTERS:
        if not trace_test or origin.source != key.translation_unit:
            raise StackEvidenceError("trace fixture getter borrowed another source or mode")
        try:
            if project_trace_runtime(origin.source, actual) != trace_reference_source(origin.source):
                raise StackEvidenceError("trace fixture getter changed its fixed provider source")
        except ValueError as error:
            raise StackEvidenceError(str(error)) from error
        return record
    if (key.translation_unit, key.symbol) in FATAL_SOURCE_CONTRACTS:
        if origin.source != key.translation_unit or key.translation_unit == THR_COUNTER and not tty_test:
            raise StackEvidenceError("diagnostic helper borrowed another source or configuration")
        try:
            return checked_fatal_function(key.translation_unit, key.symbol, actual)
        except AssertionError as error:
            raise StackEvidenceError(str(error)) from error
    if origin.source in CONTROL_SOURCES:
        expected = function_records(CONTROL_SOURCES[origin.source]).get(key.symbol)
        if expected is None or (
            record.guards != expected.guards or tokens(record.signature) != tokens(expected.signature)
            or tokens(record.body) != tokens(expected.body)
        ):
            raise StackEvidenceError("terminal control function changed its independent operation contract")
        return record
    if (key.translation_unit, key.symbol) == (REPORT, "micros_panic_report"):
        expected = function_records(PANIC_ENTRY_WRAPPER)["micros_panic_report"]
        if (
            origin.source != REPORT or record.guards != expected.guards
            or tokens(record.signature) != tokens(expected.signature) or tokens(record.body) != tokens(expected.body)
        ):
            raise StackEvidenceError("terminal reporter changed its first-claim boundary")
        return record
    original = {
        "uart_write_direct": "uart_write", "uart_write_hex64_direct": "uart_write_hex64",
    }.get(key.symbol, key.symbol) if key.translation_unit == UART else key.symbol
    if (key.translation_unit, key.symbol) == (REPORT, "panic_report_claimed"):
        original = "micros_panic_report"
    expected = function_records(reference_source(str(root), origin.source)).get(original)
    try:
        projected = project_terminal_runtime(origin.source, actual)
        if origin.source == UART:
            projected = project_fatal_uart(projected)
        elif origin.source == REPORT:
            projected = project_fatal_report(projected)
        prior = function_records(projected).get(original)
        body = record.body
        if (key.translation_unit, key.symbol) == (UART, "uart_write_character"):
            effective_thr_body(effective_terminal_body(body, enabled=False), False)
            body = body.replace(THR_OBSERVATION, "    micros_diagnostic_test_observe_thr();\n" if tty_test else "", 1)
        body = effective_terminal_body(body, enabled=terminal_test, trace_enabled=trace_test)
    except (AssertionError, ValueError) as error:
        raise StackEvidenceError(str(error)) from error
    if (
        expected is None or prior is None or prior.guards != expected.guards
        or tokens(prior.signature) != tokens(expected.signature) or tokens(prior.body) != tokens(expected.body)
    ):
        raise StackEvidenceError("fatal function changed its fixed source operation")
    return replace(record, body=body)


def source_calls(root, key, origin, machine_calls, *, tty_test=False, terminal_test=False, trace_test=False, nested_test=False):
    record = checked_source_record(
        root, key, origin, tty_test=tty_test, terminal_test=terminal_test, trace_test=trace_test, nested_test=nested_test,
    )
    body = positional_tokens(record.body)
    values = tuple(token.value for token in body)
    stacks, closings = compound_layout(values)
    names = CALL_TARGETS.get((key.translation_unit, key.symbol))
    if trace_test and (key.translation_unit, key.symbol) == (TRACE_CORE, "trace_advance"):
        names = (*names, "micros_test_work_record")
    elif trace_test and (key.translation_unit, key.symbol) == (TRACE, "micros_panic_trace_report"):
        names = (*names, "micros_test_work_record", "micros_panic_trace_test_enter", "micros_panic_trace_test_owner",
                 "micros_panic_trace_test_header", "micros_panic_trace_test_started", "micros_panic_trace_test_link",
                 "micros_panic_trace_test_before_word", "micros_panic_trace_test_word", "micros_panic_trace_test_complete")
    if key.translation_unit == UART and key.symbol in ("uart_write_character", "uart_flush"):
        names = ("micros_panic_terminate_no_console",) + (
            ("micros_diagnostic_test_observe_thr",) if tty_test and key.symbol == "uart_write_character" else ()
        ) + (("micros_test_work_record", "micros_terminal_test_lsr", "micros_terminal_test_thr") if terminal_test else ())
        if trace_test:
            names += ("micros_test_work_record", "micros_panic_trace_test_lsr", "micros_panic_trace_test_thr")
    elif terminal_test and (key.translation_unit, key.symbol) == (UART, "uart_panic_seize"):
        names = (*names, "micros_terminal_test_seizure")
    elif terminal_test and (key.translation_unit, key.symbol) == (REPORT, "panic_report_claimed"):
        names = (*names, "micros_terminal_test_report", "micros_terminal_test_after_seize")
    elif terminal_test and (key.translation_unit, key.symbol) == (SBI, "sbi_system_reset"):
        names = (*names, "micros_terminal_test_reset")
    elif trace_test and (key.translation_unit, key.symbol) == (UART, "uart_panic_seize"):
        names = (*names, "micros_panic_trace_test_seizure")
    elif trace_test and (key.translation_unit, key.symbol) == (REPORT, "panic_report_claimed"):
        names = (*names, "micros_panic_trace_test_report", "micros_panic_trace_test_after_core")
    elif trace_test and (key.translation_unit, key.symbol) == (SBI, "sbi_system_reset"):
        names = (*names, "micros_panic_trace_test_reset")
    if names is None or any(call.target_symbol is None for call in machine_calls):
        raise StackEvidenceError("fatal qualifier has an unbound indirect call")
    result, counts = [], Counter()
    lexical = c_lexical_text(record.body)
    for index, token in enumerate(body[:-1]):
        target = "micros_panic_entry" if token.value == "MICROS_PANIC" else token.value
        if target not in names or values[index + 1] != "(":
            continue
        closing = matching_lexical(lexical, body[index + 1].start)
        fragment = tokens(record.body[token.start:closing + 1])
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


def one_token_declaration(source, declaration):
    actual, expected = tokens(source), tokens(declaration)
    return sum(
        actual[index:index + len(expected)] == expected
        for index in range(len(actual) - len(expected) + 1)
    ) == 1


def require_fatal_source_units(root, *, terminal_test=False, trace_test=False):
    try:
        for unit in SOURCE_UNITS:
            actual = read_regular(owned_file(root, unit))[0].decode("utf-8")
            if unit in CONTROL_SOURCES:
                if tokens(actual) != tokens(CONTROL_SOURCES[unit]):
                    raise StackEvidenceError("terminal private state/source declaration changed")
                continue
            restored = project_terminal_runtime(unit, actual)
            projected = project_fatal_uart(restored) if unit == UART else (
                project_fatal_report(restored) if unit == REPORT else restored
            )
            reference = reference_source(str(root), unit)
            actual_tokens = tokens(projected)
            admitted = actual_tokens == tokens(reference)
            if not admitted and unit == "kernel/tty/tty_handoff_runtime.h":
                prefix, terminator, suffix = reference.rpartition("\n#endif")
                declaration = (
                    "enum micros_tty_device_authority_status\n"
                    "micros_tty_handoff_runtime_inspect_device_binding(\n"
                    "    const struct micros_kernel_objects *objects,\n"
                    "    struct micros_tty_device_authority *binding);\n"
                )
                admitted = bool(terminator) and not suffix.strip() and actual_tokens == tokens(
                    prefix + "\n" + declaration + terminator + suffix,
                )
            if not admitted:
                raise StackEvidenceError("fatal source/type/CSR contract changed from its fixed stage: " + unit)
        public = read_regular(owned_file(root, "include/micros/diagnostic.h"))[0].decode("utf-8")
        header = read_regular(owned_file(root, "kernel/diagnostic/diagnostic_history_core.h"))[0].decode("utf-8")
        history = read_regular(owned_file(root, HISTORY))[0].decode("utf-8")
        if (
            tokens(public) != tokens(diagnostic_header_source())
            or not one_token_declaration(header, HISTORY_LAYOUT)
            or not one_token_declaration(history, "static struct micros_diagnostic_history diagnostic_history;")
        ):
            raise StackEvidenceError("diagnostic history lost its exact type, constants or sole storage")
        if terminal_test or trace_test:
            for unit in (WORK_COLLECTOR, "tests/work/work_counts.h", "include/micros/work_observation.h"):
                actual = read_regular(owned_file(root, unit))[0].decode("utf-8")
                if actual != reference_source(str(root), unit):
                    raise StackEvidenceError("terminal observer changed the original EV02 source/layout")
    except (AssertionError, ValueError) as error:
        raise StackEvidenceError(str(error)) from error


def bound_data_object(objects, sections, unit, name, size, *, initial=None, binding=0):
    relative = "CMakeFiles/micros.dir/" + unit + ".obj"
    obj = objects[relative]
    symbols = tuple(symbol for symbol in obj.symbols if symbol.name == name)
    if len(symbols) != 1:
        raise StackEvidenceError("fatal fact has no uniquely owned data object")
    symbol, = symbols
    if not 0 < symbol.section_index < len(obj.sections):
        raise StackEvidenceError("fatal data object has no owned section")
    section = obj.sections[symbol.section_index]
    prefix = ".bss." if initial is None else ".data."
    if (
        symbol.binding != binding or symbol.symbol_type != 1 or symbol.size != size or symbol.value != 0
        or section.name != prefix + name or section.flags != 3 or section.size != size
        or section.section_type != (8 if initial is None else 1)
        or symbol.section_index not in sections[relative]
        or initial is not None and section_bytes(obj, section) != initial
    ):
        raise StackEvidenceError("fatal fact borrowed another object layout or initial state")
    return symbol, BoundObject(
        name, sections[relative][symbol.section_index], size, relative, section.name,
    )


def require_history_references(records, flows, variant, image_name, history):
    for name in ("micros_diagnostic_panic_size", "micros_diagnostic_panic_byte_prevalidated"):
        key = FunctionKey(variant, image_name, HISTORY, name)
        obj, symbol, _ = records[key]
        high, _ = pcrel_values(obj, symbol, flows[key])
        references = tuple(value[2] for value in high.values() if (
            0 < value[2][2] < len(obj.sections) and obj.sections[value[2][2]].flags & 3 == 3
        ))
        if not references or any(
            reference[1] != history.name or reference[2] != history.section_index
            or not history.value <= reference[3] < history.value + history.size
            for reference in references
        ):
            raise StackEvidenceError("panic history readers do not bind the same actual history object")
        if name == "micros_diagnostic_panic_size":
            calls = tuple(call for call in flows[key].calls if call.target_symbol == "micros_diagnostic_history_valid")
            if len(calls) != 1 or register_inputs(obj, symbol)[calls[0].offset][10] != (
                "symbol", history.name, history.section_index, history.value,
            ):
                raise StackEvidenceError("panic history validity borrowed another object")


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
            if symbol.name != "panic_report_claimed" or word != 0x10500073:
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


def collect_fatal_artifacts(root, directory, catalog, *, observed_mode, tty_test, terminal_test=False, trace_test=False, nested_test=False):
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
    preservers = PRESERVERS + (((THR_COUNTER, "micros_diagnostic_test_observe_thr"),) if tty_test else ())
    if terminal_test:
        preservers += tuple(OBSERVER_CALLS)
    elif trace_test:
        preservers += (
            *((TRACE_FIXTURE, name) for name in TRACE_FIXTURE_CALLS),
            *((unit, name) for unit, name in OBSERVER_CALLS if unit == WORK_COLLECTOR),
            *TRACE_FIXTURE_GETTERS,
        )
    required = (
        *preservers, (SBI, "sbi_set_timer"), *PRELUDE_SUPPORT,
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
        machine_sites = tuple(sorted((
            (call.offset, call.targets[0].symbol, call.kind) for call in catalog.calls
            if call.caller == key and len(call.targets) == 1
        ), key=lambda row: row[0])) + tuple(
            (call.offset, call.target_symbol, call.kind) for call in catalog.pending if call.caller == key
        )
        if tuple(sorted(machine_sites)) != tuple(sorted(
            (call.offset, call.target_symbol, call.kind) for call in flow.calls
        )):
            raise StackEvidenceError("fatal catalog omitted or changed an actual machine-call site")
        runtime, hooks = runtime_call_inventory(obj, symbol) if observed_mode else (flow.calls, ())
        source = source_calls(
            root, key, origins[key], runtime, tty_test=tty_test, terminal_test=terminal_test,
            trace_test=trace_test, nested_test=nested_test,
        )
        hook_sites.extend((key, hook) for hook in hooks)
        records[key] = obj, symbol, frame
        flows[key], sources[key] = flow, source
    return FatalArtifacts(
        variant, image_name, image, text, frames, origins, objects, sections,
        records, flows, sources, tuple(hook_sites), preservers, terminal_test, trace_test, nested_test,
    )


def bind_fatal_state(root, artifacts, *, tty_test):
    objects, sections = artifacts.objects, artifacts.sections
    console_symbol, console = bound_data_object(
        objects, sections, UART, "uart_console_state", 16,
        initial=MAGIC.to_bytes(8, "little") + (1).to_bytes(4, "little") + bytes(4),
    )
    history_symbol, history = bound_data_object(objects, sections, HISTORY, "diagnostic_history", 12012)
    counter_symbol, counter = None, None
    if tty_test:
        counter_symbol, counter = bound_data_object(objects, sections, THR_COUNTER, "diagnostic_thr_writes", 8)
    control_symbol, control = bound_data_object(objects, sections, CONTROL, "panic_control", 4)
    _, plic = bound_data_object(objects, sections, PLIC, "plic_controller", 48)
    _, initialized = bound_data_object(objects, sections, PLIC, "plic_initialized", 1)
    _, tty = bound_data_object(objects, sections, TTY_CONTEXT, "context_state", 80)
    test_objects = []
    if artifacts.terminal_test:
        for unit, name, size, binding in (
            (TERMINAL_OBSERVER, "micros_terminal_test_observation", 104, 1),
            (TERMINAL_OBSERVER, "micros_terminal_test_work_snapshot", 3056, 1),
            (WORK_COLLECTOR, "collector", 3072, 0),
            (TERMINAL_OBSERVER, "faults_armed", 1, 0),
            (TERMINAL_OBSERVER, "recursion_injected", 1, 0),
            (TERMINAL_OBSERVER, "forced_reads", 8, 0),
        ):
            _, bound = bound_data_object(objects, sections, unit, name, size, binding=binding)
            test_objects.append(bound)
    elif artifacts.trace_test:
        for name, size in TRACE_FIXTURE_OBJECTS.items():
            _, bound = bound_data_object(
                objects, sections, TRACE_FIXTURE, name, size, binding=int(name.startswith("micros_")),
            )
            test_objects.append(bound)
        _, bound = bound_data_object(objects, sections, WORK_COLLECTOR, "collector", 3072)
        test_objects.append(bound)
    bound_objects = (console, history, control, plic, initialized, tty, *test_objects, *((counter,) if counter is not None else ()))
    for index, first in enumerate(bound_objects):
        for second in bound_objects[index + 1:]:
            if max(first.address, second.address) < min(first.address + first.size, second.address + second.size):
                raise StackEvidenceError("fatal console, history or test counter objects alias")
    require_history_references(
        artifacts.records, artifacts.flows, artifacts.variant, artifacts.image_name, history_symbol,
    )
    console_machine_proof(root, objects["CMakeFiles/micros.dir/" + CONSOLE + ".obj"])
    return console_symbol, console, history, counter_symbol, counter, control_symbol, control, (plic, initialized, tty), tuple(test_objects)


def trace_compile_policy(root, directory):
    entries = json.loads(read_regular(Path(directory) / "compile_commands.json")[0])
    required = TRACE_RECIPE["kernel_c_options"]
    contradictory = ("-fomit-frame-pointer", "-momit-leaf-frame-pointer", "-foptimize-sibling-calls")
    units = []
    for entry in entries:
        arguments = entry.get("arguments") or shlex.split(entry["command"])
        output = Path(entry["output"])
        if output.is_absolute():
            output = output.relative_to(directory)
        kernel_c = str(output).startswith("CMakeFiles/micros.dir/") and entry["file"].endswith(".c")
        if kernel_c:
            if any(arguments.count(flag) != 1 for flag in required) or any(flag in arguments for flag in contradictory):
                raise StackEvidenceError("trace frame-chain policy is absent, repeated or overridden in an actual kernel C recipe")
            units.append(str(Path(entry["file"]).relative_to(root)))
        elif any(flag in arguments for flag in required[1:]):
            raise StackEvidenceError("trace frame-chain policy escaped kernel C compilation")
    if not {TRACE, TRACE_CORE} <= set(units) or len(units) != len(set(units)):
        raise StackEvidenceError("trace compiler policy omitted or duplicated a current kernel owner")
    return tuple(sorted(units)), required


def trace_preservation(root, artifacts, protected):
    layout = trace_object_layout(root)
    for unit in sorted({unit for unit, _ in TRACE_SELECTED}):
        source = read_regular(owned_file(root, unit))[0].decode()
        try:
            source = project_trace_runtime(unit, source)
        except ValueError as error:
            raise StackEvidenceError(str(error)) from error
        if source != trace_reference_source(unit):
            raise StackEvidenceError("trace selected-owner provider changed its complete fixed predecessor source")
    runtime = "kernel/objects/kernel_object_runtime.c"
    ownership = []
    for name, size in (("kernel_objects", layout["registry_bytes"]), ("boot_hart_handle", 8), ("runtime_ready", 1)):
        _, bound = bound_data_object(artifacts.objects, artifacts.sections, runtime, name, size)
        ownership.append(bound)
    objects = (*protected, *ownership)
    for index, first in enumerate(objects):
        if any(max(first.address, second.address) < min(first.address + first.size, second.address + second.size)
               for second in objects[index + 1:]):
            raise StackEvidenceError("trace ownership objects alias protected fatal state")
    effects = {}
    for unit in (TRACE_CORE, TRACE, *sorted({unit for unit, _ in TRACE_SELECTED})):
        relative = "CMakeFiles/micros.dir/" + unit + ".obj"
        obj = artifacts.objects[relative]
        if unit == TRACE_CORE:
            selected = trace_core_effects(obj, counted=artifacts.trace_test)
        elif unit == TRACE:
            selected = trace_target_effects(obj, trace_test=artifacts.trace_test)
        elif unit == runtime:
            selected = trace_accessor_effects(obj, artifacts.sections[relative], layout, fixture=artifacts.trace_test)
        else:
            selected = trace_resolver_effects(obj, unit, layout)
        for name, effect in selected.items():
            effects[FunctionKey(artifacts.variant, artifacts.image_name, unit, name)] = effect
    expected = {FunctionKey(artifacts.variant, artifacts.image_name, unit, name) for unit, name in TRACE_PRESERVERS}
    if artifacts.trace_test:
        expected.update(FunctionKey(artifacts.variant, artifacts.image_name, unit, name) for unit, name in TRACE_FIXTURE_GETTERS)
    if set(effects) != expected:
        raise StackEvidenceError("trace preservation omitted or borrowed a transitive function effect")
    return effects, tuple(ownership)


def trace_fixture_preservation(root, artifacts, ownership, test_objects, accessors):
    if not artifacts.trace_test:
        return {}, ()
    layout = trace_object_layout(root)
    objects = {row.symbol: row.address for row in test_objects}
    registry, = (row.address for row in ownership if row.symbol == "kernel_objects")
    linked = {}
    for name in ("micros_thread_kernel_stacks", "micros_thread_kernel_stacks_end", "micros_panic_trace_test_load_fault"):
        symbols = tuple(symbol for symbol in artifacts.image.symbols if symbol.name == name and symbol.binding == 1)
        if len(symbols) != 1:
            raise StackEvidenceError("trace fixture lost a unique actual linked input symbol")
        linked[name] = symbols[0].value
    bank = linked["micros_thread_kernel_stacks"]
    if bank % 16 or linked["micros_thread_kernel_stacks_end"] - bank != layout["thread_capacity"] * 16384:
        raise StackEvidenceError("trace fixture borrowed another linked thread-stack bank")
    relative = "CMakeFiles/micros.dir/" + TRACE_FIXTURE + ".obj"
    obj, sections = artifacts.objects[relative], artifacts.sections[relative]
    aliases = {
        "registry": registry, "hart": registry + layout["hart_table"],
        "thread": registry + layout["thread_table"] + layout["thread_bytes"],
        "bank": bank, "fault": linked["micros_panic_trace_test_load_fault"],
    }
    constructor = trace_fixture_alias_effects(obj, sections, layout, aliases)
    matrix = []
    for slot in range(layout["hart_capacity"]):
        selected = {**aliases, "hart": registry + layout["hart_table"] + slot * layout["hart_bytes"]}
        matrix.append((slot, trace_fixture_effects(obj, sections, layout, selected)))
    effects = {}
    for name in TRACE_FIXTURE_CALLS:
        rows = tuple((slot, current[name]) for slot, current in matrix)
        key = FunctionKey(artifacts.variant, artifacts.image_name, TRACE_FIXTURE, name)
        effects[key] = MachineEffects(
            rows[0][1].calls, tuple((slot, effect.writes) for slot, effect in rows),
            tuple((slot, effect.systems) for slot, effect in rows),
            (("every-resolved-boot-hart-slot", tuple((slot, effect.dependencies) for slot, effect in rows)),),
        )
    relative = "CMakeFiles/micros.dir/" + WORK_COLLECTOR + ".obj"
    collector = trace_collector_effects(
        artifacts.objects[relative], artifacts.sections[relative],
        objects["collector"], objects["micros_panic_trace_test_work_snapshot"],
    )
    for name, effect in collector.items():
        effects[FunctionKey(artifacts.variant, artifacts.image_name, WORK_COLLECTOR, name)] = effect
    return effects, (
        ("linked-inputs", tuple(sorted(linked.items()))), ("constructor", constructor),
        ("setup-accessors", tuple(
            (key, accessors[key]) for key in (
                FunctionKey(artifacts.variant, artifacts.image_name, unit, name) for unit, name in TRACE_FIXTURE_GETTERS
            )
        )),
        ("qualified-observers-and-collector", tuple(effects.items())),
    )


def trace_fault_artifacts(root, directory, catalog, artifacts):
    objects, sections, sources = {}, {}, []
    frames = {row.function: row for row in catalog.frames}
    definitions = {row.function: row for row in catalog.definitions}
    origins, names = {row.function: row for row in catalog.origins}, dict(catalog.objects)
    for unit, name in TRACE_FAULT_FUNCTIONS:
        key = FunctionKey(artifacts.variant, artifacts.image_name, unit, name)
        relative = "CMakeFiles/micros.dir/" + unit + ".obj"
        if key not in frames or names.get(key) != relative or key not in origins or key not in definitions:
            raise StackEvidenceError("trace fault prefix lacks a complete actual C owner")
        if unit not in objects:
            objects[unit] = artifacts.objects.get(relative) or load_object(owned_file(directory, relative))
            sections[unit] = verify_linked_object(objects[unit], relative, artifacts.image, artifacts.map_text)
        obj, origin = objects[unit], origins[key]
        symbol = function_symbols(obj).get(name)
        source = read_regular(owned_file(root, unit))[0].decode()
        try:
            prior = project_trace_runtime(unit, source)
        except ValueError as error:
            raise StackEvidenceError(str(error)) from error
        if (
            prior != trace_reference_source(unit) or origin.source != unit or origin.line != definition_line(source, name)
            or symbol is None or object_frames(obj, function_symbols(obj)).get(name) != frames[key].bytes_used
            or frames[key].classification != "static" or definitions[key].size != symbol.size
        ):
            raise StackEvidenceError("trace fault prefix borrowed a source, frame or definition")
        location = FunctionLocation(key, relative, obj.sections[symbol.section_index].name, symbol.value, symbol.size)
        if join_function_locations((location,), artifacts.map_text, artifacts.image.symbols).get(key) != definitions[key].address:
            raise StackEvidenceError("trace fault prefix borrowed a linked function range")
        flow = function_flow(obj, symbol)
        calls = tuple((call.offset, call.targets[0].symbol, call.kind) for call in catalog.calls
                      if call.caller == key and len(call.targets) == 1) + tuple(
            (call.offset, call.target_symbol, call.kind) for call in catalog.pending if call.caller == key
        )
        if tuple(sorted(calls)) != tuple(sorted((call.offset, call.target_symbol, call.kind) for call in flow.calls)):
            raise StackEvidenceError("trace fault prefix omitted an actual source/object call")
        sources.append((key, origin, definitions[key], frames[key], hashlib.sha256(obj.data).hexdigest(),
                        hashlib.sha256(source.encode()).hexdigest(), tuple(calls)))
    return objects, sections, tuple(sources)


def trace_fault_register(root, directory, catalog, artifacts, ownership, test_objects, protected):
    if not artifacts.trace_test:
        return ()
    layout = trace_object_layout(root)
    variant, image_name = artifacts.variant, artifacts.image_name

    def address(name):
        selected = tuple(symbol for symbol in artifacts.image.symbols if symbol.name == name and symbol.binding == 1)
        if len(selected) != 1:
            raise StackEvidenceError("trace fault epoch has no unique actual linked symbol: " + name)
        return selected[0].value

    stacks = tuple((role, address(bottom), address(top), size) for role, bottom, top, size in (
        ("boot", "__boot_stack_bottom", "__boot_stack_top", 16384),
        ("primary", "__trap_stack_bottom", "__trap_stack_top", 16384),
        ("emergency", "__trap_emergency_stack_bottom", "__trap_emergency_stack_top", 4096),
        ("thread-bank", "micros_thread_kernel_stacks", "micros_thread_kernel_stacks_end", 128 * 16384),
    ))
    for _, bottom, top, size in stacks:
        if bottom % 16 or top - bottom != size or any(
            max(bottom, row.address) < min(top, row.address + row.size) for row in (*ownership, *test_objects, *protected)
        ):
            raise StackEvidenceError("trace hardware epoch aliases its registry/test inputs or changes its fixed capacity")
    if any(max(first[1], second[1]) < min(first[2], second[2])
           for index, first in enumerate(stacks) for second in stacks[index + 1:]):
        raise StackEvidenceError("trace fault epochs share physical stack storage")
    flags = frozenset({"MICROS_BUILD_PANIC_TRACE_TEST"})
    faults, trap = [], None
    for role, unit, name, label in (
        ("load", "arch/riscv64/panic_trace.S", "micros_panic_trace_load_word", "micros_panic_trace_test_load_fault"),
        ("nested", "arch/riscv64/trap.S", "micros_trap_entry", "micros_panic_trace_test_nested_fault"),
        ("outer", "tests/qemu/panic_trace_kernel.S", "micros_panic_trace_test_trap", "micros_panic_trace_test_outer_fault"),
    ):
        relative = "CMakeFiles/micros.dir/" + unit + ".obj"
        obj = load_object(owned_file(directory, relative))
        proof = assembly_object_evidence(root, obj, unit, variant, image_name, flags)
        sections = verify_linked_object(obj, relative, artifacts.image, artifacts.map_text)
        symbol = function_symbols(obj)[name]
        matches = tuple(row for row in proof.faults if row.function.symbol == name)
        if len(matches) != 1:
            raise StackEvidenceError("trace hardware transition lacks its one exact object fault")
        witness, = matches
        base = sections[symbol.section_index]
        offset = linked_offset(obj, symbol.section_index, symbol.value + witness.offset, base) - linked_offset(
            obj, symbol.section_index, symbol.value, base,
        )
        if witness.target != "micros_trap_entry" or witness.continuation or address(label) != address(name) + offset:
            raise StackEvidenceError("trace hardware transition borrowed another linked instruction or continuation")
        faults.append((role, replace(witness, offset=offset), label, address(label), hashlib.sha256(obj.data).hexdigest()))
        if role == "nested":
            trap = obj
    registry, = (row.address for row in ownership if row.symbol == "kernel_objects")
    epochs = []
    for slot in range(layout["hart_capacity"]):
        for owner, primary_top in (("primary", stacks[1][2]), ("thread-slot-one", stacks[3][1] + 32768)):
            bindings = {
                "anchor": registry + layout["hart_table"] + slot * layout["hart_bytes"],
                "source_sp": stacks[0][2] - 16, "primary_top": primary_top,
                "emergency_top": stacks[2][2], "selector": address("micros_panic_trace_test_scenario"),
                "entry_pc": address("micros_trap_entry"),
            }
            epochs.append((slot, owner, tuple(sorted(bindings.items())), trace_trap_effects(trap, bindings)))
    objects, sections, sources = trace_fault_artifacts(root, directory, catalog, artifacts)
    prefix = trace_fault_prefix_effects(objects, sections, layout, stacks[1][2])
    key = FunctionKey(variant, image_name, TRACE_FIXTURE, "micros_panic_trace_test_before_word")
    recursive = tuple(row for row in artifacts.sources[key] if row.target == "micros_panic_entry")
    if len(recursive) != 1 or not any(call.offset == recursive[0].offset and call.kind is EdgeKind.CALL
                                     for call in artifacts.flows[key].calls):
        raise StackEvidenceError("trace explicit reentry no longer retains its actual live C caller")
    return (
        ("physical-stacks", stacks), ("hardware-faults", tuple(faults)),
        ("actual-trap-pivots", tuple(epochs)), ("post-fault-source-objects", sources),
        ("post-fault-c-prefix", prefix), ("same-epoch-reentry", recursive[0]),
    )


def trace_frame_witnesses(directory, catalog, artifacts):
    witnesses = []
    objects = dict(catalog.objects)
    names = (
        "kernel_main", "micros_trap_dispatch", "micros_trap_nested_panic",
        "micros_scheduler_idle_select", "micros_panic_report", "panic_report_claimed",
    ) + ((
        "micros_panic_trace_test_run", "prepare_thread_stack", "trace_chain_one", "trace_chain_two", "trace_chain_three",
    ) if artifacts.trace_test else ())
    for name in names:
        keys = tuple(row.function for row in catalog.frames if row.function.symbol == name)
        if len(keys) != 1:
            raise StackEvidenceError("trace epoch root or reporter has no unique actual C frame: " + name)
        key, = keys
        relative = objects[key]
        obj = artifacts.objects.get(relative)
        if obj is None:
            obj = load_object(owned_file(directory, relative))
            verify_linked_object(obj, relative, artifacts.image, artifacts.map_text)
        symbol = function_symbols(obj)[name]
        witnesses.append((key, hashlib.sha256(obj.data).hexdigest(), trace_frame_record_effects(obj, symbol)))
    return tuple(witnesses)


def trace_register(artifacts, effects, ownership, preservation, policy, frames, fixture):
    key = FunctionKey(artifacts.variant, artifacts.image_name, REPORT, "panic_report_claimed")
    entries = tuple(row for row in artifacts.sources[key] if row.target == "micros_panic_trace_report")
    history = tuple(row for row in artifacts.sources[key] if row.target == "micros_diagnostic_panic_report")
    if len(entries) != 1 or len(history) != 1 or entries[0].offset >= history[0].offset:
        raise StackEvidenceError("trace call is absent, repeated or not before the original panic history")
    flow = artifacts.flows[key]
    if history[0].offset in reachable_instructions(flow, excluded=flow.continuations[entries[0].offset]):
        raise StackEvidenceError("panic history bypasses the trace's actual normal-return boundary")
    assembly = tuple(row for row in preservation if row.function.translation_unit == "arch/riscv64/panic_trace.S")
    if len(assembly) != 2 or any(row.frame != 0 for row in assembly):
        raise StackEvidenceError("trace register omitted its actual no-frame assembly helpers")
    return (
        ("entry", entries[0]), ("ownership", ownership),
        ("portable", tuple((key, value) for key, value in effects.items() if key.translation_unit == TRACE_CORE)),
        ("target", tuple((key, value) for key, value in effects.items() if key.translation_unit == TRACE)),
        ("selected", tuple((key, value) for key, value in effects.items() if (key.translation_unit, key.symbol) in TRACE_SELECTED)),
        ("assembly", assembly), ("frames", frames), ("compile-policy", policy), ("fixture", fixture),
    )


def _prelude_artifact(artifacts, unit, name):
    key = FunctionKey(artifacts.variant, artifacts.image_name, unit, name)
    obj, symbol, _ = artifacts.records[key]
    return key, obj, symbol, artifacts.sections["CMakeFiles/micros.dir/" + unit + ".obj"]


def plic_constructor_effects(artifacts, plic, initialized):
    registers = (0x0c000028, 0x0c002080, 0x0c201000, 0x0c201004)
    mmio = tuple((value, 4) for value in registers)
    key, obj, symbol, sections = _prelude_artifact(artifacts, PLIC, "micros_plic_initialize")
    flow = artifacts.flows[key]
    states, _, _ = prelude_states(obj, symbol, sections)
    factories = tuple(call for call in flow.calls if call.target_symbol == "plic_register")
    if len(factories) != 4 or tuple(states[call.offset][0][10] for call in factories) != tuple(
        ("scalar", value) for value in registers
    ):
        raise StackEvidenceError("PLIC constructor did not use its four fixed platform register operands")
    factory_key, factory_obj, factory, factory_sections = _prelude_artifact(artifacts, PLIC, "plic_register")
    for value in registers:
        _, factory_states = prelude_machine_effects(
            factory_obj, factory, PLIC, factory_sections, arguments=(value,),
        )
        returns = [point for point, instruction in artifacts.flows[factory_key].instructions.items() if is_return(*instruction)]
        if not returns or any(factory_states[point][0][10] != ("scalar", value) for point in returns):
            raise StackEvidenceError("PLIC register constructor changed its exact pointer result")
    results = {call.offset: value for call, value in zip(factories, registers)}
    core_call, = (call for call in flow.calls if call.target_symbol == "micros_plic_controller_initialize")
    validation, = (call for call in flow.calls if call.target_symbol == "micros_plic_validate")
    results.update({core_call.offset: 0, validation.offset: 1})
    effects, states = prelude_machine_effects(
        obj, symbol, PLIC, sections, results=results,
        readable=((plic.address, 48), (initialized.address, 1), *mmio), writable=((initialized.address, 1),),
    )
    operands, memory = states[core_call.offset]
    argument = operands[11]
    if operands[10] != ("scalar", plic.address) or argument is None or argument[0] != "stack":
        raise StackEvidenceError("PLIC constructor borrowed another destination or register block")
    if tuple(dict(memory).get(argument[1] + 8 * index) for index in range(4)) != tuple(
        (8, ("scalar", value)) for value in registers
    ):
        raise StackEvidenceError("PLIC constructor stack block does not contain its exact fixed pointers")
    if len(effects.writes) != 1 or effects.writes[0][1:] != (initialized.address, 1, ("scalar", 1)):
        raise StackEvidenceError("PLIC initialized state has another writer or result")
    gate = result_zero_gate(obj, symbol, core_call.offset)
    point = effects.writes[0][0]
    if point in reachable_instructions(flow, start=gate.nonzero) or point in reachable_instructions(
        flow, excluded=flow.continuations[core_call.offset],
    ):
        raise StackEvidenceError("PLIC initialized publication bypasses successful pointer construction")
    # A separate analysis address represents the proved caller-owned register block.
    input_base = 0x6000000000000000
    fields = {(input_base + index * 8, 8): value for index, value in enumerate(registers)}
    shape_key, shape_obj, shape, shape_sections = _prelude_artifact(artifacts, PLIC_CORE, "registers_are_valid")
    _, shape_states = prelude_machine_effects(
        shape_obj, shape, PLIC_CORE, shape_sections, arguments=(input_base,), fields=fields, readable=((input_base, 32),),
    )
    returns = [point for point, instruction in artifacts.flows[shape_key].instructions.items() if is_return(*instruction) and point in shape_states]
    if not returns or any(shape_states[point][0][10] != ("scalar", 1) for point in returns):
        raise StackEvidenceError("fixed PLIC register block is not accepted by its actual shape predicate")
    create_key, create_obj, create, create_sections = _prelude_artifact(artifacts, PLIC_CORE, "micros_plic_controller_initialize")
    shape_call, = (
        call for call in artifacts.flows[create_key].calls
        if call.target_symbol == "registers_are_valid"
    )
    created, _ = prelude_machine_effects(
        create_obj, create, PLIC_CORE, create_sections, arguments=(plic.address, input_base),
        results={shape_call.offset: 1}, fields=fields,
        readable=((input_base, 32), (plic.address, 48), *mmio), writable=((plic.address, 48), *mmio[:3]),
    )
    for index, value in enumerate(registers):
        writes = tuple(row for row in created.writes if row[1] == plic.address + 16 + index * 8)
        if len(writes) != 1 or writes[0][2:] != (8, ("scalar", value)):
            raise StackEvidenceError("PLIC initializer did not install each exact persistent register pointer")
    return tuple((plic.address + 16 + index * 8, value) for index, value in enumerate(registers)), (
        (key, effects), (create_key, created), (shape_key, tuple(returns)),
    )


def prelude_preservation(artifacts, objects):
    plic, initialized, tty = objects
    pointers, construction = plic_constructor_effects(artifacts, plic, initialized)
    fields = {(address, 8): value for address, value in pointers}
    mmio = tuple((value, 4) for _, value in pointers)
    readable = ((plic.address, 48), (initialized.address, 1), (tty.address, 80), *mmio)
    getter_key, getter_obj, getter, getter_sections = _prelude_artifact(artifacts, TTY_CONTEXT, "tty_handoff_context")
    getter_effects, getter_states = prelude_machine_effects(
        getter_obj, getter, TTY_CONTEXT, getter_sections, readable=readable,
    )
    returns = [point for point, instruction in artifacts.flows[getter_key].instructions.items() if is_return(*instruction)]
    if not returns or any(getter_states[point][0][10] != ("scalar", tty.address) for point in returns):
        raise StackEvidenceError("TTY prelude does not obtain its actual bounded context object")
    effects = {getter_key: getter_effects}
    arguments = {
        (PLIC, "micros_plic_validate"): (1,),
        (PLIC_CORE, "micros_plic_controller_disable_tty"): (plic.address,),
        (PLIC_CORE, "micros_plic_controller_validate"): (plic.address,),
        (PLIC_CORE, "controller_shape_is_valid"): (plic.address,),
        (PLIC_CORE, "registers_are_valid"): (plic.address + 16,),
        (TTY_CORE, "micros_tty_handoff_panic"): (tty.address + 40,),
    }
    targets = {
        "micros_plic_controller_disable_tty": plic.address, "micros_plic_controller_validate": plic.address,
        "controller_shape_is_valid": plic.address, "registers_are_valid": plic.address + 16,
        "micros_plic_validate": 1, "micros_tty_handoff_panic": tty.address + 40,
    }
    for unit, name in PRELUDE_CALLS:
        if (unit, name) in PRELUDE_SUPPORT or (unit, name) == (TTY_CONTEXT, "tty_handoff_context"):
            continue
        key, obj, symbol, sections = _prelude_artifact(artifacts, unit, name)
        calls = artifacts.flows[key].calls
        results = {call.offset: tty.address for call in calls if call.target_symbol == "tty_handoff_context"}
        writable = ()
        if name == "micros_plic_controller_disable_tty":
            writable = ((plic.address + 8, 4), *mmio[:3])
        elif name == "micros_tty_handoff_panic":
            writable = tuple((tty.address + offset, size) for offset, size in ((48, 4), (52, 4), (56, 8), (64, 4), (68, 1)))
        effect, states = prelude_machine_effects(
            obj, symbol, unit, sections, arguments=arguments.get((unit, name), ()),
            results=results, fields=fields, readable=readable, writable=writable,
        )
        for call in calls:
            if call.target_symbol in targets and states[call.offset][0][10] != ("scalar", targets[call.target_symbol]):
                raise StackEvidenceError("pre-seizure helper borrowed another context/controller argument")
        effects[key] = effect
    return effects, (*construction, (getter_key, getter_effects), *tuple(effects.items()))


def terminal_observer_arguments(artifacts, objects):
    if not artifacts.terminal_test:
        return ()
    snapshot, = (item for item in objects if item.symbol == "micros_terminal_test_work_snapshot")
    guest = artifacts.objects["CMakeFiles/micros.dir/" + TERMINAL_OBSERVER + ".obj"]
    snapshot_symbol, = (symbol for symbol in guest.symbols if symbol.name == snapshot.symbol)
    result = []

    def require(unit, name, target, expected, arguments=None):
        key, obj, symbol, _ = _prelude_artifact(artifacts, unit, name)
        calls = tuple(call for call in artifacts.flows[key].calls if call.target_symbol == target)
        if len(calls) != 1:
            raise StackEvidenceError("terminal observer has no unique actual caller")
        call, = calls
        actual = register_inputs(obj, symbol, arguments=arguments)[call.offset][10:10 + len(expected)]
        if actual != expected:
            raise StackEvidenceError("terminal observer borrowed a snapshot, capture or primitive argument")
        result.append((key, call.offset, target, actual))

    for name in ("uart_write_character", "uart_flush"):
        require(UART, name, "micros_test_work_record", (("scalar", 1), ("scalar", 0), ("scalar", 1)))
    require(REPORT, "panic_report_claimed", "micros_terminal_test_report", (("argument", 5, ()),))
    require(
        TERMINAL_OBSERVER, "finish_capture", "micros_test_work_finish",
        (("symbol", snapshot_symbol.name, snapshot_symbol.section_index, snapshot_symbol.value),),
    )
    require(
        WORK_COLLECTOR, "micros_test_work_finish", "publish_snapshot",
        (("scalar", snapshot.address),), arguments=(snapshot.address,),
    )
    return tuple(result)


def trace_fixture_arguments(artifacts, objects):
    if not artifacts.trace_test:
        return ()
    result = []
    for unit, name in ((UART, "uart_write_character"), (UART, "uart_flush")):
        key, obj, symbol, _ = _prelude_artifact(artifacts, unit, name)
        calls = tuple(call for call in artifacts.flows[key].calls if call.target_symbol == "micros_test_work_record")
        if len(calls) != 1 or register_inputs(obj, symbol)[calls[0].offset][10:13] != (
            ("scalar", 1), ("scalar", 0), ("scalar", 1),
        ):
            raise StackEvidenceError("trace UART observation borrowed its actual diagnostic work kind or unit")
        result.append((key, calls[0].offset, calls[0].target_symbol, (1, 0, 1)))
    key, obj, symbol, _ = _prelude_artifact(artifacts, REPORT, "panic_report_claimed")
    calls = tuple(call for call in artifacts.flows[key].calls if call.target_symbol == "micros_panic_trace_test_report")
    if len(calls) != 1 or register_inputs(obj, symbol)[calls[0].offset][10] != ("argument", 5, ()):
        raise StackEvidenceError("trace report observation borrowed another live panic capture")
    result.append((key, calls[0].offset, calls[0].target_symbol, ("argument", 5, ())))
    snapshot, = (row.address for row in objects if row.symbol == "micros_panic_trace_test_work_snapshot")
    key, obj, symbol, _ = _prelude_artifact(artifacts, WORK_COLLECTOR, "micros_test_work_finish")
    calls = tuple(call for call in artifacts.flows[key].calls if call.target_symbol == "publish_snapshot")
    if len(calls) != 1 or register_inputs(obj, symbol, arguments=(snapshot,))[calls[0].offset][10] != ("scalar", snapshot):
        raise StackEvidenceError("trace work publication borrowed another complete caller output")
    result.append((key, calls[0].offset, calls[0].target_symbol, snapshot))
    return tuple(result)


def preserving_fatal_functions(
    root, directory, artifacts, console_symbol, counter_symbol, prelude_effects, test_objects, *, observed_mode, tty_test,
    trace_effects,
):
    variant, image_name = artifacts.variant, artifacts.image_name
    records, sources = artifacts.records, artifacts.sources
    image, text = artifacts.image, artifacts.map_text
    preserving = []
    control_obj = artifacts.objects["CMakeFiles/micros.dir/" + CONTROL_CORE + ".obj"]
    control_relations = panic_control_effects(control_obj)
    runtime_obj = artifacts.objects["CMakeFiles/micros.dir/" + CONTROL + ".obj"]
    runtime_effects = {name: (effects, paths) for name, effects, paths in panic_runtime_effects(runtime_obj)}
    for unit, name in artifacts.preservers:
        key = FunctionKey(variant, image_name, unit, name)
        obj, symbol, frame = records[key]
        if key in trace_effects:
            effects = trace_effects[key]
        elif unit == UART:
            effects = diagnostic_selector_effects(obj, symbol, console_symbol) if name == "uart_panic_mode" else (
                uart_machine_effects(
                    obj, symbol, test_counter=tty_test, terminal=True,
                    terminal_test=artifacts.terminal_test, console=console_symbol,
                    trace_test=artifacts.trace_test,
                )
            )
        elif unit == SBI:
            effects = terminal_srst_effects(obj, symbol, test_observer=artifacts.terminal_test, trace_observer=artifacts.trace_test)
        elif unit in (TERMINAL_OBSERVER, WORK_COLLECTOR):
            effects = terminal_observer_effects(
                obj, symbol, unit, artifacts.sections["CMakeFiles/micros.dir/" + unit + ".obj"],
                {item.symbol: item.address for item in test_objects},
            )
        elif unit == CONTROL_CORE:
            effects = MachineEffects((), (), (), control_relations)
        elif unit == CONTROL:
            if name in runtime_effects:
                effect, paths = runtime_effects[name]
                effects = MachineEffects(effect.calls, effect.writes, effect.systems, paths)
            else:
                effects = uart_machine_effects(obj, symbol)
        elif unit == REPORT:
            if name == "micros_panic_report":
                effects = terminal_reporter_effects(obj, symbol)
            elif name in ("riscv_irq_save", "riscv_external_interrupt_disable"):
                effects = uart_machine_effects(obj, symbol)
            else:
                writes, systems = reporter_effects(obj, symbol)
                effects = MachineEffects((), writes, systems)
        elif key in prelude_effects:
            effects = prelude_effects[key]
        elif (unit, name) == (PLIC, "riscv_mmio_fence"):
            effects = uart_machine_effects(obj, symbol)
        elif unit == THR_COUNTER:
            effects = diagnostic_counter_effects(obj, symbol, counter_symbol)
        elif name in DIAGNOSTIC_READONLY_CALLEES:
            effects = diagnostic_readonly_effects(obj, symbol)
        else:
            effects = None
        preserving.append(FatalFunction(
            key, frame.bytes_used, hashlib.sha256(obj.data).hexdigest(), sources[key],
            () if effects is None else effects.writes, () if effects is None else effects.systems,
            () if effects is None else effects.dependencies,
        ))
    name = "CMakeFiles/micros.dir/arch/riscv64/panic.S.obj"
    obj = load_object(owned_file(directory, name))
    capture = assembly_object_evidence(root, obj, "arch/riscv64/panic.S", variant, image_name)
    verify_linked_object(obj, name, image, text)
    symbols = function_symbols(obj)
    locations = tuple(FunctionLocation(
        frame.function, name, obj.sections[symbols[frame.function.symbol].section_index].name,
        symbols[frame.function.symbol].value, symbols[frame.function.symbol].size,
    ) for frame in capture.frames)
    if len(join_function_locations(locations, text, image.symbols)) != 2:
        raise StackEvidenceError("terminal latch lacks both actual linked panic capture entries")
    preserving.extend(FatalFunction(
        frame.function, frame.bytes_used, hashlib.sha256(obj.data).hexdigest(), (),
        relations=(("fixed-stack-only-panic-capture", frame.bytes_used),),
    ) for frame in capture.frames)
    name = "CMakeFiles/micros.dir/arch/riscv64/panic_trace.S.obj"
    obj = load_object(owned_file(directory, name))
    helpers = assembly_object_evidence(
        root, obj, "arch/riscv64/panic_trace.S", variant, image_name,
        frozenset({"MICROS_BUILD_PANIC_TRACE_TEST"}) if artifacts.trace_test else frozenset(),
    )
    verify_linked_object(obj, name, image, text)
    symbols = function_symbols(obj)
    locations = tuple(FunctionLocation(
        frame.function, name, obj.sections[symbols[frame.function.symbol].section_index].name,
        symbols[frame.function.symbol].value, symbols[frame.function.symbol].size,
    ) for frame in helpers.frames)
    if len(join_function_locations(locations, text, image.symbols)) != 2:
        raise StackEvidenceError("trace helpers lack their exact actual linked ranges")
    preserving.extend(FatalFunction(
        frame.function, 0, hashlib.sha256(obj.data).hexdigest(), (),
        relations=(("closed-frame-register-or-admitted-word-helper", frame.function.symbol),),
    ) for frame in helpers.frames)
    if artifacts.trace_test:
        name = "CMakeFiles/micros.dir/tests/qemu/panic_trace_kernel.S.obj"
        obj = load_object(owned_file(directory, name))
        fixture = assembly_object_evidence(
            root, obj, "tests/qemu/panic_trace_kernel.S", variant, image_name,
            frozenset({"MICROS_BUILD_PANIC_TRACE_TEST"}),
        )
        verify_linked_object(obj, name, image, text)
        symbols = function_symbols(obj)
        locations = tuple(FunctionLocation(
            frame.function, name, obj.sections[symbols[frame.function.symbol].section_index].name,
            symbols[frame.function.symbol].value, symbols[frame.function.symbol].size,
        ) for frame in fixture.frames)
        if len(join_function_locations(locations, text, image.symbols)) != 6:
            raise StackEvidenceError("trace fixture lost a complete linked assembly owner")
        preserving.extend(FatalFunction(
            frame.function, frame.bytes_used, hashlib.sha256(obj.data).hexdigest(),
            tuple(call for call in fixture.calls if call.caller == frame.function),
            relations=fixture.effects,
        ) for frame in fixture.frames)
    if observed_mode:
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
    return tuple(preserving)


def establishing_fatal_calls(artifacts, console_symbol, *, observed_mode):
    variant, image_name = artifacts.variant, artifacts.image_name
    records, flows, sources = artifacts.records, artifacts.flows, artifacts.sources
    uart_key = FunctionKey(variant, image_name, UART, "uart_panic_seize")
    obj, symbol, _ = records[uart_key]
    uart_machine_effects(
        obj, symbol, terminal=True, terminal_test=artifacts.terminal_test,
        trace_test=artifacts.trace_test, console=console_symbol,
    )
    console_call, = (call for call in flows[uart_key].calls if call.target_symbol == "micros_uart_console_panic")
    inputs = register_inputs(obj, symbol)
    if inputs[console_call.offset][10] != (
        "symbol", console_symbol.name, console_symbol.section_index, console_symbol.value,
    ):
        raise StackEvidenceError("UART seizure borrowed a same-shaped foreign console argument")
    establishment = []
    for name, producer_name in (
        ("micros_panic_seize", "uart_panic_seize"),
        ("panic_report_claimed", "micros_panic_seize"),
    ):
        key = FunctionKey(variant, image_name, REPORT, name)
        obj, symbol, _ = records[key]
        reporter_effects(obj, symbol)
        producer, = (call for call in flows[key].calls if call.target_symbol == producer_name)
        dominated = normal_return_dominated_calls(obj, symbol, producer.offset)
        declared = tuple(row for row in sources[key] if row.offset > producer.offset)
        if observed_mode:
            declared += tuple(SourceCall(
                key, hook.offset, hook.target_symbol, artifacts.origins[key].source,
                ("compiler-hook", hook.target_symbol, "self", "entry-return-address"), (), 0,
            ) for caller, hook in artifacts.hook_sites if caller == key and hook.offset > producer.offset)
        if dominated != frozenset(row.offset for row in declared):
            raise StackEvidenceError("fatal post-transition source/machine dominance inventory differs")
        allowed = {
            "uart_write", "uart_write_hex64", "uart_flush", "sbi_system_reset",
            "micros_diagnostic_panic_report", "micros_panic_trace_report", *HOOK_SYMBOLS,
        }
        if artifacts.terminal_test:
            allowed.add("micros_terminal_test_after_seize")
        elif artifacts.trace_test:
            allowed.add("micros_panic_trace_test_after_core")
        if any(row.target not in allowed for row in declared):
            raise StackEvidenceError("fatal ready continuation reaches an unbound preservation call")
        establishment.extend(declared)
    return tuple(establishment)


def fatal_exclusions(artifacts, console_symbol):
    variant, image_name = artifacts.variant, artifacts.image_name
    records, flows, sources = artifacts.records, artifacts.flows, artifacts.sources
    guard_key = FunctionKey(variant, image_name, UART, "uart_write_direct")
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
    predicate_source, = (
        row for row in sources[guard_key] if row.offset == predicate.offset
    )
    exclusions = [FatalExclusion("direct-console-owner", predicate_source, guard)]
    for name, role in (("uart_write", "text-recording-arm"), ("uart_write_hex64", "hex-recording-arm")):
        key = FunctionKey(variant, image_name, UART, name)
        obj, symbol, _ = records[key]
        predicate_offset, excluded_offset, _ = diagnostic_recording_guard(obj, symbol)
        predicate_source, = (row for row in sources[key] if row.offset == predicate_offset)
        excluded_source, = (row for row in sources[key] if row.offset == excluded_offset)
        exclusions.append(FatalExclusion(role, predicate_source, excluded_source))
    key = FunctionKey(variant, image_name, REPORT, "micros_panic_report")
    obj, symbol, _ = records[key]
    terminal_reporter_effects(obj, symbol)
    predicate, = (row for row in sources[key] if row.target == "micros_panic_claim_first")
    guard, = (row for row in sources[key] if row.target == "panic_report_claimed")
    result_zero_gate(obj, symbol, predicate.offset, bits=1)
    exclusions.append(FatalExclusion("first-report-body", predicate, guard, Qualifier.PANIC_LATCHED))
    return tuple(exclusions)


def fatal_sbi_inventory(directory, catalog, artifacts):
    variant, image_name = artifacts.variant, artifacts.image_name
    frames, objects = artifacts.frames, artifacts.objects
    records, sources = artifacts.records, artifacts.sources
    sbi_inventory = []
    claims = []
    for key, relative in catalog.objects:
        if relative not in objects:
            objects[relative] = load_object(owned_file(directory, relative))
        obj = objects[relative]
        symbol = function_symbols(obj)[key.symbol]
        flow = function_flow(obj, symbol)
        claims.extend((key, call.offset) for call in flow.calls if call.target_symbol == "micros_panic_claim_reset")
        ecalls = tuple(point for point, instruction in flow.instructions.items() if instruction == (4, 0x73))
        if not ecalls:
            continue
        if key.translation_unit != SBI or key.symbol not in ("sbi_set_timer", "sbi_system_reset") or len(ecalls) != 1:
            raise StackEvidenceError("fatal firmware scope has an unknown C SBI/SSE ecall")
        if key.symbol == "sbi_system_reset":
            effects = terminal_srst_effects(obj, symbol, test_observer=artifacts.terminal_test, trace_observer=artifacts.trace_test)
        else:
            values = register_inputs(obj, symbol)[ecalls[0]]
            if (values[16], values[17]) != (("scalar", 0), ("scalar", 0x54494d45)):
                raise StackEvidenceError("TIME wrapper changed the closed SBI extension inventory")
            effects = sbi_machine_effects(obj, symbol)
        sbi_inventory.append(FatalFunction(
            key, frames[key].bytes_used, hashlib.sha256(obj.data).hexdigest(), sources[key],
            systems=tuple((point, 0x73) for point in ecalls),
            relations=effects.dependencies,
        ))
    if len(claims) != 1 or (claims[0][0].translation_unit, claims[0][0].symbol) != (SBI, "sbi_system_reset"):
        raise StackEvidenceError("failure reset has a caller-side preclaim or an unowned admission site")
    report_key = FunctionKey(variant, image_name, REPORT, "panic_report_claimed")
    reset, = (row for row in sources[report_key] if row.target == "sbi_system_reset")
    values = register_inputs(*records[report_key][:2])[reset.offset]
    if (values[10], values[11]) != (("scalar", 0), ("scalar", 1)):
        raise StackEvidenceError("ready reset site changed its scalar shutdown/failure operands")
    return tuple(sbi_inventory)


def derive_fatal_context(root, directory, catalog, *, qemu):
    root, directory = Path(root), Path(directory)
    cache = read_regular(owned_file(directory, "CMakeCache.txt"))[0].decode("utf-8").splitlines()
    if [line for line in cache if line.startswith("MICROS_QEMU:")] != [
        "MICROS_QEMU:FILEPATH=" + str(qemu),
    ]:
        raise StackEvidenceError("fatal firmware resolver borrowed another configured QEMU")
    observed_mode = [line.partition("=")[2] for line in cache if line.startswith(
        "MICROS_STACK_BUILD_MODE:"
    )] == ["observed"]
    tty_test = "MICROS_BUILD_TTY_SERVICE_TEST:BOOL=ON" in cache
    terminal_test = "MICROS_BUILD_TERMINAL_FAILURE_TEST:BOOL=ON" in cache
    trace_test = "MICROS_BUILD_PANIC_TRACE_TEST:BOOL=ON" in cache
    nested_test = "MICROS_BUILD_NESTED_TRAP_TEST:BOOL=ON" in cache
    if terminal_test and (tty_test or observed_mode):
        raise StackEvidenceError("terminal fixture borrowed an unapproved combined observation configuration")
    if trace_test and (terminal_test or tty_test or observed_mode or nested_test):
        raise StackEvidenceError("trace fixture borrowed an unapproved combined observation configuration")
    policy = trace_compile_policy(root, directory)
    require_fatal_source_units(root, terminal_test=terminal_test, trace_test=trace_test)
    artifacts = collect_fatal_artifacts(
        root, directory, catalog, observed_mode=observed_mode, tty_test=tty_test, terminal_test=terminal_test,
        trace_test=trace_test, nested_test=nested_test,
    )
    console_symbol, console, history, counter_symbol, counter, _, control, prelude_objects, test_objects = bind_fatal_state(
        root, artifacts, tty_test=tty_test,
    )
    prelude_effects, prelude = prelude_preservation(artifacts, prelude_objects)
    trace_effects, ownership = trace_preservation(root, artifacts, (console, history, control, *prelude_objects, *test_objects))
    fixture_effects, fixture = trace_fixture_preservation(root, artifacts, ownership, test_objects, trace_effects)
    if set(trace_effects) & set(fixture_effects):
        raise StackEvidenceError("trace fixture borrowed a production effect owner")
    trace_effects.update(fixture_effects)
    fixture += trace_fault_register(
        root, directory, catalog, artifacts, ownership, test_objects, (console, history, control, *prelude_objects),
    )
    test_arguments = terminal_observer_arguments(artifacts, test_objects) + trace_fixture_arguments(artifacts, test_objects)
    preserving = preserving_fatal_functions(
        root, directory, artifacts, console_symbol, counter_symbol, prelude_effects, test_objects,
        observed_mode=observed_mode, tty_test=tty_test,
        trace_effects=trace_effects,
    )
    establishment = establishing_fatal_calls(artifacts, console_symbol, observed_mode=observed_mode)
    exclusions = fatal_exclusions(artifacts, console_symbol)
    latching = (exclusions[-1].edge,)
    sbi_inventory = fatal_sbi_inventory(directory, catalog, artifacts)
    firmware = resolve_default_firmware(qemu)
    trace = trace_register(
        artifacts, trace_effects, ownership, preserving, policy, trace_frame_witnesses(directory, catalog, artifacts),
        fixture,
    )
    return FatalContextProof(
        version=4,
        bindings=ContextBindings(
            tuple((row.function, row.offset) for row in establishment),
            tuple(row.function for row in preserving),
            tuple((row.edge.function, row.edge.offset) for row in exclusions),
            latching=tuple((row.function, row.offset) for row in latching),
            latch_preservers=tuple(row.function for row in preserving),
        ),
        console=console, history=history, establishment=establishment,
        preservation=preserving, exclusions=exclusions,
        sbi_inventory=sbi_inventory, firmware=firmware,
        control=control, latch_establishment=latching, prelude=prelude,
        compiler_hooks=artifacts.hook_sites, test_counter=counter, test_objects=test_objects,
        test_arguments=test_arguments,
        trace_register=trace,
    )

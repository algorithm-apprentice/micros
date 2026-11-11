"""Finite current callback targets anchored to merged design source."""

from dataclasses import replace
from functools import lru_cache
from pathlib import Path
import re

from tests.host.c_source_records import c_declaration_text, c_lexical_text, compact, function_records, tokens
from tests.host.evidence_records import regular_source
from tests.host.stack_source_projection import project_stack_source
from tests.host.diagnostic_history_source import project_diagnostic_callback_source
from tools.stack_bound_callback_contracts import GROUPS
from tools.stack_bound_callbacks import require_callback_argument
from tools.stack_bound_objects import call_instructions, function_symbols, load_object
from tools.stack_bound_switch import switch_jumps
from tools.stack_bound_types import CallSite, FunctionKey, StackEvidenceError
from tools.validation_inputs import _run


AUTHORITY_COMMIT = "fc98edd26505ac522d3752ad85f09366e8085f32"
@lru_cache(maxsize=128)
def reference_source(root, unit):
    return _run(("git", "show", AUTHORITY_COMMIT + ":" + unit), cwd=root).decode()


def anchored_function(root, key, source_unit=None):
    unit = key.translation_unit if source_unit is None else source_unit
    source = project_stack_source(unit, (Path(root) / unit).read_text())
    try:
        source = project_diagnostic_callback_source(unit, source)
    except AssertionError as error:
        raise StackEvidenceError(f"diagnostic callback constructor: {error}") from error
    actual = function_records(source).get(key.symbol)
    expected = function_records(reference_source(str(root), unit)).get(key.symbol)
    if (
        actual is None or expected is None
        or tokens(actual.signature) != tokens(expected.signature)
        or actual.guards != expected.guards or tokens(actual.body) != tokens(expected.body)
    ):
        raise StackEvidenceError("callback source-stage function anchor changed")
    return actual


def parameter_names(signature):
    name_at = signature.rfind("(")
    parameters = signature[name_at + 1:signature.rfind(")")].split(",")
    result = []
    for parameter in parameters:
        match = re.search(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*(?:\[[0-9]+\]\s*)*$", parameter)
        if match is None:
            raise StackEvidenceError("callback carrier has an unsupported parameter declaration")
        result.append(match[1])
    return tuple(result)


def group_provider_targets(root, group, key, definitions, flags):
    if group not in GROUPS or type(flags) is not frozenset:
        raise StackEvidenceError("unknown finite callback group or build flags")
    constructors = list(group.constructors)
    providers = {member: list(entries) for member, _, entries in group.members}
    if group.identifier == "kernel-visitor" and "MICROS_BUILD_ADDRESS_SPACE_HANDOFF_TEST" in flags:
        providers["visitor"].append((
            "kernel/tests/memory/address_space_handoff_test_snapshot.c",
            "handoff_test_record_managed_mapping",
        ))
        constructors.append((
            "kernel/tests/memory/address_space_handoff_test_tty.c", "verify_tty_stage_1107",
        ))
    if group.identifier == "tty-uart" and "MICROS_BUILD_TTY_SERVICE_TEST" in flags:
        providers["read"].append(("servers/tty/tty_service.c", "read_deferred_uart"))
        providers["write"].append(("servers/tty/tty_service.c", "defer_uart_write"))
        constructors.append(("servers/tty/tty_service.c", "prepare_ready_interrupt_race"))
    for unit, name in constructors:
        try:
            anchored_function(root, FunctionKey(key.variant, key.image, unit, name))
        except StackEvidenceError as error:
            raise StackEvidenceError(f"{group.identifier} provider constructor: {error}") from error
    if group.identifier == "kernel-visitor":
        entries = providers["visitor"]
        if len(entries) != len(constructors):
            raise StackEvidenceError("kernel-visitor has an incomplete constructor/provider declaration")
        active = []
        for (unit, name), (constructor_unit, constructor_name) in zip(entries, constructors):
            provider = FunctionKey(key.variant, key.image, unit, name)
            constructor = FunctionKey(key.variant, key.image, constructor_unit, constructor_name)
            if (provider in definitions) != (constructor in definitions):
                raise StackEvidenceError("kernel-visitor retained only half a constructor/provider pair")
            if provider in definitions:
                active.append((unit, name))
        providers["visitor"] = active
    result = {}
    for member, entries in providers.items():
        targets = tuple(FunctionKey(key.variant, key.image, unit, name) for unit, name in entries)
        if not targets or any(target not in definitions for target in targets):
            raise StackEvidenceError(f"{group.identifier} has an incomplete linked provider table")
        result[member] = targets
    return result


def source_callback_occurrences(record, group):
    body = compact(c_lexical_text(record.body))
    result = []
    for carrier in group.carriers:
        for member, displacement, _ in group.members:
            expression = carrier.expression + ("->" + member if member else "")
            pattern = r"\b" + r"\s*".join(re.escape(token) for token in tokens(expression)) + r"\s*\("
            for match in re.finditer(pattern, body):
                if body[:match.start()].rstrip().endswith((".", ">")):
                    continue
                path = carrier.prefix + (() if displacement is None else (displacement,))
                result.append((match.start(), carrier, member, path))
    result.sort(key=lambda item: item[0])
    if len({position for position, *_ in result}) != len(result):
        raise StackEvidenceError("ambiguous callback source occurrence")
    return tuple(result)


def proof_header_tokens(source, protected):
    """Keep types, macros, storage and proof APIs, not unrelated prototypes."""
    lexical = c_declaration_text(source)
    ordinary = re.compile(
        r"[A-Za-z_][A-Za-z0-9_\s*]*\b[A-Za-z_][A-Za-z0-9_]*\s*"
        r"\([^(){};]*\)\s*;"
    )
    removed = []
    for match in ordinary.finditer(lexical):
        declaration = tokens(match[0])
        if (
            lexical.count("{", 0, match.start()) != lexical.count("}", 0, match.start())
            or set(declaration) & {"typedef", "_Noreturn", *protected}
            or re.search(r"(?m)^\s*#", source[match.start():match.end()])
        ):
            continue
        removed.append((match.start(), match.end()))
    for start, end in reversed(removed):
        source = source[:start] + source[end:]
    return tokens(source)


def require_callback_type_scope(root, group, units):
    if group not in GROUPS:
        raise StackEvidenceError("unknown callback header contract")
    try:
        header = regular_source(root, group.header)
        reference = reference_source(str(root), group.header)
        if proof_header_tokens(header.read_text(), group.types) != proof_header_tokens(reference, group.types):
            raise StackEvidenceError(f"{group.identifier} callback header contract changed")
    except (ValueError, AssertionError, OSError) as error:
        raise StackEvidenceError(f"{group.identifier} callback header contract: {error}") from error
    expression = r"\b(?:" + "|".join(map(re.escape, group.types)) + r")\b"
    callback_free = set()
    for unit in units:
        if not unit.startswith(group.source_prefix):
            continue
        try:
            actual = function_records(regular_source(root, unit).read_text())
        except (AssertionError, ValueError, OSError) as error:
            raise StackEvidenceError(f"{group.identifier} callback source scope: {error}") from error
        selected = {
            name for name, record in actual.items()
            if re.search(expression, record.signature + record.body)
            or source_callback_occurrences(record, group)
        }
        if not selected:
            callback_free.add(unit)
        for name in selected:
            try:
                anchored_function(root, FunctionKey("source", "source.elf", unit, name))
            except StackEvidenceError as error:
                raise StackEvidenceError(f"{group.identifier} callback type/argument scope: {error}") from error
    return frozenset(callback_free)


def configured_callback_flags(directory):
    result = set()
    for line in (Path(directory) / "CMakeCache.txt").read_text().splitlines():
        match = re.fullmatch(r"(MICROS_BUILD_[A-Z0-9_]+):BOOL=(ON|OFF)", line)
        if match and match[2] == "ON":
            result.add(match[1])
    return frozenset(result)


def bind_current_callbacks(root, directory, catalog):
    definitions = {definition.function for definition in catalog.definitions}
    objects = dict(catalog.objects)
    origins = {origin.function: origin.source for origin in catalog.origins}
    functions = {frame.function for frame in catalog.frames}
    if (
        len(objects) != len(catalog.objects) or set(objects) != functions
        or len(origins) != len(catalog.origins) or set(origins) != functions
    ):
        raise StackEvidenceError("callback catalog has an incomplete qualified object inventory")
    flags = configured_callback_flags(directory)
    grouped = {}
    for call in catalog.pending:
        if call.target_symbol is None:
            grouped.setdefault(call.caller, []).append(call)
    result, claimed, sources, machine, loaded = [], set(), {}, {}, {}
    for group in GROUPS:
        candidates = [
            frame.function for frame in catalog.frames
            if frame.function.translation_unit.startswith(group.source_prefix)
        ]
        if not candidates:
            continue
        require_callback_type_scope(root, group, {key.translation_unit for key in candidates})
        for key in candidates:
            origin = origins[key]
            if not group.source_prefix.endswith("/") and key.translation_unit != group.source_prefix:
                raise StackEvidenceError("callback source is outside its qualified translation unit")
            try:
                if origin not in sources:
                    sources[origin] = function_records(regular_source(root, origin).read_text())
                record = sources[origin].get(key.symbol)
            except (AssertionError, ValueError, OSError) as error:
                raise StackEvidenceError(f"callback source origin: {error}") from error
            if record is None:
                raise StackEvidenceError("callback catalog has no actual source function")
            if origin != key.translation_unit:
                anchored_function(root, key, origin)
            if key not in machine:
                path = objects[key]
                if path not in loaded:
                    obj = load_object(Path(directory) / path)
                    loaded[path] = obj, function_symbols(obj)
                obj, symbols = loaded[path]
                symbol = symbols.get(key.symbol)
                if symbol is None:
                    raise StackEvidenceError("callback catalog has no actual machine symbol")
                calls = tuple(sorted(grouped.get(key, ()), key=lambda call: call.offset))
                actual = tuple(
                    call
                    for call in call_instructions(obj, symbol, key, switch_jumps(obj, symbol))
                    if call.target_symbol is None
                )
                if calls != actual:
                    raise StackEvidenceError("callback machine-indirect occurrence inventory is incomplete")
                machine[key] = obj, symbol, calls
            obj, symbol, calls = machine[key]
            if key.translation_unit == origin == "servers/vm/vm_load.c" and key.symbol == "vm_load_prepare_transaction":
                from tools.stack_bound_vm_lifetime import require_vm_prepare_lifetime

                require_vm_prepare_lifetime(root, obj)
            expected = source_callback_occurrences(record, group)
            if not expected:
                if calls and not any(
                    source_callback_occurrences(record, other)
                    for other in GROUPS if key.translation_unit.startswith(other.source_prefix)
                ):
                    raise StackEvidenceError("callback-free source has an actual unbound indirect call")
                continue
            record = anchored_function(root, key, origin)
            if len(calls) != len(expected):
                raise StackEvidenceError(f"{group.identifier} source/machine occurrence inventory is incomplete")
            targets = group_provider_targets(root, group, key, definitions, flags)
            parameters = parameter_names(record.signature)
            for call, (_, carrier, member, path) in zip(calls, expected):
                if parameters.count(carrier.parameter) != 1:
                    raise StackEvidenceError("callback source has no exact carrier parameter")
                require_callback_argument(
                    obj, symbol, call.offset, parameters.index(carrier.parameter), path,
                )
                bound = targets[member]
                if group.identifier == "tty-uart" and carrier.expression == "write->source":
                    bound = tuple(target for target in bound if target.symbol == "uart_read")
                site = (key, call.offset)
                if site in claimed or not bound:
                    raise StackEvidenceError("duplicate or empty callback binding")
                claimed.add(site)
                result.append(CallSite(key, call.offset, bound, call.kind))
    pending = tuple(call for call in catalog.pending if (call.caller, call.offset) not in claimed)
    return tuple(result), pending


def bind_current_indirect_calls(root, directory, catalog):
    from tools.stack_bound_test_tables import bind_current_test_tables

    callbacks, pending = bind_current_callbacks(root, directory, catalog)
    tables, pending = bind_current_test_tables(
        root, directory,
        replace(catalog, calls=catalog.calls + callbacks, pending=pending),
    )
    return callbacks + tables, pending

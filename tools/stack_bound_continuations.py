"""Saved supervisor continuations retain their exact live C caller chain."""

from dataclasses import dataclass, replace
from pathlib import Path

from tests.host.c_source_records import tokens
from tests.host.stack_source_projection import project_stack_source
from tools.stack_artifact_files import read_regular
from tools.stack_bound_bindings import anchored_function, reference_source
from tools.stack_bound_flow import function_flow
from tools.stack_bound_link import text_owners
from tools.stack_bound_objects import function_symbols, instructions, load_object, relocations, section_bytes
from tools.stack_bound_pointer import memory_action
from tools.stack_bound_relocations import linked_offset, verify_linked_object
from tools.stack_bound_types import CallSite, FunctionKey, StackEvidenceError
from tools.stack_bound_values import register_inputs
from tools.user_elf_parse import load_elf


CONTINUATIONS = (
    (
        "arch/riscv64/grant_syscall_test.S", "micros_grant_syscall_test_supervisor_resume",
        "kernel/tests/grant/grant_syscall_test.c", "micros_grant_syscall_runtime_run_self_test",
        "kernel/tests/grant/grant_syscall_test.c", "micros_grant_syscall_test_saved_sp",
    ),
    (
        "arch/riscv64/ipc_ecall_test.S", "micros_ipc_ecall_test_supervisor_resume",
        "kernel/tests/ipc/ipc_ecall_test.c", "micros_ipc_ecall_core_runtime_run_self_test",
        "kernel/tests/ipc/ipc_ecall_test_fixture.c", "micros_ipc_ecall_test_saved_sp",
    ),
    (
        "arch/riscv64/ipc_syscall_test.S", "micros_ipc_syscall_test_supervisor_resume",
        "kernel/tests/ipc/ipc_syscall_test.c", "micros_ipc_syscall_runtime_run_self_test",
        "kernel/tests/ipc/ipc_syscall_test_fixture.c", "micros_ipc_syscall_test_saved_sp",
    ),
    (
        "arch/riscv64/user_runtime_test.S", "micros_user_runtime_test_supervisor_resume",
        "kernel/tests/runtime/user_runtime_test.c", "micros_user_runtime_runtime_run_self_test",
        "kernel/tests/runtime/user_runtime_test.c", "micros_user_runtime_test_saved_sp",
    ),
)


@dataclass(frozen=True)
class SavedStackPair:
    capture: int
    restore: int
    symbol: str
    frame: int


@dataclass(frozen=True)
class SavedContinuation:
    caller: FunctionKey
    resume: FunctionKey
    storage: str
    address: int
    pair: SavedStackPair


@dataclass(frozen=True)
class SnapshotStorage:
    function: FunctionKey
    section: int
    entry_bottom: int
    entry_top: int
    bottom: int
    top: int
    bytes_used: int
    entry_offset: int
    resume_offset: int
    capacity: int = 256


@dataclass(frozen=True)
class SnapshotBinding:
    storage: SnapshotStorage
    setter: FunctionKey | None
    setter_offset: int | None


SNAPSHOT_SYMBOLS = (
    ("entry_bottom", "micros_trap_test_entry_stack_bottom"),
    ("entry_top", "micros_trap_test_entry_stack_top"),
    ("bottom", "micros_trap_test_snapshot"),
    ("top", "micros_trap_test_return_sp"),
)


def snapshot_object_evidence(obj, symbol, key):
    selected = {}
    for field, name in SNAPSHOT_SYMBOLS:
        matches = tuple(row for row in obj.symbols if row.name == name)
        if len(matches) != 1 or matches[0].binding != 1 or matches[0].symbol_type != 0:
            raise StackEvidenceError("trap snapshot lacks its exact declared storage symbols")
        selected[field] = matches[0]
    sections = {row.section_index for row in selected.values()}
    if len(sections) != 1:
        raise StackEvidenceError("trap snapshot storage borrowed another object section")
    section = obj.sections[next(iter(sections))]
    values = {field: row.value for field, row in selected.items()}
    if (
        section.section_type != 8 or section.flags != 3
        or values["entry_top"] - values["entry_bottom"] != 256
        or values["top"] - values["bottom"] != 256
        or not 0 <= values["entry_bottom"] < values["entry_top"] <= values["bottom"] < values["top"] <= section.size
    ):
        raise StackEvidenceError("trap snapshot exceeds or aliases its separate 256-byte buffers")
    code_section = obj.sections[symbol.section_index]
    code = tuple(instructions(
        section_bytes(obj, code_section)[symbol.value:symbol.value + symbol.size], symbol.value,
    ))
    stores = tuple((point, action) for point, width, word in code
                   if (action := memory_action(width, word)) is not None and action[3] == 2)
    if tuple((action[0], action[1], action[4]) for _, action in stores) != tuple(
        (True, 8, offset) for offset in range(-256, 0, 8)
    ):
        raise StackEvidenceError("trap snapshot has an unbound stack-relative read/write footprint")
    resumes = tuple(row for row in obj.symbols if row.name == "micros_trap_test_resume")
    entries = tuple(point + 4 for point, kind, target, _ in relocations(obj, symbol.section_index)
                    if kind == 23 and target.name == "micros_trap_test_entry_stack_top")
    if len(resumes) != 1 or len(entries) != 1 or resumes[0].value != stores[0][0]:
        raise StackEvidenceError("trap snapshot lost its exact entry or returned-store boundary")
    return SnapshotStorage(
        key, section.index, **values, bytes_used=256,
        entry_offset=entries[0] - symbol.value, resume_offset=resumes[0].value - symbol.value,
    )


def prove_snapshot_return(obj, symbol):
    flow, inputs = function_flow(obj, symbol), register_inputs(obj, symbol)
    stores = []
    for point, (width, word) in flow.instructions.items():
        action = memory_action(width, word)
        if action is None or not action[0] or point not in inputs:
            continue
        _, size, register, basis, offset = action
        if inputs[point][basis] == ("argument", 0, ()) and offset == 8:
            value = inputs[point][register]
            if size != 8 or value is None or value[:2] != ("symbol", "micros_trap_test_return_sp") or value[3] != 0:
                raise StackEvidenceError("trap snapshot return borrowed another stack address")
            stores.append(point)
    if len(stores) != 1:
        raise StackEvidenceError("trap snapshot lacks one exact return-SP field publication")
    return stores[0]


def bind_trap_snapshots(root, directory, catalog, graph):
    definitions = {row.function: row for row in graph.definitions}
    unit, name = "kernel/tests/trap/trap_test.c", "trap_test_prepare_return"
    setters = tuple(key for key in definitions if (key.translation_unit, key.symbol) == (unit, name))
    if not graph.snapshots:
        if setters:
            raise StackEvidenceError("trap snapshot C return has no bound assembly storage")
        return ()
    root, directory = Path(root), Path(directory)
    image_name = graph.snapshots[0].function.image
    image = load_elf(directory / image_name)
    text = read_regular((directory / image_name).with_suffix(".map"))[0].decode()
    result = []
    for snapshot in graph.snapshots:
        relative = "CMakeFiles/micros.dir/" + snapshot.function.translation_unit + ".obj"
        obj = load_object(directory / relative)
        sections = verify_linked_object(obj, relative, image, text)
        if snapshot.section not in sections:
            raise StackEvidenceError("trap snapshot storage was not linked to its current image")
        addresses = {field: sections[snapshot.section] + getattr(snapshot, field) for field, _ in SNAPSHOT_SYMBOLS}
        for field, symbol_name in SNAPSHOT_SYMBOLS:
            matches = tuple(row for row in image.symbols if row.name == symbol_name and row.binding == 1)
            if len(matches) != 1 or matches[0].value != addresses[field] or matches[0].symbol_type != 0:
                raise StackEvidenceError("trap snapshot has a borrowed linked buffer address")
        symbol = function_symbols(obj)[snapshot.function.symbol]
        base = sections[symbol.section_index]
        offsets = {
            field: linked_offset(obj, symbol.section_index, symbol.value + getattr(snapshot, field), base)
            - linked_offset(obj, symbol.section_index, symbol.value, base)
            for field in ("entry_offset", "resume_offset")
        }
        storage = replace(snapshot, **addresses, **offsets)
        setter, point = None, None
        if setters:
            if len(setters) != 1:
                raise StackEvidenceError("trap snapshot return has multiple C owners")
            setter, = setters
            if tokens(read_regular(root / unit)[0].decode()) != tokens(reference_source(str(root), unit)):
                raise StackEvidenceError("trap snapshot context-publication source changed")
            anchored_function(root, setter)
            output = dict(catalog.objects)[setter]
            owner = load_object(directory / output)
            function = function_symbols(owner)[name]
            point = prove_snapshot_return(owner, function)
            owned = verify_linked_object(owner, output, image, text)[function.section_index]
            point = linked_offset(owner, function.section_index, function.value + point, owned) - linked_offset(
                owner, function.section_index, function.value, owned,
            )
        elif any(snapshot.function in edge.targets for edge in graph.edges):
            raise StackEvidenceError("reachable trap snapshot lacks its exact C return owner")
        result.append(SnapshotBinding(storage, setter, point))
    return tuple(result)


def prove_saved_stack_pair(capture_obj, capture, resume_obj, resume, name, frame):
    flow, inputs = function_flow(capture_obj, capture), register_inputs(capture_obj, capture)
    stores = []
    for point, (width, word) in flow.instructions.items():
        action = memory_action(width, word)
        if action is None or not action[0] or point not in inputs:
            continue
        _, size, register, basis, offset = action
        base = inputs[point][basis]
        if base is not None and base[0:2] == ("symbol", name):
            if size != 8 or offset != 0 or base[3] != 0 or inputs[point][register] != ("stack", -frame):
                raise StackEvidenceError("saved supervisor SP does not retain its original complete caller frame")
            stores.append(point)
    if len(stores) != 1:
        raise StackEvidenceError("saved supervisor SP lacks one exact complete capture")
    resume_flow, resume_inputs = function_flow(resume_obj, resume), register_inputs(resume_obj, resume)
    restores = []
    for point, (width, word) in resume_flow.instructions.items():
        action = memory_action(width, word)
        if action is not None and action[:3] == (False, 8, 2):
            base = resume_inputs[point][action[3]]
            if action[4] != 0 or base is None or base[0:2] != ("symbol", name) or base[3] != 0:
                raise StackEvidenceError("supervisor return borrowed another saved stack owner")
            restores.append(point)
    if len(restores) != 1:
        raise StackEvidenceError("supervisor return lacks its exact saved-SP restoration")
    return SavedStackPair(stores[0], restores[0], name, frame)


def bind_saved_supervisor_continuations(root, directory, catalog, graph):
    root, directory = Path(root), Path(directory)
    definitions = {row.function: row for row in graph.definitions}
    if not any(row[1] == key.symbol for row in CONTINUATIONS for key in definitions):
        return (), ()
    objects = dict(catalog.objects)
    frames = {row.function: row for row in catalog.frames}
    image_name = graph.frames[0].function.image
    variant = graph.frames[0].function.variant
    image = load_elf(directory / image_name)
    text = read_regular((directory / image_name).with_suffix(".map"))[0].decode("utf-8")
    edges, records = [], []
    for asm_unit, resume_name, caller_unit, caller_name, storage_unit, name in CONTINUATIONS:
        resume_key = FunctionKey(variant, image_name, asm_unit, resume_name)
        if resume_key not in definitions:
            continue
        caller_key = FunctionKey(variant, image_name, caller_unit, caller_name)
        if caller_key not in definitions or caller_key not in frames:
            raise StackEvidenceError("saved supervisor continuation omitted its live original caller")
        for unit in (caller_unit, storage_unit):
            actual = project_stack_source(unit, read_regular(root / unit)[0].decode("utf-8"))
            if tokens(actual) != tokens(reference_source(str(root), unit)):
                raise StackEvidenceError("saved continuation source/storage stage changed")
        anchored_function(root, caller_key)
        caller_obj = load_object(directory / objects[caller_key])
        resume_name_in_map = "CMakeFiles/micros.dir/" + asm_unit + ".obj"
        if resume_name_in_map not in {object_name for object_name, _ in text_owners(text)}:
            raise StackEvidenceError("supervisor continuation borrowed another source object")
        resume_obj = load_object(directory / resume_name_in_map)
        pair = prove_saved_stack_pair(
            caller_obj, function_symbols(caller_obj)[caller_name],
            resume_obj, function_symbols(resume_obj)[resume_name],
            name, frames[caller_key].bytes_used,
        )
        storage_name = "CMakeFiles/micros.dir/" + storage_unit + ".obj"
        storage_obj = load_object(directory / storage_name)
        sections = verify_linked_object(storage_obj, storage_name, image, text)
        owned = tuple(symbol for symbol in storage_obj.symbols if symbol.name == name)
        if len(owned) != 1:
            raise StackEvidenceError("saved stack has no unique source-object storage")
        storage, = owned
        section = storage_obj.sections[storage.section_index]
        if (
            storage.binding != 1 or storage.symbol_type != 1 or storage.size != 8 or storage.value != 0
            or section.section_type != 8 or section.flags != 3 or section.name != ".bss." + name
            or storage.section_index not in sections
        ):
            raise StackEvidenceError("saved stack borrowed another layout or object owner")
        address = sections[storage.section_index]
        emitted = tuple(symbol for symbol in image.symbols if (
            symbol.name == name and symbol.value == address and symbol.size == 8
            and symbol.symbol_type == 1 and symbol.binding == 1
        ))
        if len(emitted) != 1:
            raise StackEvidenceError("saved stack has no exact linked global owner")
        if any(edge.caller == caller_key and edge.offset == pair.capture for edge in graph.edges):
            raise StackEvidenceError("continuation capture collides with a machine call site")
        edges.append(CallSite(caller_key, pair.capture, (resume_key,)))
        records.append(SavedContinuation(caller_key, resume_key, storage_unit, address, pair))
    return tuple(edges), tuple(records)

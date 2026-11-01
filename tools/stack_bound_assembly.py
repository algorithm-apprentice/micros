"""Closed fixed-source RV64 assembly frames and explicit epoch boundaries."""

from dataclasses import dataclass, replace
from functools import lru_cache
import json
import os
from pathlib import Path
import subprocess
import tempfile

from tools.stack_artifact_files import read_regular
from tools.stack_bound_bindings import reference_source
from tools.stack_bound_bindings import configured_callback_flags
from tools.stack_bound_catalog import DefinedFunction, resolve_linked_calls
from tools.stack_bound_collect import image_map_text
from tools.stack_bound_link import FunctionLocation, join_function_locations, text_owners
from tools.stack_bound_machine import (
    branch_offset, require_c_frame_movement,
)
from tools.stack_bound_objects import (
    call_instructions, function_symbols, instructions, load_object, relocations, section_bytes,
)
from tools.stack_bound_panic import panic_frame_evidence
from tools.stack_bound_pointer import memory_action
from tools.stack_bound_relocations import linked_offset, verify_linked_object
from tools.stack_bound_types import CallSite, EdgeKind, Frame, FunctionKey, StackEvidenceError
from tools.user_elf_instructions import _instruction_destination
from tools.validation_context import target_compiler
from tools.validation_inputs import _file_identity
from tools.user_elf_parse import load_elf


CONTRACTS = {
    "arch/riscv64/boot.S": (("_start", 0, "boot"),),
    "arch/riscv64/trap.S": (
        ("micros_trap_entry", 288, "trap"), ("micros_riscv_trap_install", 0, "local"),
    ),
    "arch/riscv64/panic.S": (
        ("micros_panic_entry", 0, "panic"), ("micros_trap_panic_entry", 48, "panic"),
    ),
    "arch/riscv64/user_entry.S": (("micros_riscv_enter_user", 0, "user"),),
    "arch/riscv64/scheduler.S": (
        ("micros_scheduler_idle_pivot", 0, "idle"), ("micros_scheduler_idle_wait", 0, "local"),
    ),
    "arch/riscv64/mmu.S": tuple((name, 0, "local") for name in (
        "micros_riscv_activate_sv39", "micros_riscv_publish_page_table",
        "micros_riscv_publish_page_table_address",
    )),
    "lib/runtime/start.S": (("_start", 0, "local"),),
    "lib/runtime/raw_syscall.S": (("micros_runtime_raw_syscall", 0, "local"),),
    "lib/runtime/application_start.S": tuple((name, 0, "local") for name in (
        "_start", "micros_application_start_returned", "micros_application_startup_failed",
        "micros_application_exit_returned",
    )),
    "arch/riscv64/address_space_handoff_test.S": (
        ("micros_address_space_handoff_test_enter", 0, "local"),
        ("micros_address_space_handoff_test_supervisor_resume", 0, "resume"),
    ),
    "arch/riscv64/grant_syscall_test.S": (("micros_grant_syscall_test_supervisor_resume", 0, "resume"),),
    "arch/riscv64/ipc_ecall_test.S": (("micros_ipc_ecall_test_supervisor_resume", 0, "resume"),),
    "arch/riscv64/ipc_syscall_test.S": (("micros_ipc_syscall_test_supervisor_resume", 0, "resume"),),
    "arch/riscv64/ipc_syscall_panic_test.S": (),
    "arch/riscv64/mmu_test.S": (
        ("micros_mmu_text_probe", 0, "local"), ("micros_mmu_store_test_trigger", 0, "local"),
        ("micros_mmu_execute_test_trigger", 16, "nx-fault"),
    ),
    "arch/riscv64/nested_trap_test.S": (("micros_nested_trap_test_trigger", 0, "local"),),
    "arch/riscv64/scheduler_test.S": (
        ("micros_scheduler_test_enter", 0, "local"),
        ("micros_scheduler_test_supervisor_resume", 0, "resume"),
    ),
    "arch/riscv64/trap_test.S": (
        ("micros_trap_test_trigger", 0, "test-pivot"),
        ("micros_trap_sum_restore_test_trigger", 0, "local"),
        ("micros_trap_panic_test_trigger", 0, "local"),
    ),
    "arch/riscv64/user_address_space_test.S": (
        ("micros_user_address_space_sum_fault_trigger", 0, "local"),
        ("micros_user_address_space_sum_load", 0, "local"),
    ),
    "arch/riscv64/user_execution_test.S": (
        ("micros_user_execution_test_enter", 0, "local"),
        ("micros_user_execution_test_supervisor_resume", 0, "resume"),
    ),
    "arch/riscv64/user_runtime_test.S": (("micros_user_runtime_test_supervisor_resume", 0, "resume"),),
    "tests/qemu/bootstrap_probe.S": (
        ("micros_bootstrap_probe_report", 0, "local"), ("micros_bootstrap_probe_wait", 0, "local"),
    ),
    "tests/qemu/elf_spawn_report.S": (("micros_elf_spawn_report", 0, "local"),),
    "tests/qemu/pm_service_report.S": (("micros_pm_service_report", 0, "local"),),
    "tests/qemu/ramfs_service_report.S": (("micros_ramfs_service_test_report", 0, "local"),),
    "tests/qemu/tty_service_claim_wait.S": (("micros_tty_service_test_enable_and_wait", 16, "local"),),
    "tests/qemu/tty_service_report.S": (("micros_tty_service_test_report", 0, "local"),),
    "tests/qemu/user_runtime_probe.S": (
        ("micros_user_runtime_test_probe_rodata", 0, "local"),
        ("micros_user_runtime_test_raw_send_probe", 128, "local"),
    ),
    "tests/qemu/vfs_service_report.S": (("micros_vfs_service_test_report", 0, "local"),),
    "tests/qemu/vm_handoff_probe.S": (("micros_vm_handoff_probe_report", 0, "local"),),
    "tests/qemu/vm_scratch_preemption_peer.S": (
        ("micros_vm_scratch_preemption_peer_hold", 0, "local"),
        ("micros_vm_scratch_preemption_peer_run", 0, "local"),
    ),
    "tests/qemu/vm_scratch_preemption_vm.S": (
        ("micros_vm_scratch_preemption_vm_hook", 0, "local"),
        ("micros_vm_scratch_preemption_stale_load", 0, "local"),
    ),
    "tests/qemu/vm_self_fault.S": (("micros_vm_trigger_self_fault", 0, "local"),),
}
PAYLOADS = {
    "arch/riscv64/address_space_handoff_test.S": ("micros_address_space_handoff_payload", 0),
    "arch/riscv64/grant_syscall_test.S": ("micros_grant_syscall_payload", 0),
    "arch/riscv64/ipc_ecall_test.S": ("micros_ipc_ecall_test_payload", 0),
    "arch/riscv64/ipc_syscall_test.S": ("micros_ipc_syscall_test_payload", 0),
    "arch/riscv64/ipc_syscall_panic_test.S": ("micros_ipc_syscall_panic_payload", 0),
    "arch/riscv64/scheduler_test.S": ("micros_scheduler_payload", 0),
    "arch/riscv64/user_execution_test.S": ("micros_user_execution_payload", 16),
}
CALLS = {
    "_start": None,
    "micros_trap_entry": ("micros_trap_dispatch", "micros_trap_nested_panic"),
    "micros_trap_panic_entry": ("micros_panic_report",),
    "micros_scheduler_idle_wait": ("micros_scheduler_idle_select", "micros_scheduler_idle_accounting_panic"),
    "micros_address_space_handoff_test_enter": ("micros_address_space_handoff_test_enter_production",),
    "micros_user_execution_test_enter": ("micros_user_execution_test_enter_production",),
    "micros_scheduler_test_enter": ("micros_scheduler_test_start_production",),
    "micros_grant_syscall_test_supervisor_resume": ("micros_grant_syscall_test_finish",),
    "micros_ipc_ecall_test_supervisor_resume": ("micros_ipc_ecall_core_test_finish",),
    "micros_ipc_syscall_test_supervisor_resume": ("micros_ipc_syscall_test_finish",),
    "micros_user_runtime_test_supervisor_resume": ("micros_user_runtime_test_finish",),
    "micros_user_runtime_test_raw_send_probe": ("micros_runtime_raw_syscall",),
}
HEADERS = (
    "arch/riscv64/trap_context.h", "arch/riscv64/panic_context.h",
    "arch/riscv64/scheduler_context.h",
)


@dataclass(frozen=True)
class Epoch:
    function: FunctionKey
    offset: int
    owner: str
    target: str
    initial_bytes: int = 0


@dataclass(frozen=True)
class Payload:
    function: FunctionKey
    section_index: int
    start: int
    size: int
    frame: int
    ecalls: tuple
    owner: str = "user"


@dataclass(frozen=True)
class Fault:
    function: FunctionKey
    offset: int
    target: str
    continuation: str


@dataclass(frozen=True)
class AssemblyEvidence:
    frames: tuple
    calls: tuple
    epochs: tuple
    payloads: tuple
    faults: tuple
    snapshots: tuple = ()


@dataclass(frozen=True)
class LinkedAssembly:
    frames: tuple
    definitions: tuple
    edges: tuple
    epochs: tuple
    payloads: tuple
    faults: tuple
    units: tuple
    continuations: tuple = ()
    snapshots: tuple = ()


def allocated_signature(obj):
    result = []
    for section in obj.sections:
        if not section.flags & 2:
            continue
        references = []
        for point, kind, target, extra in relocations(obj, section.index):
            target_section = (
                obj.sections[target.section_index].name
                if 0 < target.section_index < len(obj.sections) else target.section_index
            )
            name = target.name if target.binding or target.section_index == 0 else ""
            references.append((point, kind, name, target_section, target.value, extra))
        result.append((
            section.name, section.section_type, section.flags, section.size, section.alignment,
            b"" if section.section_type == 8 else section_bytes(obj, section), tuple(references),
        ))
    return tuple(result)


@lru_cache(maxsize=128)
def reference_object(root, unit, flags, compiler, compiler_sha):
    with tempfile.TemporaryDirectory(prefix="micros-stack-reference-asm-") as temporary:
        directory = Path(temporary)
        for source in (unit, *HEADERS):
            path = directory / source
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(reference_source(root, source))
        output = directory / "reference.obj"
        argv = [
            compiler, "--target=riscv64-unknown-elf", "-march=rv64imac_zicsr_zifencei",
            "-mabi=lp64", "-mcmodel=medany", "-I" + str(directory),
            *(f"-D{flag}=1" for flag in sorted(flags)),
            "-c", str(directory / unit), "-o", str(output),
        ]
        result = subprocess.run(argv, capture_output=True, text=True, timeout=60)
        if result.returncode or _file_identity(Path(compiler))["sha256"] != compiler_sha:
            raise StackEvidenceError("fixed-source assembly compiler failed or changed")
        return load_object(output)


def function_code(obj, symbol):
    section = obj.sections[symbol.section_index]
    return tuple(instructions(
        section_bytes(obj, section)[symbol.value:symbol.value + symbol.size], symbol.value,
    ))


def assembly_object_evidence(root, obj, unit, variant, image, flags=frozenset()):
    if unit not in CONTRACTS or type(flags) is not frozenset:
        raise StackEvidenceError("unbound assembly source or compile-definition inventory")
    for source in (unit, *HEADERS):
        if read_regular(Path(root) / source)[0].decode("utf-8") != reference_source(str(root), source):
            raise StackEvidenceError("assembly source/include differs from its fixed stage")
    compiler = str(target_compiler(dict(os.environ)).resolve(strict=True))
    reference = reference_object(
        str(root), unit, flags, compiler, _file_identity(Path(compiler))["sha256"],
    )
    if allocated_signature(obj) != allocated_signature(reference):
        raise StackEvidenceError("assembly object differs from independently compiled fixed source")
    symbols = function_symbols(obj)
    if set(symbols) != {name for name, _, _ in CONTRACTS[unit]}:
        raise StackEvidenceError("assembly function/range declaration is incomplete")
    frames, calls, epochs, payloads, faults, snapshots = [], [], [], [], [], []
    if unit == "arch/riscv64/panic.S":
        panic_frame_evidence(root, obj, variant, image)
    for name, size, role in CONTRACTS[unit]:
        key = FunctionKey(variant, image, unit, name)
        symbol, code = symbols[name], function_code(obj, symbols[name])
        jumps = {point: () for point, width, word in code if (
            width == 4 and word & 0x7f == 0x6f and word >> 7 & 31 == 0
        )}
        found = call_instructions(obj, symbol, key, jumps)
        if role == "nx-fault":
            indirect, = (call for call in found if call.target_symbol is None)
            target, = (item for item in obj.symbols if item.name == "micros_mmu_writable_probe")
            section = obj.sections[target.section_index]
            if section.flags != 3 or section_bytes(obj, section) != b"\x67\x80\x00\x00":
                raise StackEvidenceError("NX test transfer does not name its exact nonexecutable probe")
            faults.append(Fault(key, indirect.offset, target.name, "micros_mmu_execute_resume"))
            found = tuple(call for call in found if call != indirect)
        expected = CALLS.get(name, ())
        if name == "_start":
            expected = ({
                "arch/riscv64/boot.S": "kernel_main",
                "lib/runtime/start.S": "micros_service_main",
                "lib/runtime/application_start.S": "micros_application_start",
            }[unit],)
        if tuple(call.target_symbol for call in found) != expected:
            raise StackEvidenceError("assembly machine/source call inventory differs")
        frames.append(Frame(key, size))
        calls.extend(found)
        if role in ("local", "nx-fault"):
            require_c_frame_movement(obj, symbol, size)
        if role == "trap":
            nested, = (call for call in found if call.target_symbol == "micros_trap_nested_panic")
            epochs.append(Epoch(key, nested.offset, "emergency", nested.target_symbol))
        elif role == "idle":
            point, width, word = code[-1]
            if branch_offset(width, word) is None:
                raise StackEvidenceError("idle pivot lacks its exact direct epoch transfer")
            epochs.append(Epoch(key, point - symbol.value, "primary-idle", "micros_scheduler_idle_wait"))
        elif role == "user":
            point, width, word = code[-1]
            if (width, word) != (4, 0x10200073):
                raise StackEvidenceError("user pivot lacks its exact context sret")
            epochs.append(Epoch(key, point - symbol.value, "user", "prepared-user-context"))
        elif role == "resume":
            writes = tuple((point, width, word) for point, width, word in code if _instruction_destination(word, width) == 2)
            if len(writes) != 1 or memory_action(*writes[0][1:])[0:3] != (False, 8, 2):
                raise StackEvidenceError("supervisor continuation lacks its single saved-SP restoration")
            epochs.append(Epoch(key, writes[0][0] - symbol.value, "saved-supervisor", name))
        elif role == "test-pivot":
            from tools.stack_bound_continuations import snapshot_object_evidence

            snapshot = snapshot_object_evidence(obj, symbol, key)
            snapshots.append(snapshot)
            epochs.extend((
                Epoch(key, snapshot.entry_offset, "test-snapshot", "micros_trap_test_entry_stack_top"),
                Epoch(key, snapshot.resume_offset, "test-snapshot", "micros_trap_test_return_sp", 256),
            ))
    if unit in PAYLOADS:
        prefix, size = PAYLOADS[unit]
        start, = (symbol for symbol in obj.symbols if symbol.name == prefix + "_start")
        end, = (symbol for symbol in obj.symbols if symbol.name == prefix + "_end")
        section = obj.sections[start.section_index]
        if (
            start.symbol_type != 0 or end.symbol_type != 0 or start.section_index != end.section_index
            or start.value != 0 or end.value != section.size or section.flags != 6
        ):
            raise StackEvidenceError("copied U-mode payload lacks its complete untyped range")
        key = FunctionKey(variant, image, unit, start.name)
        function = replace(start, size=end.value - start.value)
        code = function_code(obj, function)
        poisoned = "MICROS_BUILD_SCHEDULER_INVALID_OUTGOING_TEST" in flags and unit.endswith("scheduler_test.S")
        if not poisoned:
            require_c_frame_movement(obj, function, size)
        elif any(memory_action(width, word) for _, width, word in code if _instruction_destination(word, width) == 2):
            raise StackEvidenceError("invalid-context fixture acquired an unbound stack memory operation")
        payloads.append(Payload(
            key, start.section_index, start.value, function.size, size,
            tuple(point - start.value for point, width, word in code if (width, word) == (4, 0x73)),
        ))
        frames.append(Frame(key, size))
    return AssemblyEvidence(tuple(frames), tuple(calls), tuple(epochs), tuple(payloads), tuple(faults), tuple(snapshots))


def collect_linked_assembly(root, directory, image_name, variant, catalog):
    root, directory = Path(root), Path(directory)
    image = load_elf(directory / image_name)
    text = image_map_text(directory, image_name)
    text_objects = {name for name, _ in text_owners(text)}
    commands = json.loads(read_regular(directory / "compile_commands.json")[0])
    flags = configured_callback_flags(directory)
    cache = read_regular(directory / "CMakeCache.txt")[0].decode("utf-8").splitlines()
    modes = [line.partition("=")[2] for line in cache if line.startswith("MICROS_STACK_BUILD_MODE:")]
    if len(modes) > 1:
        raise StackEvidenceError("assembly scope has an ambiguous stack build mode")
    observed = modes == ["observed"]
    frames, definitions, calls, epochs, payloads, faults, units, extra_edges = [], [], [], [], [], [], [], []
    snapshots = []
    compiled, declared = set(), set()
    for command in commands:
        output = Path(command["output"])
        if not output.is_absolute():
            output = directory / output
        relative = str(output.relative_to(directory))
        if relative in compiled:
            raise StackEvidenceError("assembly link has duplicate compiled object ownership")
        compiled.add(relative)
        if not command["file"].endswith(".S"):
            continue
        unit = str(Path(command["file"]).relative_to(root))
        if unit not in CONTRACTS and unit != "tests/stack/observer.S":
            raise StackEvidenceError("compiled assembly source has no finite declaration")
        if unit == "tests/stack/observer.S" and not observed:
            raise StackEvidenceError("observer assembly is forbidden in ordinary/metadata artifacts")
        declared.add(unit)
        if relative not in text_objects:
            continue
        if command["directory"] != str(directory):
            raise StackEvidenceError("assembly compile directory differs from its exact image")
        obj = load_object(output)
        sections = verify_linked_object(obj, relative, image, text)
        if unit == "tests/stack/observer.S":
            from tools.stack_bound_observer import observer_frame_evidence

            observer = observer_frame_evidence(root, obj, variant, image_name, image_name == "kernel/micros.elf")
            proof = AssemblyEvidence(observer.frames, (), (), (), ())
        else:
            proof = assembly_object_evidence(root, obj, unit, variant, image_name, flags)
        symbols = function_symbols(obj)
        locations = []
        for frame in proof.frames:
            symbol = symbols.get(frame.function.symbol)
            if symbol is None or symbol.section_index not in sections:
                continue
            address = sections[symbol.section_index]
            start = linked_offset(obj, symbol.section_index, symbol.value, address)
            end = linked_offset(obj, symbol.section_index, symbol.value + symbol.size, address)
            locations.append(FunctionLocation(
                frame.function, relative, obj.sections[symbol.section_index].name, start, end - start,
            ))
        locations = tuple(locations)
        addresses = join_function_locations(locations, text, image.symbols)
        active = set(addresses)
        for frame in proof.frames:
            key = frame.function
            if key not in active:
                continue
            symbol = symbols[key.symbol]
            frames.append(frame)
            location, = (row for row in locations if row.function == key)
            definitions.append(DefinedFunction(key, addresses[key], location.size, symbol.binding != 0))
        for payload in proof.payloads:
            if payload.section_index not in sections:
                continue
            base = sections[payload.section_index]
            address = base + linked_offset(obj, payload.section_index, payload.start, base)
            size = linked_offset(
                obj, payload.section_index, payload.start + payload.size, base,
            ) - linked_offset(obj, payload.section_index, payload.start, base)
            actual = tuple(symbol for symbol in image.symbols if (
                symbol.name == payload.function.symbol and symbol.value == address
                and symbol.symbol_type == 0 and symbol.binding == 1
            ))
            if len(actual) != 1:
                raise StackEvidenceError("linked copied payload has no exact untyped symbol/range owner")
            end_name = PAYLOADS[unit][0] + "_end"
            endings = tuple(symbol for symbol in image.symbols if (
                symbol.name == end_name and symbol.value == address + size
                and symbol.symbol_type == 0 and symbol.binding == 1
            ))
            if len(endings) != 1:
                raise StackEvidenceError("linked copied payload lost its exact end boundary")
            active.add(payload.function)
            frame, = (row for row in proof.frames if row.function == payload.function)
            frames.append(frame)
            definitions.append(DefinedFunction(payload.function, address, size, False))
            payloads.append(payload)
        def offset_for(key, offset):
            symbol = symbols[key.symbol]
            base = sections[symbol.section_index]
            return linked_offset(obj, symbol.section_index, symbol.value + offset, base) - linked_offset(
                obj, symbol.section_index, symbol.value, base,
            )
        calls.extend(
            replace(call, offset=offset_for(call.caller, call.offset))
            for call in proof.calls if call.caller in active
        )
        epochs.extend(
            replace(epoch, offset=offset_for(epoch.function, epoch.offset))
            for epoch in proof.epochs if epoch.function in active
        )
        faults.extend(fault for fault in proof.faults if fault.function in active)
        snapshots.extend(snapshot for snapshot in proof.snapshots if snapshot.function in active)
        units.append(unit)
        if unit == "arch/riscv64/panic.S":
            _, panic_edges = panic_frame_evidence(root, obj, variant, image_name)
            extra_edges.extend(edge for edge in panic_edges if edge.caller.symbol == "micros_panic_entry")
    if not text_objects <= compiled:
        raise StackEvidenceError("linked text has an unowned C/assembly source object")
    all_definitions = catalog.definitions + tuple(definitions)
    resolved, pending = resolve_linked_calls(
        image, catalog.pending + tuple(calls), all_definitions,
    )
    if pending:
        first = pending[0]
        raise StackEvidenceError(
            f"assembly closure left an unbound call: {first.caller.symbol}+{first.offset:#x} -> {first.target_symbol}"
        )
    edges = list(catalog.calls + resolved + tuple(extra_edges))
    for epoch in epochs:
        if epoch.owner == "emergency":
            matches = tuple(index for index, edge in enumerate(edges) if (
                edge.caller == epoch.function and edge.offset == epoch.offset
                and len(edge.targets) == 1 and edge.targets[0].symbol == epoch.target
            ))
            if len(matches) != 1:
                raise StackEvidenceError("emergency epoch lacks its exact linked call")
            index, = matches
            edges[index] = replace(edges[index], kind=EdgeKind.RESET)
        elif epoch.owner == "primary-idle":
            target = FunctionKey(variant, image_name, "arch/riscv64/scheduler.S", epoch.target)
            if target not in {row.function for row in definitions}:
                raise StackEvidenceError("idle epoch lacks its exact linked target root")
            edges.append(CallSite(epoch.function, epoch.offset, (target,), EdgeKind.RESET))
    graph = LinkedAssembly(
        catalog.frames + tuple(frames), all_definitions, tuple(edges),
        tuple(epochs), tuple(payloads), tuple(faults), tuple(sorted(set(units))),
        snapshots=tuple(snapshots),
    )
    from tools.stack_bound_continuations import bind_saved_supervisor_continuations, bind_trap_snapshots

    continuation_edges, records = bind_saved_supervisor_continuations(root, directory, catalog, graph)
    return replace(
        graph, edges=graph.edges + continuation_edges, continuations=records,
        snapshots=bind_trap_snapshots(root, directory, catalog, graph),
    )

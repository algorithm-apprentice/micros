"""Independent kernel/service VM work from the existing ELF-spawn guest."""

import os
import re
import shlex
from pathlib import Path

from tests.host.c_source_records import function_records, tokens
from tests.host.evidence_records import record_fields
from tests.host.runtime_memory_evidence import json_pairs
from tests.host.source_snapshot import source_bytes
from tests.host.work_count_snapshots import compose_snapshots
from tests.host.work_count_types import Capture, Kind, Owner, Snapshot, Status
from tools.stack_artifact_files import read_regular


KERNEL_PRODUCER = 0x564d3031544b0001
SERVICE_PRODUCER = 0x564d303154530001
PRODUCERS = (KERNEL_PRODUCER, SERVICE_PRODUCER)
MASKS = {
    KERNEL_PRODUCER: (1 << 17) - 1,
    SERVICE_PRODUCER: sum(1 << owner for owner in (
        Owner.RUNTIME, Owner.LIB_VM, Owner.LIB_SPAWN, Owner.VM_SERVICE,
    )),
}
FIELDS = ("version", "reserved0", "producer", "capture", "owner_mask", "reserved1", "attempts", "complete")
DIMENSIONS = (
    "comparison", "record", "index", "link", "bitmap", "pte", "queue",
    "validator", "copy", "copied", "clear", "cleared", "range", "hash",
)
PASS_LINE = (
    "MICROS_ELF_SPAWN_TEST_PASS services=production init=dynamic rollback=post-freeze "
    "arguments=validated descriptors=inherited io=two-hop exit=normal wait=one-shot resources=baseline"
)
SITES = (
    ("servers/vm/vm_validation.c", "vm_core_validate_state", "service-state", "VM_SERVICE", "COMPLETE_VALIDATOR_CALL"),
    ("kernel/vm/vm_handoff_core.c", "micros_vm_handoff_state_validate", "kernel-state", "KERNEL_VM", "COMPLETE_VALIDATOR_CALL"),
    ("kernel/vm/vm_handoff_runtime.c", "micros_vm_handoff_runtime_validate", "kernel-runtime", "KERNEL_VM", "COMPLETE_VALIDATOR_CALL"),
    ("kernel/boot/bootstrap_runtime_validation.c", "micros_bootstrap_runtime_validate", "bootstrap-runtime", "KERNEL_BOOT", "COMPLETE_VALIDATOR_CALL"),
    ("kernel/pm/pm_control_runtime.c", "micros_pm_control_runtime_validate", "pm-runtime", "KERNEL_PM", "COMPLETE_VALIDATOR_CALL"),
    ("servers/vm/vm_validation.c", "vm_core_validate_selected_state", "service-selected", "VM_SERVICE", "INDEX_PROBE"),
    ("servers/vm/vm_elf.c", "micros_vm_elf_plan", "elf-plan", "VM_SERVICE", "RANGE_VISIT"),
    ("lib/spawn/spawn_request.c", "micros_spawn_request_decode", "request-decode", "LIB_SPAWN", "INDEX_PROBE"),
    ("kernel/vm/vm_handoff_core.c", "micros_vm_handoff_validate_selected", "kernel-selected", "KERNEL_VM", "INDEX_PROBE"),
    ("kernel/vm/vm_handoff_runtime.c", "micros_vm_handoff_runtime_validate_command", "command-selected", "KERNEL_VM", "INDEX_PROBE"),
    ("kernel/pm/pm_control_runtime.c", "micros_pm_control_runtime_validate_vm_command", "pm-selected", "KERNEL_PM", "INDEX_PROBE"),
    ("kernel/memory/user_address_space_validation.c", "micros_user_address_space_validate_vm_table_plan_locked", "memory-root", "KERNEL_MEMORY", "COMPLETE_VALIDATOR_CALL"),
    ("kernel/memory/user_address_space_validation.c", "micros_user_address_space_validate_all_process_roots_locked", "root-enumeration", "KERNEL_MEMORY", "RECORD_VISIT"),
)
OPTIONALLY_UNLINKED = {
    ("kernel/memory/user_address_space_validation.c", "micros_user_address_space_validate_all_process_roots_locked"),
}
KEY_SOURCES = ("lib/vm/vm_keys.c", "lib/vm/vm_load_token.c")
UNSCOPED_SOURCES = (
    "arch/riscv64/uart.c", "kernel/diagnostic/diagnostic_history_core.c", "lib/runtime/memory.c",
)


def parse_target_output(output, exit_code):
    if type(output) is not str or not output.endswith("\n") or type(exit_code) is not int or exit_code:
        raise ValueError("VM01 target did not complete its existing guest")
    captures, ended = [], False
    for line in output.splitlines():
        if not line.startswith("# MICROS_VM01_"):
            continue
        match = re.fullmatch(r"# (MICROS_VM01_CAPTURE|MICROS_VM01_COMMAND|MICROS_VM01_COUNTS|MICROS_VM01_CAPTURE_END) (.*)", line)
        if match is None or ended:
            raise ValueError("VM01 target has unknown, duplicate or late evidence")
        pairs = [re.fullmatch(r"([a-z0-9_]+)=(0x[0-9a-f]{16})", item) for item in match[2].split(" ")]
        if any(pair is None for pair in pairs):
            raise ValueError("VM01 target has malformed work fields")
        row = json_pairs((pair[1], int(pair[2], 16)) for pair in pairs)
        if match[1] == "MICROS_VM01_CAPTURE":
            record_fields(row, FIELDS)
            if (
                len(captures) >= len(PRODUCERS)
                or row["producer"] != PRODUCERS[len(captures)] or row["capture"] != 1
                or row["complete"] != 1
                or captures and (len(captures[-1]["commands"]) != 7 or len(captures[-1]["counts"]) != len(Owner))
            ):
                raise ValueError("VM01 target borrowed, reordered or omitted a producer")
            captures.append({**row, "commands": [], "counts": []})
        elif match[1] == "MICROS_VM01_COMMAND":
            record_fields(row, ("producer", "index", "count"))
            if (
                not captures or row["producer"] != captures[-1]["producer"]
                or row["index"] != len(captures[-1]["commands"]) or row["index"] >= 7
                or captures[-1]["counts"]
            ):
                raise ValueError("VM01 target omitted, duplicated or borrowed a command")
            captures[-1]["commands"].append(row["count"])
        elif match[1] == "MICROS_VM01_COUNTS":
            record_fields(row, ("producer", "owner", *DIMENSIONS))
            if (
                not captures or row["producer"] != captures[-1]["producer"]
                or len(captures[-1]["commands"]) != 7
                or row["owner"] != len(captures[-1]["counts"]) or row["owner"] >= len(Owner)
            ):
                raise ValueError("VM01 target omitted, duplicated or borrowed an owner")
            captures[-1]["counts"].append(tuple(row[name] for name in DIMENSIONS))
        else:
            record_fields(row, ("complete", "producers"))
            if row != {"complete": 1, "producers": 2}:
                raise ValueError("VM01 target has an incomplete terminal record")
            ended = True
    if (
        not ended or len(captures) != 2 or len(captures[-1]["commands"]) != 7
        or len(captures[-1]["counts"]) != len(Owner)
        or len(re.findall(r"^# " + re.escape(PASS_LINE) + r"\r?$", output, re.M)) != 1
        or re.search(r"^# MICROS_(?:PANIC|ELF_SPAWN_TEST_FAILURE|TEST_FAILURE)\b", output, re.M)
    ):
        raise ValueError("VM01 target lacks complete original semantics or work evidence")
    kernel, service = (row["commands"] for row in captures)
    if (
        kernel[0] != 3 or kernel[3:] != [3, 1, 2, 1]
        or not 12 <= kernel[1] <= 48 or kernel[1] != kernel[2]
        or service != [3, 1, 2, 1, 0, 0, 0]
        or any(row["attempts"] != sum(row["commands"]) for row in captures)
    ):
        raise ValueError("VM01 target lacks the original load/rollback/commit/exit command coverage")
    snapshots = tuple(Capture(Status.OK, Snapshot(
        row["producer"], row["capture"], row["owner_mask"], tuple(row["counts"]),
        row["version"], row["reserved0"], row["reserved1"],
    )) for row in captures)
    totals = compose_snapshots(snapshots, capture_label=1, expected_producers=MASKS)
    return tuple(captures), totals


def target_failures(measured):
    captures, totals = measured
    failures = [
        (owner.name, "complete-audits", totals.per_owner[owner][Kind.COMPLETE_VALIDATOR_CALL], 0)
        for owner in (Owner.KERNEL_BOOT, Owner.KERNEL_VM, Owner.KERNEL_PM, Owner.VM_SERVICE)
        if totals.per_owner[owner][Kind.COMPLETE_VALIDATOR_CALL]
    ]
    for owner, lower, upper in (
        (Owner.KERNEL_PM, captures[0]["attempts"], captures[0]["attempts"]),
        (Owner.KERNEL_VM, 4 * captures[0]["attempts"], (1 << 64) - 1),
        (Owner.VM_SERVICE, captures[1]["attempts"], (1 << 64) - 1),
    ):
        count = totals.per_owner[owner][Kind.INDEX_PROBE]
        if not lower <= count <= upper:
            failures.append((owner.name, "selected-admissions", count, (lower, upper)))
    commands = captures[0]["commands"]
    roots = totals.per_owner[Owner.KERNEL_MEMORY][Kind.COMPLETE_VALIDATOR_CALL]
    certificates = commands[3] + commands[4] + commands[6]
    if roots != certificates:
        failures.append(("KERNEL_MEMORY", "selected-root-certificates", roots, certificates))
    enumerations = totals.per_owner[Owner.KERNEL_MEMORY][Kind.RECORD_VISIT]
    if enumerations:
        failures.append(("KERNEL_MEMORY", "root-enumeration", enumerations, 0))
    loads = captures[1]["commands"][0]
    for owner, kind, label, expected in (
        (Owner.VM_SERVICE, Kind.RANGE_VISIT, "elf-plans", loads),
        (Owner.LIB_SPAWN, Kind.INDEX_PROBE, "request-decodes", 3 * loads),
    ):
        actual = captures[1]["counts"][owner][kind]
        if actual != expected:
            failures.append((owner.name, label, actual, expected))
    return tuple(failures)


def checked_terminal_prefix(output, exit_code):
    lines = output.splitlines(keepends=True)
    failures = target_failures(parse_target_output("".join("# " + line for line in lines), exit_code))
    if failures:
        raise ValueError("VM01 terminal semantic/work witnesses failed: " + repr(failures))
    start = next(index for index, line in enumerate(lines) if line.startswith("MICROS_VM01_"))
    if any(line.strip() and not line.startswith("MICROS_VM01_") for line in lines[start:]):
        raise ValueError("VM01 command records split the terminal evidence boundary")
    return "".join(lines[:start])


def validate_target_observations(root):
    root = Path(root)
    for path, name, label, owner, kind in SITES:
        body = function_records(source_bytes(root, root / path).decode())[name].body
        expected = tokens(
            f'MICROS_VM01_WORK_HIT("vm01-{label}", MICROS_WORK_OWNER_{owner}, '
            f'MICROS_WORK_KIND_{kind}, 1);'
        )
        values = tokens(body)
        if values.count("MICROS_VM01_WORK_HIT") != 1:
            raise ValueError("VM01 lost or duplicated its complete-validator observation")
        offset = values.index("MICROS_VM01_WORK_HIT")
        if values[offset:offset + len(expected)] != expected:
            raise ValueError("VM01 changed its complete-validator primitive")
        prefix = re.sub(
            r'#ifdef MICROS_TEST_BOOTSTRAP_COMMAND_COUNTS\s+MICROS_TEST_WORK_HIT\([^;]+;\s*#endif',
            "", body[:body.index("MICROS_VM01_WORK_HIT")],
        )
        if tokens(prefix):
            raise ValueError("VM01 validator observation moved after executed work")
    dispatch = tokens(function_records(source_bytes(root, root / "kernel/trap/syscall.c").decode())["syscall_dispatch_operation"].body)
    if dispatch.count("MICROS_VM01_DISPATCH") != 1 or "micros_vm_handoff_handle_captured_user_ecall" in dispatch:
        raise ValueError("VM01 capture no longer encloses the actual operation-12 handler")
    observer = function_records(source_bytes(root, root / "tests/qemu/vm_command_work.c").decode())
    if tokens(observer["micros_vm01_work_active"].body) != tokens("return observer.kernel.active == 1;"):
        raise ValueError("VM01 kernel observation no longer follows its actual capture scope")
    body = tokens(observer["micros_vm01_dispatch"].body)
    active = tokens("capture->active = true;")
    inactive = tokens("capture->active = false;")
    starts = [index for index in range(len(body)) if body[index:index + len(active)] == active]
    stops = [index for index in range(len(body)) if body[index:index + len(inactive)] == inactive]
    calls = [index for index, value in enumerate(body) if value == "micros_vm_handoff_handle_captured_user_ecall"]
    if len(starts) != 1 or len(stops) != 1 or len(calls) != 2 or not starts[0] < calls[1] < stops[0]:
        raise ValueError("VM01 actual dynamic command escaped its measured scope")
    service = function_records(source_bytes(root, root / "tests/qemu/vm_server_control.c").decode())
    observer = function_records(source_bytes(root, root / "tests/qemu/selected_vm_work.c").decode())
    if tokens(observer["micros_vm01_work_active"].body) != tokens("return vm_server_context_get()->work.active == 1;"):
        raise ValueError("VM01 service observation lost its real context")
    body = tokens(service["vm_server_serve_control"].body)
    if not (
        body.count("micros_vm01_service_begin") == body.count("micros_vm01_service_finish") == body.count("micros_vm_service_handle_message") == 1
        and body.index("micros_vm01_service_begin") < body.index("micros_vm_service_handle_message")
        < body.index("micros_vm01_service_finish") < body.index("micros_runtime_reply")
    ):
        raise ValueError("VM01 service command escaped its measured scope")
    report = tokens(function_records(source_bytes(root, root / "kernel/tests/spawn/elf_spawn_test_report.c").decode())["elf_spawn_test_process_report"].body)
    if not (
        report.count("MICROS_VM01_FINISH") == report.count("MICROS_VM01_REPORT") == 1
        and report.index("elf_spawn_final") < report.index("MICROS_VM01_FINISH")
        < report.index("MICROS_TEST_STACK_REPORT") < report.index("MICROS_VM01_REPORT")
        < report.index("sbi_system_reset")
    ):
        raise ValueError("VM01 readback/report moved across the semantic or stack boundary")


def _target_objects(root, directory, commands, owners, expected, compiler):
    from tools.stack_bound_manifest import owned_artifact

    selected = {}
    for row in commands:
        source, output = Path(row["file"]), Path(row["output"])
        source = source if source.is_absolute() else directory / source
        output = output if output.is_absolute() else directory / output
        if source not in {root / name for name in expected}:
            continue
        if not output.is_relative_to(directory) or str(output.relative_to(directory)) not in owners:
            continue
        name = str(source.relative_to(root))
        arguments = tuple(shlex.split(row["command"]))
        if (name in selected or Path(row["directory"]).resolve() != directory
            or not arguments or arguments[0] != compiler
            or arguments.count("-c") != 1 or arguments.count("-o") != 1
            or max(arguments.index("-c"), arguments.index("-o")) + 1 >= len(arguments)
            or any(value.startswith("@") for value in arguments)):
            raise ValueError("VM01 object has duplicate, foreign or incomplete compiler ownership")
        operands = []
        for option in ("-c", "-o"):
            operand = Path(arguments[arguments.index(option) + 1])
            operands.append(operand if operand.is_absolute() else directory / operand)
        if tuple(operands) != (source, output):
            raise ValueError("VM01 compiler operands disagree with their linked source owner")
        selected[name] = (owned_artifact(directory, str(output.relative_to(directory))), arguments)
    if selected.keys() != set(expected):
        raise ValueError("VM01 image omitted a required producer or observation object")
    return selected


def require_observation_mode(source, arguments, symbols):
    if source in UNSCOPED_SOURCES:
        if "-UMICROS_TEST_WORK_COUNTS" not in arguments or any(
            name.startswith("micros_test_work_") for name in symbols
        ):
            raise ValueError("VM01 capture admitted an unrelated observer: " + source)
    elif not {"-DMICROS_TEST_WORK_COUNTS=1", "-DMICROS_TEST_VM_COMMAND_COUNTS=1"} <= set(arguments):
        raise ValueError("VM01 producer omitted its explicit scoped observation mode")


def validate_target_products(root):
    from tests.host.runtime_memory_evidence import runtime_compile_commands
    from tools.stack_bound_collect import image_map_text
    from tools.stack_bound_flow import function_flow, reachable_instructions
    from tools.stack_bound_link import text_owners
    from tools.stack_bound_manifest import owned_artifact
    from tools.stack_bound_objects import function_symbols, load_object
    from tools.stack_bound_relocations import verify_linked_object
    from tools.user_elf_format import SECTION_ALLOC, SECTION_EXECUTE, SYMBOL_UNDEFINED
    from tools.user_elf_parse import load_elf
    from tools.validation_context import compiler_version, target_compiler, validate_environment
    from tools.validation_inputs import _file_identity, _run

    root = Path(root).resolve()
    directory = owned_artifact(root, "build/riscv64-elf-spawn-test")
    validate_environment(dict(os.environ), target=True)
    compiler = target_compiler(dict(os.environ))
    compiler_identity = _file_identity(compiler.resolve())
    version = _run((str(compiler), "--version")).decode()
    compiler_version(version, target=True)
    commands = runtime_compile_commands(directory)
    subjects = {
        "kernel/micros.elf": tuple(sorted({path for path, *_ in SITES if path.startswith("kernel/")})) + (
            "tests/qemu/vm_command_work.c", "kernel/tests/spawn/elf_spawn_test_services.c",
            "tests/work/work_counts.c", *UNSCOPED_SOURCES, *KEY_SOURCES,
        ),
        "bootstrap/micros-vm-server.elf": (
            "servers/vm/vm_validation.c", "servers/vm/vm_elf.c", "lib/spawn/spawn_request.c",
            "tests/qemu/vm_server_control.c",
            "tests/qemu/selected_vm_work.c",
            "tests/qemu/vm_server_fixture.c", "tests/work/work_counts.c", "lib/runtime/memory.c",
            *KEY_SOURCES,
        ),
    }
    inputs = {
        name: read_regular(root / name)[1]
        for name in {
            *(name for paths in subjects.values() for name in paths), "CMakeLists.txt",
            "tests/qemu/vm_validation_work.h", "tests/qemu/vm_command_work.h",
            "tests/qemu/vm_server_internal.h", "tests/work/work_counts.h", "include/micros/work_observation.h",
            "include/micros/spawn.h", "lib/spawn/spawn_request.h",
            "servers/vm/vm_image.h", "servers/vm/vm_image_internal.h",
        }
    }
    artifacts, maps, results = {}, {}, {}
    for image_name, paths in subjects.items():
        image_path = owned_artifact(directory, image_name)
        raw, artifacts[str(image_path)] = read_regular(image_path)
        image = load_elf(image_path)
        if image.data != raw:
            raise ValueError("VM01 image changed before producer proof")
        maps[image_name] = image_map_text(directory, image_name)
        owners = {name for name, _ in text_owners(maps[image_name])}
        selected = _target_objects(root, directory, commands, owners, paths, str(compiler))
        defined = [symbol.name for symbol in image.symbols if symbol.section_index != SYMBOL_UNDEFINED]
        for name in ("micros_test_work_begin", "micros_test_work_finish", "micros_test_work_record", "micros_vm01_work_active"):
            if defined.count(name) != 1:
                raise ValueError("VM01 image lacks one exact scoped collector")
        objects = {}
        for source, (output, arguments) in selected.items():
            raw, artifacts[str(output)] = read_regular(output)
            obj = load_object(output)
            if obj.data != raw:
                raise ValueError("VM01 object changed before producer proof")
            retained = verify_linked_object(obj, str(output.relative_to(directory)), image, maps[image_name])
            objects[source] = obj
            require_observation_mode(source, arguments, tuple(symbol.name for symbol in obj.symbols))
            if source in UNSCOPED_SOURCES:
                continue
            symbols = function_symbols(obj)
            for path, name, *_ in SITES:
                if path != source:
                    continue
                if name not in symbols or (
                    symbols[name].section_index not in retained and (path, name) not in OPTIONALLY_UNLINKED
                ):
                    raise ValueError("VM01 primitive is absent from its linked owner")
                flow = function_flow(obj, symbols[name])
                reachable = reachable_instructions(flow)
                for callee in ("micros_vm01_work_active", "micros_test_work_record"):
                    calls = [call for call in flow.calls if call.target_symbol == callee]
                    if len(calls) != 1 or calls[0].offset not in reachable:
                        raise ValueError("VM01 primitive lost its actual scoped machine call")
        workspaces = [symbol for symbol in image.symbols
                      if symbol.name == "workspace" and symbol.section_index != SYMBOL_UNDEFINED]
        backing = [symbol for symbol in objects["lib/vm/vm_keys.c"].symbols
                   if symbol.name == "workspace" and symbol.section_index != SYMBOL_UNDEFINED]
        if (len(workspaces) != 1 or len(backing) != 1
            or workspaces[0].size != backing[0].size
            or not 16 * 1024 < backing[0].size <= 16 * 1024 + 256):
            raise ValueError("VM02 image lost its single bounded key workspace")
        def storage(source):
            return sum(section.size for section in objects[source].sections
                       if section.flags & SECTION_ALLOC and not section.flags & SECTION_EXECUTE)

        if image_name.startswith("kernel/"):
            size = storage("tests/qemu/vm_command_work.c") + storage("tests/work/work_counts.c")
            if size > 4 * 4096:
                raise ValueError("VM01 kernel test storage exceeded four pages")
        else:
            contexts = [symbol for symbol in image.symbols
                        if symbol.name == "service_context" and symbol.section_index != SYMBOL_UNDEFINED]
            if len(contexts) != 1:
                raise ValueError("VM01 service capture lost its generated context owner")
            size = (contexts[0].size - 201632 + storage("tests/work/work_counts.c")
                    + storage("tests/qemu/vm_server_control.c") + storage("tests/qemu/selected_vm_work.c"))
            if not 0 < size <= 2 * 4096:
                raise ValueError("VM01 service test storage exceeded the fixed-design two-page addition")
        results[image_name] = size
    if (
        {name: read_regular(root / name)[1] for name in inputs} != inputs
        or {name: read_regular(name)[1] for name in artifacts} != artifacts
        or {name: image_map_text(directory, name) for name in maps} != maps
        or runtime_compile_commands(directory) != commands
        or _file_identity(compiler.resolve()) != compiler_identity
        or _run((str(compiler), "--version")).decode() != version
    ):
        raise ValueError("VM01 source, compiler, recipe or linked producer changed during proof")
    return results

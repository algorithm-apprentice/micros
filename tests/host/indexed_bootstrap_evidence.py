"""Independent bootstrap lookup/transition semantics and bounded native work."""

import json
import hashlib
import os
from pathlib import Path
import re
import shlex
import subprocess

from tests.host.evidence_records import record_fields
from tests.host.object_hart_validation_evidence import observation_symbols
from tests.host.runtime_memory_evidence import json_pairs
from tests.host.selected_address_space_evidence import native_symbols_tool
from tests.host.selected_ipc_validation_evidence import NATIVE_HEADERS as IPC_HEADERS, NATIVE_SOURCES as IPC_SOURCES
from tests.host.work_count_snapshots import compose_snapshots
from tests.host.work_count_types import Capture, Kind, Owner, Snapshot, Status
from tools.stack_artifact_files import read_regular


NATIVE_SOURCES = tuple(path for path in IPC_SOURCES if path not in (
    "kernel/ipc/ipc_buffer.c", "kernel/ipc/ipc_abi.c", "kernel/scheduler/scheduler_completion.c",
)) + (
    "kernel/boot/bootstrap_control_core.c", "kernel/boot/bootstrap_transitions.c",
    "kernel/boot/bootstrap_manifest_common.c",
    "kernel/boot/bootstrap_image.c", "lib/runtime/memory.c",
    "kernel/boot/bootstrap_runtime_state.c", "kernel/boot/bootstrap_diagnostics.c",
    "kernel/tty/tty_handoff_core.c", "kernel/tty/tty_handoff_runtime_state.c",
    "kernel/tty/tty_handoff_runtime.c",
)
NATIVE_SUBSETS = {
    "kernel/boot/bootstrap_runtime_state.c": (
        "bootstrap_runtime_context", "bootstrap_runtime_copy_bytes", "bootstrap_runtime_clear_bytes",
    ),
    "kernel/tty/tty_handoff_runtime.c": (
        "micros_tty_handoff_runtime_state", "micros_tty_handoff_runtime_deadline_expired",
    ),
}
NATIVE_HEADERS = IPC_HEADERS + (
    "kernel/boot/bootstrap_control_internal.h", "kernel/boot/bootstrap_manifest_internal.h",
    "kernel/boot/bootstrap_transitions_internal.h",
    "arch/riscv64/panic_context.h", "arch/riscv64/platform.h",
    "include/micros/frame_allocator.h", "include/micros/frame_ownership.h",
    "include/micros/kernel_object_runtime.h", "include/micros/panic.h",
    "include/micros/spawn.h", "include/micros/vm.h", "include/micros/vm_bootstrap.h",
    "kernel/boot/bootstrap_image.h", "kernel/boot/bootstrap_runtime.h",
    "kernel/boot/bootstrap_runtime_internal.h", "kernel/tty/tty_handoff_core.h",
    "kernel/tty/tty_handoff_runtime.h", "kernel/tty/tty_handoff_runtime_internal.h",
    "kernel/vm/vm_snapshot.h",
    "lib/runtime/memory.h",
)
OBSERVED_HEADERS = ("kernel/boot/bootstrap_control_internal.h",)
TARGETS = ("indexed_bootstrap_ordinary", "indexed_bootstrap_test")
PRODUCER = 0x424f4f5430310001
OWNER_MASK = sum(1 << owner for owner in (
    Owner.KERNEL_OBJECTS, Owner.KERNEL_SCHEDULER, Owner.KERNEL_ENDPOINT, Owner.KERNEL_IPC, Owner.KERNEL_BOOT,
    Owner.KERNEL_TTY, Owner.RUNTIME,
))
CASE_FIELDS = ("version", "case", "name", "result", "value", "preserved", "before_state", "after_state")
WORK_FIELDS = ("version", "reserved0", "producer", "case", "owner_mask", "reserved1", "counts")
TRANSFER_SIZES = (0, 1, 7, 8, 11, 12, 13, 14, 24, 48, 56, 64, 68, 128, 136, 152, 200, 264, 392, 448, 1408, 2416, 4096)
# name, result, value, preserved, owned row visits (-1: retained full audit), copied/cleared bytes, minimum boot audits
CASES = (
    ("binding-first", 0, 2, 1, 0, 0, 0, 0),
    ("binding-last", 0, 3, 1, 0, 0, 0, 0),
    ("binding-missing", 0, 0, 1, 0, 0, 0, 0),
    ("binding-range", 0, 0, 1, 0, 0, 0, 0),
    ("classify-controller", 0, 4, 1, 0, 0, 0, 0),
    ("classify-last", 0, 3, 1, 0, 0, 0, 0),
    ("runtime-release-two", 0, 110, 0, 1, 192, 96, 0),
    ("runtime-release-many", 0, 110, 0, 1, 192, 96, 0),
    ("runtime-ready-many", 0, 2, 0, 0, 192, 96, 0),
    ("runtime-complete-many", 0, 3, 0, 7, 192, 96, 0),
    ("runtime-release-unknown", 1, 0, 1, 0, 192, 96, 0),
    ("runtime-ready-expired", 10, 0, 1, 0, 192, 96, 0),
    ("control-release", 0, 110, 0, -1, 192, 96, 0),
    ("control-ready-plan", 0, 2, 1, -1, 320, 224, 0),
    ("control-ready-commit", 0, 2, 0, 0, 64, 160, 0),
    ("control-ready-role", 8, 0, 1, -1, 192, 224, 0),
    ("control-complete-plan", 0, 1, 1, -1, 384, 288, 0),
    ("control-complete-commit", 0, 3, 0, -1, 64, 224, 0),
    ("deadline-before", 0, 0, 1, 0, 0, 0, 0),
    ("deadline-expired", 0, 1, 0, 0, 0, 0, 0),
    ("deadline-missing-index", 0, 1, 0, 0, 0, 0, 0),
    ("deadline-slot-range", 0, 1, 0, 0, 0, 0, 0),
    ("deadline-row-mismatch", 0, 1, 0, 0, 0, 0, 0),
    ("deadline-wrong-state", 0, 1, 0, 0, 0, 0, 0),
    ("deadline-zero", 0, 1, 0, 0, 0, 0, 0),
    ("deadline-binding-missing", 0, 1, 0, 0, 0, 0, 0),
    ("deadline-tty-first", 0, 1, 0, 0, 0, 0, 0),
    ("deadline-no-start", 0, 0, 1, 0, 0, 0, 0),
    ("deadline-sealed", 0, 0, 1, 0, 0, 0, 0),
    ("deadline-zero-count", 0, 1, 0, 0, 0, 0, 0),
    ("deadline-invalid-id", 0, 1, 0, 0, 0, 0, 0),
    ("single-service-complete", 0, 3, 0, 1, 192, 96, 0),
    ("binding-sparse", 0, 63, 1, 0, 0, 0, 0),
    ("binding-corruption", 0, 1, 1, 0, 0, 0, 0),
    ("control-index-diagnosis", 0, 1, 1, -1, 0, 0, 8),
    ("release-named-dependencies", 0, 110, 0, 14, 192, 96, 0),
    ("runtime-selected-corruption", 0, 1, 1, 0, 0, 0, 0),
    ("ready-counter-precedence", 0, 1, 1, 0, 0, 0, 0),
    ("selected-plan-canaries", 0, 1, 1, 2, 192, 96, 0),
    ("constructor-rejections", 0, 1, 1, -1, 0, 4480, 0),
    ("preparing-permutation", 0, 1, 0, -1, 4096, 2416, 0),
    ("permuted-control-seal", 0, 3, 0, -1, 64, 224, 0),
    ("runtime-unvisited-scope", 0, 110, 0, 1, 192, 96, 0),
    ("control-unvisited-binding", 0, 110, 0, 1, 192, 96, 0),
    ("control-unvisited-runtime", 0, 110, 0, 1, 192, 96, 0),
    ("control-selected-tuple", 11, 0, 1, 0, 192, 96, 0),
    ("control-selection-guards", 0, 1, 1, 0, 0, 0, 0),
    ("control-complete-membership", 0, 1, 1, 15, 0, 1000, 0),
    ("runtime-transfer-sizes", 0, sum(TRANSFER_SIZES), 0, 0, sum(TRANSFER_SIZES), sum(TRANSFER_SIZES), 0),
    ("runtime-unaligned-copy", 0, 130, 0, 0, 130, 0, 0),
    ("runtime-unaligned-clear", 0, 24, 0, 0, 0, 24, 0),
    ("image-catalog-clear", 0, 1, 1, -1, 0, 3136, 0),
)
MOVEMENT_KINDS = (Kind.COPIED_BYTE, Kind.CLEARED_BYTE, Kind.COPY_UNIT, Kind.CLEAR_UNIT)
MOVEMENT_COUNTS = {
    **{name: (160, 64, 20, 16) for name in (
        "runtime-release-two", "runtime-release-many", "runtime-ready-many", "runtime-complete-many",
        "control-release", "single-service-complete", "runtime-unvisited-scope",
        "control-unvisited-binding", "control-unvisited-runtime",
    )},
    "control-ready-plan": (248, 200, 31, 50),
    "control-ready-commit": (48, 136, 6, 34),
    "control-complete-plan": (312, 264, 39, 66),
    "control-complete-commit": (48, 200, 6, 50),
    "permuted-control-seal": (48, 200, 6, 50),
    "runtime-transfer-sizes": (
        sum(TRANSFER_SIZES), sum(TRANSFER_SIZES),
        sum(size // 8 + size % 8 for size in TRANSFER_SIZES),
        sum(size if size < 12 else size // 4 + size % 4 for size in TRANSFER_SIZES),
    ),
    "runtime-unaligned-copy": (130, 0, 81, 0),
    "runtime-unaligned-clear": (0, 24, 0, 18),
    "image-catalog-clear": (0, 3136, 0, 784),
}
COMMAND_CASES = frozenset((
    "control-release", "control-ready-plan", "control-ready-commit", "control-ready-role",
    "control-complete-plan", "control-complete-commit", "permuted-control-seal",
    "control-unvisited-binding", "control-unvisited-runtime", "control-selected-tuple",
    "control-selection-guards",
    "control-complete-membership",
))


def expected_cases():
    return tuple({
        "version": 1, "case": index, "name": spec[0], "result": spec[1],
        "value": spec[2], "preserved": spec[3],
    } for index, spec in enumerate(CASES, 1))


def work_totals(row):
    record_fields(row, WORK_FIELDS)
    if (any(type(row[key]) is not int for key in WORK_FIELDS if key != "counts")
        or type(row["counts"]) is not list or any(type(values) is not list for values in row["counts"])):
        raise ValueError("BOOT01 work fields have an invalid type")
    snapshot = Snapshot(
        row["producer"], row["case"], row["owner_mask"], tuple(tuple(values) for values in row["counts"]),
        row["version"], row["reserved0"], row["reserved1"],
    )
    return compose_snapshots((Capture(Status.OK, snapshot),), capture_label=row["case"],
                             expected_producers={PRODUCER: OWNER_MASK})


def parse_native_output(output, channel):
    if channel not in ("ordinary", "work") or type(output) is not str or not output.endswith("\n"):
        raise ValueError("BOOT01 native transcript is incomplete or has an unknown channel")
    records, work = [], []
    lines = output.splitlines()
    for line in lines[:-1]:
        if line.startswith("MICROS_BOOT01_CASE "):
            if channel == "work" and len(work) != len(records):
                raise ValueError("BOOT01 case omitted its work snapshot")
            row = json.loads(line.removeprefix("MICROS_BOOT01_CASE "), object_pairs_hook=json_pairs)
            record_fields(row, CASE_FIELDS)
            if (any(type(row[key]) is not int or not 0 <= row[key] < 1 << 64 for key in CASE_FIELDS if key != "name")
                or type(row["name"]) is not str or re.fullmatch(r"[a-z-]+", row["name"]) is None
                or row["case"] != len(records) + 1):
                raise ValueError("BOOT01 case has malformed fields or order")
            records.append(row)
        elif line.startswith("MICROS_BOOT01_WORK "):
            row = json.loads(line.removeprefix("MICROS_BOOT01_WORK "), object_pairs_hook=json_pairs)
            work_totals(row)
            if channel != "work" or row["case"] != len(records) or len(work) + 1 != len(records):
                raise ValueError("BOOT01 work belongs to another or duplicate case")
            work.append(row)
        else:
            raise ValueError("BOOT01 transcript contains an unknown record")
    identities = tuple({key: value for key, value in row.items()
                        if key not in ("before_state", "after_state")} for row in records)
    if (identities != expected_cases() or lines[-1:] != ["MICROS_BOOT01_NATIVE_PASS cases=" + str(len(records))]
        or channel == "work" and len(work) != len(records)):
        raise ValueError("BOOT01 evidence omits or changes an independent case")
    return tuple(records), tuple(work)


def work_failures(records, work):
    identities = tuple({key: value for key, value in row.items()
                        if key not in ("before_state", "after_state")} for row in records)
    if identities != expected_cases() or len(work) != len(records):
        raise ValueError("BOOT01 requires its complete independent case set")
    failures = []
    for row, snapshot, spec in zip(records, work, CASES, strict=True):
        if row["name"] != spec[0] or row["case"] != snapshot["case"]:
            raise ValueError("BOOT01 borrowed a work snapshot")
        totals = work_totals(snapshot)
        owned = totals.per_owner[Owner.KERNEL_BOOT]
        runtime = totals.per_owner[Owner.RUNTIME]
        if row["name"] in COMMAND_CASES and totals.per_kind[Kind.COMPLETE_VALIDATOR_CALL] != 0:
            failures.append((row["name"], "complete-command-audit", totals.per_kind[Kind.COMPLETE_VALIDATOR_CALL], 0))
        for kind, bound in (
            (Kind.RECORD_VISIT, spec[4]), (Kind.COPIED_BYTE, spec[5]), (Kind.CLEARED_BYTE, spec[6]),
        ):
            count = owned[kind] + (runtime[kind] if kind in MOVEMENT_KINDS else 0)
            if bound >= 0 and count > bound:
                failures.append((row["name"], kind.name.lower(), count, bound))
        if row["name"] in MOVEMENT_COUNTS:
            for kind, expected in zip(MOVEMENT_KINDS, MOVEMENT_COUNTS[row["name"]], strict=True):
                count = owned[kind] + runtime[kind]
                if count != expected:
                    failures.append((row["name"], "transfer-" + kind.name.lower(), count, expected))
        if owned[Kind.COMPLETE_VALIDATOR_CALL] < spec[7]:
            failures.append((row["name"], "lost-boot-audit", owned[Kind.COMPLETE_VALIDATOR_CALL], spec[7]))
    return tuple(failures)


def check_native_products(root, executable_paths):
    from tools.generate_indexed_bootstrap_fixture import source_bundle

    root = Path(root).resolve()
    build = root / "build/host-debug"
    expected = tuple(str(build / name) for name in TARGETS)
    if tuple(executable_paths) != expected:
        raise ValueError("BOOT01 requires its two owned executables")
    products = {path: read_regular(path)[1] for path in expected}
    generated_inputs, measured = {}, {}
    nm = native_symbols_tool()
    for channel, path in zip(("ordinary", "work"), expected, strict=True):
        for relative, raw in source_bundle(root, channel).items():
            generated = build / "indexed-bootstrap" / channel / relative
            actual, identity = read_regular(generated)
            if actual != raw or identity["mode"] != 0o644:
                raise ValueError("BOOT01 compiled input is stale or foreign")
            generated_inputs[str(generated)] = identity
        symbols = subprocess.check_output((nm["path"], "-a", path), cwd=root, text=True, timeout=30)
        wanted = frozenset() if channel == "ordinary" else frozenset((
            "micros_test_work_begin", "micros_test_work_finish", "micros_test_work_record",
        ))
        if observation_symbols(symbols) != wanted:
            raise ValueError("BOOT01 native product has the wrong collector linkage")
        completed = subprocess.run((path, "--run", channel), cwd=root, stdin=subprocess.DEVNULL,
                                   capture_output=True, text=True, timeout=30)
        if completed.returncode or completed.stderr:
            raise ValueError("BOOT01 native product failed:\n" + completed.stdout + completed.stderr)
        measured[channel] = parse_native_output(completed.stdout, channel)
        print(f"MICROS_BOOT01_NATIVE_ACCEPTED channel={channel} cases={len(measured[channel][0])}", flush=True)
    if ({path: read_regular(path)[1] for path in expected} != products
        or {path: read_regular(path)[1] for path in generated_inputs} != generated_inputs
        or native_symbols_tool() != nm):
        raise ValueError("BOOT01 native products or tools changed during execution")
    if measured["ordinary"][0] != measured["work"][0] or measured["ordinary"][1]:
        raise ValueError("BOOT01 passive observations changed semantics")
    failures = work_failures(*measured["work"])
    if failures:
        raise ValueError("BOOT01 independent work bounds failed: " + repr(failures))
    return measured


def _target_transfer_objects(root, directory, commands, owners, compiler):
    from tests.host.indexed_bootstrap_source import MOVEMENT_HELPERS
    from tools.stack_bound_manifest import owned_artifact

    expected = frozenset((*MOVEMENT_HELPERS, "lib/runtime/memory.c"))
    selected = {}
    for command in commands:
        source = Path(command["file"])
        output = Path(command["output"])
        source = source if source.is_absolute() else directory / source
        output = output if output.is_absolute() else directory / output
        if source not in {root / name for name in expected}:
            continue
        if not output.is_relative_to(directory) or str(output.relative_to(directory)) not in owners:
            continue
        output = owned_artifact(directory, str(output.relative_to(directory)))
        argv = tuple(shlex.split(command["command"]))
        if (Path(command["directory"]).resolve() != directory
            or not argv or argv[0] != compiler
            or argv.count("-c") != 1 or argv.count("-o") != 1
            or max(argv.index("-c"), argv.index("-o")) + 1 >= len(argv)
            or any(argument.startswith("@") for argument in argv)):
            raise ValueError("BOOT03 kernel transfer has a foreign or incomplete compiler command")
        operands = []
        for option in ("-c", "-o"):
            operand = Path(argv[argv.index(option) + 1])
            operands.append(operand if operand.is_absolute() else directory / operand)
        if tuple(operands) != (source, output):
            raise ValueError("BOOT03 compiler operands disagree with their linked owner")
        name = str(source.relative_to(root))
        if name in selected:
            raise ValueError("BOOT03 kernel transfer has duplicate source ownership")
        selected[name] = output
    if selected.keys() != expected:
        raise ValueError("BOOT03 kernel lacks its exact four caller objects and memory provider")
    return selected


def check_target_product(root):
    from tests.host.indexed_bootstrap_source import MOVEMENT_HELPERS
    from tests.host.runtime_memory_evidence import runtime_compile_commands, validate_green_source, wide_data_transfers
    from tests.host.source_snapshot import source_bytes
    from tools.stack_bound_manifest import owned_artifact
    from tools.stack_bound_collect import image_map_text
    from tools.stack_bound_flow import function_flow, reachable_instructions
    from tools.stack_bound_link import text_owners
    from tools.stack_bound_objects import function_symbols, load_object
    from tools.stack_bound_relocations import verify_linked_object
    from tools.user_elf_parse import load_elf
    from tools.validation_context import compiler_version, target_compiler, validate_environment
    from tools.validation_inputs import _file_identity, _run

    root = Path(root).resolve()
    directory = owned_artifact(root, "build/riscv64-bootstrap-launcher-test")
    relative_image = "kernel/micros.elf"
    memory = "lib/runtime/memory.c"
    sources = (
        *MOVEMENT_HELPERS, memory, "lib/runtime/memory.h", "include/micros/work_observation.h",
        "include/micros/bootstrap.h", "kernel/boot/bootstrap_manifest_internal.h",
        "kernel/boot/bootstrap_control_internal.h", "kernel/boot/bootstrap_runtime_internal.h",
        "kernel/boot/bootstrap_image.h",
    )
    source_identities = {name: read_regular(owned_artifact(root, name))[1] for name in sources}
    validate_green_source(source_bytes(root, root / memory).decode())
    validate_environment(dict(os.environ), target=True)
    compiler = target_compiler(dict(os.environ))
    compiler_identity = _file_identity(compiler.resolve())
    version = _run((str(compiler), "--version")).decode()
    compiler_version(version, target=True)
    image_path = owned_artifact(directory, relative_image)
    image_data, image_identity = read_regular(image_path)
    image = load_elf(image_path)
    if image.data != image_data:
        raise ValueError("BOOT03 kernel image changed before byte proof")
    map_text = image_map_text(directory, relative_image)
    owners = {name for name, _ in text_owners(map_text)}
    commands = runtime_compile_commands(directory)
    selected = _target_transfer_objects(root, directory, commands, owners, str(compiler))
    identities, objects, symbols = {}, {}, {}
    for source, output in selected.items():
        raw, identities[source] = read_regular(output)
        obj = load_object(output)
        if obj.data != raw:
            raise ValueError("BOOT03 kernel object changed before byte proof")
        retained = verify_linked_object(obj, str(output.relative_to(directory)), image, map_text)
        functions = function_symbols(obj)
        names = MOVEMENT_HELPERS[source] if source in MOVEMENT_HELPERS else (
            "micros_runtime_memcpy", "micros_runtime_memset",
        )
        if any(name not in functions or functions[name].section_index not in retained for name in names):
            raise ValueError("BOOT03 transfer function is absent from its actual linked owner")
        objects[source], symbols[source] = obj, functions
    helpers = 0
    for source, functions in MOVEMENT_HELPERS.items():
        for name, (unit, _) in functions.items():
            provider = "micros_runtime_memcpy" if unit == "COPY_UNIT" else "micros_runtime_memset"
            flow = function_flow(objects[source], symbols[source][name])
            if (len(flow.calls) != 1 or flow.calls[0].target_symbol != provider
                or flow.calls[0].offset not in reachable_instructions(flow)):
                raise ValueError("BOOT03 helper lacks its unique reachable provider call: " + name)
            helpers += 1
    transfers = {
        operation: wide_data_transfers(objects[memory], symbols[memory][name], operation)
        for operation, name in (("copy", "micros_runtime_memcpy"), ("fill", "micros_runtime_memset"))
    }
    if (any(symbol.name in ("memcpy", "memset") for symbol in image.symbols)
        or any(symbol.name.startswith("micros_test_work_") for symbol in objects[memory].symbols)):
        raise ValueError("BOOT03 kernel added a memory forwarder or instrumented the ordinary provider")
    if ({name: read_regular(owned_artifact(root, name))[1] for name in sources} != source_identities
        or {name: read_regular(owned_artifact(directory, str(output.relative_to(directory))))[1]
            for name, output in selected.items()} != identities
        or read_regular(image_path)[1] != image_identity
        or image_map_text(directory, relative_image) != map_text
        or runtime_compile_commands(directory) != commands
        or _file_identity(compiler.resolve()) != compiler_identity
        or _run((str(compiler), "--version")).decode() != version):
        raise ValueError("BOOT03 source, compiler, image, object or recipe changed during proof")
    return {
        "image": image_identity, "objects": identities, "compiler": compiler_identity,
        "map_sha256": hashlib.sha256(map_text.encode()).hexdigest(),
        "helpers": helpers, "wide_transfers": transfers,
    }

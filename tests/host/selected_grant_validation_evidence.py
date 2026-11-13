"""Independent selected-grant semantics, work and current-product evidence."""

import json
import hashlib
import os
from pathlib import Path
import re
import shlex
import subprocess

from tests.host.evidence_records import record_fields
from tests.host.object_hart_validation_evidence import (
    NATIVE_HEADERS as OBJECT_HEADERS, NATIVE_SOURCES as OBJECT_SOURCES, observation_symbols,
)
from tests.host.runtime_memory_evidence import json_pairs
from tests.host.selected_address_space_evidence import native_symbols_tool
from tests.host.work_count_snapshots import compose_snapshots
from tests.host.work_count_types import Capture, Kind, Owner, Snapshot, Status
from tools.stack_artifact_files import read_regular


NATIVE_SOURCES = OBJECT_SOURCES + tuple("kernel/endpoint/" + name + ".c" for name in (
    "endpoint", "endpoint_close", "endpoint_codec", "endpoint_completion_validation",
    "endpoint_identity", "endpoint_object_validation", "endpoint_privilege",
    "endpoint_publication", "endpoint_queue_validation", "endpoint_registry",
)) + tuple("kernel/grant/" + name + ".c" for name in (
    "grant", "grant_copy_core", "grant_copy", "grant_copy_range", "grant_runtime",
    "grant_syscall", "grant_syscall_core", "grant_abi",
)) + (
    "lib/runtime/memory.c",
    "kernel/ipc/ipc_runtime.c", "kernel/tty/tty_handoff_core.c",
    "kernel/tty/tty_handoff_runtime_state.c", "kernel/tty/tty_handoff_runtime_validate.c",
    "kernel/boot/bootstrap_control_core.c",
)
NATIVE_SUBSETS = {"kernel/boot/bootstrap_control_core.c": ("micros_bootstrap_control_find_binding",)}
OBSERVED_HEADERS = ("kernel/boot/bootstrap_control_internal.h",)
NATIVE_BACKENDS = {"arch/riscv64/interrupt.h": "tests/host/selected_grant_irq.h"}
NATIVE_HEADERS = OBJECT_HEADERS + (
    "include/micros/endpoint.h", "include/micros/grant.h", "include/micros/grant_copy.h",
    "include/micros/user_address_space.h", "include/micros/sv39.h",
    "include/micros/work_observation.h", "kernel/grant/grant_internal.h",
    "lib/runtime/memory.h",
    "kernel/endpoint/endpoint_private.h", "kernel/endpoint/endpoint_internal.h",
    "tests/qemu/selected_ipc_work.h",
    "arch/riscv64/interrupt.h", "kernel/boot/bootstrap_control_internal.h",
    "kernel/grant/grant_copy_internal.h", "kernel/grant/grant_runtime_internal.h",
    "kernel/grant/grant_syscall.h", "kernel/ipc/ipc_runtime_internal.h",
    "kernel/trap/syscall.h", "kernel/tty/tty_handoff_core.h",
    "kernel/tty/tty_handoff_runtime.h", "kernel/tty/tty_handoff_runtime_internal.h",
    "arch/riscv64/panic_context.h", "arch/riscv64/trap_context.h",
    "include/micros/bootstrap.h", "include/micros/bootstrap_control.h",
    "include/micros/frame_allocator.h", "include/micros/frame_ownership.h",
    "include/micros/frame_ownership_runtime.h", "include/micros/grant_runtime.h",
    "include/micros/grant_syscall_core.h", "include/micros/ipc_core.h", "include/micros/ipc_runtime.h",
    "include/micros/kernel_address_space.h", "include/micros/kernel_object_runtime.h",
    "include/micros/panic.h", "include/micros/spawn.h", "include/micros/syscall_abi.h",
    "include/micros/tty.h", "include/micros/user_address_space_core.h",
    "include/micros/vm.h", "include/micros/vm_bootstrap.h",
    "kernel/memory/user_address_space_internal.h", "kernel/vm/vm_handoff_core.h",
)
TARGETS = ("selected_grant_validation_ordinary", "selected_grant_validation_test")
PRODUCER = 0x4752414e54303101
OWNER_MASK = sum(1 << owner for owner in (
    Owner.KERNEL_OBJECTS, Owner.KERNEL_SCHEDULER, Owner.KERNEL_ENDPOINT, Owner.KERNEL_IPC,
    Owner.KERNEL_GRANT, Owner.KERNEL_BOOT, Owner.KERNEL_TTY, Owner.RUNTIME,
))
CASE_FIELDS = ("version", "case", "name", "result", "value", "preserved", "before_state", "after_state")
WORK_FIELDS = ("version", "reserved0", "producer", "case", "owner_mask", "reserved1", "counts")
CASES = (
    ("create-empty", 0, 64, 0, 0), ("create-last", 0, 127, 0, 0),
    ("create-full", 4, (1 << 32) - 1, 1, 0),
    ("inspect-first", 0, 64, 1, 0), ("inspect-last", 0, 64, 1, 0),
    ("inspect-stale", 5, 0, 1, 0),
    ("revoke-first", 0, 2, 0, 0), ("revoke-last", 0, 2, 0, 0),
    ("authority-first", 0, 0x40000008, 1, 0),
    ("authority-last", 0, 0x40000fc8, 1, 0),
    ("authority-write", 0, 0x400007c8, 1, 0),
    ("diagnostic-dense", 0, 64, 1, 384),
    ("cancel-dense", 0, (1 << 64) - 1, 1, 128),
    ("cancel-empty", 0, 0, 1, 0), ("cancel-sparse", 0, 1, 1, 2),
    ("syscall-unavailable", 12, 0, 1, 0),
    ("syscall-revoke", 0, 0, 0, 0),
    ("syscall-shape", 0, (1 << 64) - 1, 1, 0),
    ("syscall-create", 0, 64, 0, 0),
    ("copy-from", 0, 32, 1, 0), ("copy-to", 0, 32, 1, 0),
    ("validate-remote", 0, 0, 1, 0), ("copy-device", 10, 0, 1, 0),
    ("copy-same-va-ram", 0, 32, 1, 0),
    ("syscall-create-device", 0, (1 << 64) - 6, 1, 0),
    ("syscall-copy-from", 0, 0, 1, 0), ("syscall-copy-to", 0, 0, 1, 0),
    ("syscall-validate", 0, 0, 1, 0),
    ("copy-no-device", 0, 32, 1, 0), ("copy-prepared-device", 0, 32, 1, 0),
    ("copy-starting-device", 0, 32, 1, 0),
    ("copy-bad-device-thread", 12, 0, 1, 0), ("copy-bad-device-root", 12, 0, 1, 0),
    ("copy-bad-device-route", 12, 0, 1, 0), ("copy-unused-bootstrap-policy", 0, 32, 1, 0),
    ("syscall-readiness-before-shape", 12, 0, 1, 0),
    ("syscall-shape-unrelated-corruption", 0, (1 << 64) - 1, 1, 0),
    ("syscall-selected-invariant", 12, 0, 1, 0), ("syscall-create-device-invariant", 12, 0, 1, 0),
    ("copy-zero-before-device", 0, 0, 1, 0), ("copy-phase-before-zero", 11, 0, 1, 0),
    ("copy-range-before-phase", 8, 0, 1, 0),
    ("copy-fault-invariant", 12, 0, 1, 0), ("copy-invariant-fault", 12, 0, 1, 0),
    ("copy-device-tail-invariant", 12, 0, 1, 0),
    ("copy-missing-ledger", 12, 0, 1, 0), ("copy-revalidates-after-validation", 5, 0, 1, 0),
    ("inspect-sparse-high", 0, 64, 1, 0),
    ("inspect-unrelated-grant", 0, 64, 1, 0),
    ("inspect-unrelated-endpoint", 0, 64, 1, 0),
    ("inspect-count-mismatch", 0, 64, 1, 0),
    ("revoke-zero-count", 12, 0, 1, 0),
    ("inspect-stale-corrupt", 12, 0, 1, 0),
    ("authority-stored-participant", 12, 0, 1, 0),
    ("inspect-caller-profile", 12, 0, 1, 0),
    ("inspect-caller-primary", 12, 0, 1, 0),
    ("inspect-unbound-profile", 12, 0, 1, 0),
    ("create-unvisited-suffix", 0, 64, 0, 0),
    ("create-prefix-storage", 12, (1 << 32) - 1, 1, 0),
    ("create-prefix-count", 12, (1 << 32) - 1, 1, 0),
    ("create-full-count", 12, (1 << 32) - 1, 1, 0),
    ("create-quarantine-count", 4, (1 << 32) - 1, 1, 0),
    ("create-all-quarantined", 4, (1 << 32) - 1, 1, 0),
    ("create-skipped-relation", 0, 67, 0, 0),
    ("create-participant-before-prefix", 6, (1 << 32) - 1, 1, 0),
    ("inspect-source-only", 9, 0, 1, 0),
    ("index-free-shape", 12, 0, 1, 0), ("index-free-stale", 12, 0, 1, 0),
    ("index-free-active", 12, 0, 1, 0), ("index-free-cycle", 12, 0, 1, 0),
    ("index-root-tag", 12, 0, 1, 0),
    ("index-revoke-stale", 12, 0, 1, 0), ("index-revoke-reciprocal", 12, 0, 1, 0),
    ("index-inspect-unconsumed", 0, 64, 1, 0),
    ("index-prepare-cycle", 12, 0, 1, 6), ("index-prepare-role", 12, 0, 1, 2),
    ("cancel-prepare-poison", 0, 1, 1, 2),
    ("cancel-commit-extra", 12, 0, 1, 2), ("cancel-commit-missing", 12, 0, 1, 2),
    ("cancel-commit-generation", 12, 0, 1, 2),
    ("cancel-commit-empty", 0, 64, 1, 0), ("cancel-commit-sparse", 0, 63, 0, 2),
    ("cancel-commit-dense", 0, 0, 0, 128),
    ("cancel-commit-mixed", 0, 2, 0, 8), ("cancel-commit-closed", 0, 2, 0, 8),
    ("cancel-commit-quarantine", 0, 2, 0, 8),
)
SPAN_CASES = (
    # name, bytes, source/destination offsets, first-chunk cuts, result, copies, core-only
    ("span-zero", 0, 0, 0, 0, 0, 0, 1, 1),
    ("span-one", 1, 0, 0, 0, 0, 0, 1, 1),
    ("span-seven", 7, 0, 0, 0, 0, 0, 1, 1),
    ("span-eight", 8, 0, 0, 0, 0, 0, 1, 1),
    ("span-nine", 9, 0, 0, 0, 0, 0, 1, 1),
    ("span-block", 64, 0, 0, 0, 0, 0, 1, 1),
    ("span-page", 4096, 0, 0, 0, 0, 0, 1, 1),
    ("span-prefix-tail", 64, 1, 1, 0, 0, 0, 1, 1),
    ("span-different-residues", 64, 0, 1, 0, 0, 0, 1, 1),
    ("span-source-cross", 4096, 4088, 0, 8, 0, 0, 1, 1),
    ("span-destination-cross", 4096, 0, 4080, 0, 16, 0, 1, 1),
    ("span-coincident-cross", 4096, 4080, 4080, 16, 16, 0, 1, 1),
    ("span-unequal-cross", 4096, 4088, 4080, 8, 16, 0, 1, 1),
    ("span-unaligned-cross", 4096, 4083, 4075, 13, 21, 0, 1, 1),
    ("span-reversed", 64, 0, 0, 0, 0, 0, 1, 1),
    ("span-from-page", 4096, 4088, 4080, 8, 16, 0, 1, 0),
    ("span-to-page", 4096, 4083, 4075, 13, 21, 0, 1, 0),
    ("span-validate-page", 4096, 4088, 4080, 8, 16, 0, 0, 0),
    ("span-late-fault", 4096, 4088, 4080, 8, 16, 10, 0, 0),
    ("span-late-invariant", 4096, 4088, 4080, 8, 16, 12, 0, 0),
    ("span-physical-overlap", 64, 0, 0, 0, 0, 12, 0, 0),
)
SPAN_GEOMETRY = {row[0]: row[1:] for row in SPAN_CASES}
CASES += tuple((name, result, length if copies else 0, 1, 0)
               for name, length, _, _, _, _, result, copies, _ in SPAN_CASES)
COLD_CASES = frozenset(("diagnostic-dense",))
CANCELLATION_COUNTS = {
    "cancel-empty": 0, "cancel-sparse": 1, "cancel-dense": 64,
    "index-prepare-cycle": 3, "index-prepare-role": 1, "cancel-prepare-poison": 1,
}
CANCELLATION_COMMITS = {
    "cancel-commit-extra": 1, "cancel-commit-missing": 1, "cancel-commit-generation": 1,
    "cancel-commit-empty": 0, "cancel-commit-sparse": 1, "cancel-commit-dense": 64,
    "cancel-commit-mixed": 4, "cancel-commit-closed": 4, "cancel-commit-quarantine": 4,
}


def span_work(name):
    length, source, destination, source_cut, destination_cut, _, copies, core = SPAN_GEOMETRY[name]
    cuts = sorted({0, length, source_cut, destination_cut})
    words = 0
    for start, end in zip(cuts, cuts[1:]):
        source_residue = (start - source_cut if source_cut and start >= source_cut else source + start) % 8
        destination_residue = (start - destination_cut if destination_cut and start >= destination_cut
                               else destination + start) % 8
        if source_residue == destination_residue:
            first_aligned = start + (-source_residue) % 8
            words += max(0, (end - first_aligned) // 8)
    return len(cuts) - 1, length - 7 * words if copies else 0, length if copies else 0, core


def expected_cases():
    return tuple({
        "version": 1, "case": index, "name": name, "result": result,
        "value": value, "preserved": preserved,
    } for index, (name, result, value, preserved, _) in enumerate(CASES, 1))


def case_identities(records):
    return tuple({key: value for key, value in row.items()
                  if key not in ("before_state", "after_state")} for row in records)


def work_totals(row):
    record_fields(row, WORK_FIELDS)
    if (any(type(row[key]) is not int for key in WORK_FIELDS if key != "counts")
        or type(row["counts"]) is not list
        or any(type(values) is not list for values in row["counts"])):
        raise ValueError("GRANT01 work fields have an invalid type")
    snapshot = Snapshot(
        row["producer"], row["case"], row["owner_mask"], tuple(tuple(values) for values in row["counts"]),
        row["version"], row["reserved0"], row["reserved1"],
    )
    return compose_snapshots(
        (Capture(Status.OK, snapshot),), capture_label=row["case"],
        expected_producers={PRODUCER: OWNER_MASK},
    )


def parse_native_output(output, channel):
    if channel not in ("ordinary", "work") or type(output) is not str or not output.endswith("\n"):
        raise ValueError("GRANT01 native transcript is incomplete or has an unknown channel")
    records, work = [], []
    lines = output.splitlines()
    for line in lines[:-1]:
        if line.startswith("MICROS_GRANT01_CASE "):
            if channel == "work" and len(work) != len(records):
                raise ValueError("GRANT01 native case omitted its work snapshot")
            row = json.loads(line.removeprefix("MICROS_GRANT01_CASE "), object_pairs_hook=json_pairs)
            record_fields(row, CASE_FIELDS)
            if (
                any(type(row[key]) is not int or not 0 <= row[key] < 1 << 64
                    for key in CASE_FIELDS if key != "name")
                or type(row["name"]) is not str or re.fullmatch(r"[a-z-]+", row["name"]) is None
                or row["case"] != len(records) + 1
            ):
                raise ValueError("GRANT01 native case has malformed fields or order")
            records.append(row)
        elif line.startswith("MICROS_GRANT01_WORK "):
            row = json.loads(line.removeprefix("MICROS_GRANT01_WORK "), object_pairs_hook=json_pairs)
            work_totals(row)
            if channel != "work" or row["case"] != len(records) or len(work) + 1 != len(records):
                raise ValueError("GRANT01 work belongs to another or duplicate case")
            work.append(row)
        else:
            raise ValueError("GRANT01 native transcript contains an unknown record")
    if (case_identities(records) != expected_cases()
        or lines[-1:] != ["MICROS_GRANT01_NATIVE_PASS cases=" + str(len(records))]
        or channel == "work" and len(work) != len(records)):
        raise ValueError("GRANT01 native evidence omits or changes an independent case")
    return tuple(records), tuple(work)


def work_failures(records, work):
    if case_identities(records) != expected_cases() or len(records) != len(work):
        raise ValueError("GRANT01 requires the complete independent case set")
    failures = []
    for row, snapshot, spec in zip(records, work, CASES, strict=True):
        if row["case"] != snapshot["case"]:
            raise ValueError("GRANT01 borrowed a work snapshot")
        totals = work_totals(snapshot)
        audits = totals.per_kind[Kind.COMPLETE_VALIDATOR_CALL]
        grants = totals.per_owner[Owner.KERNEL_GRANT][Kind.RECORD_VISIT]
        if row["name"] in SPAN_GEOMETRY:
            spans, units, copied, core = span_work(row["name"])
            expected = {Kind.COPY_UNIT: units}
            if core:
                expected.update({
                    Kind.COPIED_BYTE: copied, Kind.RANGE_VISIT: spans, Kind.INDEX_PROBE: 2 * spans,
                })
            elif copied == 0 and totals.per_owner[Owner.RUNTIME][Kind.COPIED_BYTE] != 0:
                failures.append((row["name"], "rejected-data-copy", totals.per_owner[Owner.RUNTIME][Kind.COPIED_BYTE], 0))
            for kind, count in expected.items():
                if totals.per_kind[kind] != count:
                    failures.append((row["name"], kind.name.lower(), totals.per_kind[kind], count))
            if any(totals.per_owner[owner][Kind.COPY_UNIT] for owner in Owner
                   if owner not in (Owner.KERNEL_GRANT, Owner.RUNTIME)):
                failures.append((row["name"], "foreign-copy-owner", 1, 0))
        if row["name"] in COLD_CASES:
            if audits == 0:
                failures.append((row["name"], "lost-complete-diagnostic", audits, 1))
        else:
            if audits:
                failures.append((row["name"], "complete-audits", audits, 0))
            other = totals.per_kind[Kind.RECORD_VISIT] - grants
            if other:
                failures.append((row["name"], "unrelated-table-visits", other, 0))
            # Fixed metadata and per-incident selected checks, not a capacity scan.
            byte_limits = {
                Kind.COMPARISON: 512 + 64 * spec[4],
                Kind.COPIED_BYTE: 256,
                Kind.CLEARED_BYTE: 256,
            }
            if row["name"] in SPAN_GEOMETRY:
                _, _, copied, core = span_work(row["name"])
                byte_limits[Kind.COPIED_BYTE] = copied + (0 if core else 256)
            if row["name"] in CANCELLATION_COUNTS:
                count = CANCELLATION_COUNTS[row["name"]]
                byte_limits[Kind.COPIED_BYTE] = 32 + 8 * count
                byte_limits[Kind.CLEARED_BYTE] = 32
            if row["name"] in CANCELLATION_COMMITS:
                count = CANCELLATION_COMMITS[row["name"]]
                byte_limits[Kind.COPIED_BYTE] = 32 + 72 * count
                byte_limits[Kind.CLEARED_BYTE] = 32 + 72 * count
            for kind, bound in byte_limits.items():
                if totals.per_kind[kind] > bound:
                    failures.append((row["name"], kind.name.lower(), totals.per_kind[kind], bound))
        if grants > spec[4] or row["name"].startswith("create-") and grants != spec[4]:
            failures.append((row["name"], "grant-table-visits", grants, spec[4]))
    return tuple(failures)


def check_native_products(root, executable_paths):
    from tools.generate_selected_grant_validation_fixture import source_bundle

    root = Path(root).resolve()
    build = root / "build/host-debug"
    expected = tuple(str(build / name) for name in TARGETS)
    if tuple(executable_paths) != expected:
        raise ValueError("GRANT01 requires its two owned executables")
    products = {path: read_regular(path)[1] for path in expected}
    generated_inputs, measured = {}, {}
    nm = native_symbols_tool()
    for channel, path in zip(("ordinary", "work"), expected, strict=True):
        for relative, raw in source_bundle(root, channel).items():
            generated = build / "selected-grant-validation" / channel / relative
            actual, identity = read_regular(generated)
            if actual != raw or identity["mode"] != 0o644:
                raise ValueError("GRANT01 compiled input is stale or foreign")
            generated_inputs[str(generated)] = identity
        symbols = subprocess.check_output((nm["path"], "-a", path), cwd=root, text=True, timeout=30)
        wanted = frozenset() if channel == "ordinary" else frozenset((
            "micros_test_work_begin", "micros_test_work_finish", "micros_test_work_record",
        ))
        if observation_symbols(symbols) != wanted:
            raise ValueError("GRANT01 native product has the wrong collector linkage")
        completed = subprocess.run(
            (path, "--run", channel), cwd=root, stdin=subprocess.DEVNULL,
            capture_output=True, text=True, timeout=30,
        )
        if completed.returncode or completed.stderr:
            raise ValueError("GRANT01 compiled native product failed:\n" + completed.stdout + completed.stderr)
        measured[channel] = parse_native_output(completed.stdout, channel)
        print(f"MICROS_GRANT01_NATIVE_ACCEPTED channel={channel} cases={len(measured[channel][0])}", flush=True)
    if ({path: read_regular(path)[1] for path in expected} != products
        or {path: read_regular(path)[1] for path in generated_inputs} != generated_inputs
        or native_symbols_tool() != nm):
        raise ValueError("GRANT01 native products or tools changed during execution")
    if measured["ordinary"][0] != measured["work"][0] or measured["ordinary"][1]:
        raise ValueError("GRANT01 passive observations changed semantics")
    failures = work_failures(*measured["work"])
    if failures:
        raise ValueError("GRANT01 independent work bounds failed: " + repr(failures))
    return measured


def _target_copy_objects(root, directory, commands, owners, compiler):
    from tools.stack_bound_manifest import owned_artifact

    expected = frozenset(("kernel/grant/grant_copy_core.c", "lib/runtime/memory.c"))
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
            raise ValueError("GRANT03 kernel copy has a foreign or incomplete compiler command")
        operands = []
        for option in ("-c", "-o"):
            operand = Path(argv[argv.index(option) + 1])
            operands.append(operand if operand.is_absolute() else directory / operand)
        if tuple(operands) != (source, output):
            raise ValueError("GRANT03 kernel copy compiler operands disagree with their linked owner")
        name = str(source.relative_to(root))
        if name in selected:
            raise ValueError("GRANT03 kernel copy has duplicate source ownership")
        selected[name] = output
    if selected.keys() != expected:
        raise ValueError("GRANT03 kernel lacks its exact copy caller and provider objects")
    return selected


def check_target_product(root):
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
    directory = owned_artifact(root, "build/riscv64-grant-test")
    relative_image = "kernel/micros.elf"
    image_path = owned_artifact(directory, relative_image)
    sources = (
        "kernel/grant/grant_copy_core.c", "lib/runtime/memory.c", "lib/runtime/memory.h",
        "include/micros/grant_copy.h", "include/micros/work_observation.h",
    )
    source_identities = {name: read_regular(owned_artifact(root, name))[1] for name in sources}
    validate_green_source(source_bytes(root, root / "lib/runtime/memory.c").decode())
    validate_environment(dict(os.environ), target=True)
    compiler = target_compiler(dict(os.environ))
    compiler_identity = _file_identity(compiler.resolve())
    version = _run((str(compiler), "--version")).decode()
    compiler_version(version, target=True)
    image_data, image_identity = read_regular(image_path)
    image = load_elf(image_path)
    if image.data != image_data:
        raise ValueError("GRANT03 kernel image changed before byte proof")
    map_text = image_map_text(directory, relative_image)
    owners = {name for name, _ in text_owners(map_text)}
    commands = runtime_compile_commands(directory)
    selected = _target_copy_objects(root, directory, commands, owners, str(compiler))
    identities, objects, symbols = {}, {}, {}
    names = {
        "kernel/grant/grant_copy_core.c": "micros_grant_copy_commit",
        "lib/runtime/memory.c": "micros_runtime_memcpy",
    }
    for source, output in selected.items():
        raw, identities[source] = read_regular(output)
        obj = load_object(output)
        if obj.data != raw:
            raise ValueError("GRANT03 kernel object changed before byte proof")
        retained = verify_linked_object(obj, str(output.relative_to(directory)), image, map_text)
        symbol = function_symbols(obj).get(names[source])
        if symbol is None or symbol.section_index not in retained:
            raise ValueError("GRANT03 kernel copy function is absent from its linked owner")
        objects[source], symbols[source] = obj, symbol
    core, memory = "kernel/grant/grant_copy_core.c", "lib/runtime/memory.c"
    flow = function_flow(objects[core], symbols[core])
    if (len(flow.calls) != 1 or flow.calls[0].target_symbol != names[memory]
        or flow.calls[0].offset not in reachable_instructions(flow)):
        raise ValueError("GRANT03 kernel commit lacks its unique reachable copy-provider call")
    transfers = wide_data_transfers(objects[memory], symbols[memory], "copy")
    if any(symbol.name in ("memcpy", "memset") or symbol.name.startswith("micros_test_work_")
           for symbol in image.symbols):
        raise ValueError("GRANT03 ordinary kernel contains a memory forwarder or work observer")
    if ({name: read_regular(owned_artifact(root, name))[1] for name in sources} != source_identities
        or {name: read_regular(owned_artifact(directory, str(output.relative_to(directory))))[1]
            for name, output in selected.items()} != identities
        or read_regular(owned_artifact(directory, relative_image))[1] != image_identity
        or image_map_text(directory, relative_image) != map_text
        or runtime_compile_commands(directory) != commands
        or _file_identity(compiler.resolve()) != compiler_identity
        or _run((str(compiler), "--version")).decode() != version):
        raise ValueError("GRANT03 kernel source, compiler, image, object or recipe changed during proof")
    return {
        "image": image_identity, "objects": identities, "compiler": compiler_identity,
        "map_sha256": hashlib.sha256(map_text.encode()).hexdigest(), "wide_copy_sites": transfers,
    }

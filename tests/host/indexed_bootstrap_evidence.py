"""Independent bootstrap lookup/transition semantics and bounded native work."""

import json
from pathlib import Path
import re
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
    "kernel/boot/bootstrap_runtime_state.c", "kernel/boot/bootstrap_diagnostics.c",
    "kernel/tty/tty_handoff_core.c", "kernel/tty/tty_handoff_runtime_state.c",
    "kernel/tty/tty_handoff_runtime.c",
)
NATIVE_SUBSETS = {
    "kernel/boot/bootstrap_runtime_state.c": ("bootstrap_runtime_context",),
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
)
OBSERVED_HEADERS = ("kernel/boot/bootstrap_control_internal.h",)
TARGETS = ("indexed_bootstrap_ordinary", "indexed_bootstrap_test")
PRODUCER = 0x424f4f5430310001
OWNER_MASK = sum(1 << owner for owner in (
    Owner.KERNEL_OBJECTS, Owner.KERNEL_SCHEDULER, Owner.KERNEL_ENDPOINT, Owner.KERNEL_IPC, Owner.KERNEL_BOOT,
    Owner.KERNEL_TTY,
))
CASE_FIELDS = ("version", "case", "name", "result", "value", "preserved", "before_state", "after_state")
WORK_FIELDS = ("version", "reserved0", "producer", "case", "owner_mask", "reserved1", "counts")
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
    ("control-release", 0, 110, 0, -1, 192, 96, 2),
    ("control-ready-plan", 0, 2, 1, -1, 320, 224, 1),
    ("control-ready-commit", 0, 2, 0, 0, 64, 160, 0),
    ("control-ready-role", 8, 0, 1, -1, 192, 224, 1),
    ("control-complete-plan", 0, 1, 1, -1, 384, 288, 1),
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
)


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
        for kind, bound in (
            (Kind.RECORD_VISIT, spec[4]), (Kind.COPIED_BYTE, spec[5]), (Kind.CLEARED_BYTE, spec[6]),
        ):
            if bound >= 0 and owned[kind] > bound:
                failures.append((row["name"], kind.name.lower(), owned[kind], bound))
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

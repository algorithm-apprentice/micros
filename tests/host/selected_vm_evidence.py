"""Independent selected VM native semantics and consumed-work bounds."""

import json
from pathlib import Path
import re
import subprocess

from tests.host.evidence_records import record_fields
from tests.host.object_hart_validation_evidence import observation_symbols
from tests.host.runtime_memory_evidence import json_pairs
from tests.host.selected_address_space_evidence import native_symbols_tool
from tests.host.work_count_snapshots import compose_snapshots
from tests.host.work_count_types import Capture, Kind, Owner, Snapshot, Status
from tools.stack_artifact_files import read_regular


NATIVE_SOURCES = tuple("kernel/vm/vm_handoff_" + part + ".c" for part in (
    "core", "abi", "boot", "load", "scratch", "freeze", "commit", "release",
)) + tuple("kernel/memory/" + part + ".c" for part in (
    "frame_allocator", "frame_ownership", "frame_ownership_validation",
    "frame_ownership_bootstrap", "frame_ownership_handoff", "frame_ownership_vm_claim",
    "frame_ownership_vm_freeze", "frame_ownership_vm_release",
)) + (
    "kernel/vm/vm_bootstrap.c", "lib/vm/vm_boot_validate.c", "lib/vm/vm_keys.c",
    "lib/vm/vm_load_token.c",
    "lib/spawn/spawn_request.c", "lib/spawn/spawn_stack_layout.c",
) + tuple("servers/vm/vm_" + part + ".c" for part in (
    "image", "stack", "elf", "core", "validation", "frames", "load", "exit",
    "service_core", "service_protocol", "service_load", "service_release",
))
NATIVE_SUBSETS = {"kernel/vm/vm_bootstrap.c": ("micros_vm_boot_finalize",)}
NATIVE_HEADERS = (
    "include/micros/bootstrap.h", "include/micros/endpoint.h", "include/micros/frame_allocator.h",
    "include/micros/frame_ownership.h", "include/micros/grant.h", "include/micros/ipc.h",
    "include/micros/kernel_objects.h", "include/micros/runtime.h", "include/micros/spawn.h",
    "include/micros/sv39.h", "include/micros/syscall_abi.h", "include/micros/tty.h",
    "include/micros/user_address_space.h", "include/micros/user_address_space_core.h",
    "include/micros/user_context.h", "include/micros/vm.h", "include/micros/vm_bootstrap.h",
    "include/micros/vm_service.h", "include/micros/work_observation.h",
    "kernel/vm/vm_handoff_core.h", "kernel/vm/vm_handoff_internal.h",
    "kernel/memory/frame_ownership_internal.h",
    "lib/spawn/spawn_request.h", "lib/vm/vm_boot_internal.h", "lib/vm/vm_keys.h",
    "lib/vm/vm_load_token.h",
    "servers/vm/vm_core.h", "servers/vm/vm_image.h",
    "servers/vm/vm_image_internal.h", "servers/vm/vm_internal.h", "servers/vm/vm_admission.h",
    "servers/vm/vm_service_core.h", "servers/vm/vm_service_internal.h",
    "tests/qemu/vm_validation_work.h", "tests/work/work_counts.h",
)
TARGETS = ("selected_vm_ordinary", "selected_vm_test")
PRODUCER = 0x564d30314e000001
OWNER_MASK = sum(1 << owner for owner in (
    Owner.KERNEL_MEMORY, Owner.KERNEL_VM, Owner.LIB_VM, Owner.LIB_SPAWN, Owner.VM_SERVICE))
CASE_FIELDS = ("version", "case", "name", "result", "extent", "phase", "preserved", "before_state", "after_state")
WORK_FIELDS = ("version", "reserved0", "producer", "case", "owner_mask", "reserved1", "counts")
# name, result, phase, byte-preserving state, positive complete-diagnostic control
CASES = (
    ("service-prepare", 0, 2, 0, 0), ("service-bind", 0, 3, 0, 0),
    ("service-next-page", 0, 3, 1, 0), ("service-map", 0, 3, 0, 0),
    ("service-fill", 0, 3, 0, 0), ("service-page-commit", 0, 3, 0, 0),
    ("service-freeze-plan", 0, 3, 1, 0), ("service-freeze", 0, 4, 0, 0),
    ("service-commit", 0, 1, 0, 0), ("service-exit-begin", 0, 5, 0, 0),
    ("service-exit-complete", 0, 1, 0, 0), ("service-abort-loading", 0, 1, 0, 0),
    ("service-abort-frozen", 0, 1, 0, 0), ("service-stale-token", 3, 4, 1, 0),
    ("service-diagnostic", 0, 4, 1, 1),
    ("kernel-begin", 0, 1, 0, 0), ("kernel-map", 0, 1, 0, 0),
    ("kernel-page-commit", 0, 1, 0, 0), ("kernel-freeze", 0, 2, 0, 0),
    ("kernel-prepare", 0, 2, 0, 0), ("kernel-unprepare", 0, 2, 0, 0),
    ("kernel-activate", 0, 2, 0, 0), ("kernel-commit", 0, 0, 0, 0),
    ("kernel-exit-detach", 0, 0, 0, 0), ("kernel-exit-release", 0, 0, 0, 0),
    ("kernel-abort-loading", 0, 0, 0, 0), ("kernel-abort-frozen", 0, 0, 0, 0),
    ("kernel-diagnostic", 0, 0, 1, 1),
    ("message-load", 0, 4, 0, 0), ("message-abort", 0, 1, 0, 0),
    ("message-commit", 0, 1, 0, 0), ("message-exit", 0, 1, 0, 0),
    ("service-unvisited-frame", 0, 3, 1, 0), ("service-unvisited-input", 0, 3, 1, 0),
    ("service-unvisited-record", 0, 3, 1, 0), ("service-selected-count", 8, 3, 1, 0),
    ("service-zero-geometry", 8, 3, 1, 0), ("service-overflow-geometry", 8, 3, 1, 0),
    ("service-cleanup-preflight", 8, 4, 1, 0),
    ("message-abort-preflight", 2, 4, 1, 0), ("message-commit-preflight", 2, 4, 1, 0),
    ("message-load-preflight", 2, 1, 1, 0),
    ("kernel-unvisited-record", 0, 1, 1, 0), ("kernel-owner-unvisited", 0, 1, 1, 0),
    ("kernel-scanned-count", 7, 1, 1, 0),
    ("kernel-selected-slot", 7, 1, 1, 0),
    ("service-max-frames", 0, 3, 1, 0), ("service-max-ranges-pages", 0, 3, 1, 0),
    ("kernel-dense-records", 0, 2, 0, 0),
    ("boot-mapping-sparse", 0, 1, 1, 1), ("boot-mapping-max", 0, 1, 1, 1),
    ("boot-mapping-duplicate", 5, 1, 1, 1),
    ("service-range-to-index", 0, 1, 1, 0), ("service-index-to-range", 0, 1, 1, 0),
    ("kernel-global-records", 0, 0, 1, 1), ("kernel-global-duplicate", 7, 0, 1, 1),
    ("service-global-records", 0, 1, 1, 1), ("service-global-duplicate", 8, 1, 1, 1),
    ("service-membership-max", 0, 1, 1, 0),
    ("service-exit-unvisited", 0, 5, 0, 0), ("kernel-exit-unvisited", 0, 0, 1, 0),
    ("kernel-owner-collision", 3, 1, 1, 0), ("kernel-owner-bit", 7, 1, 1, 0),
    ("kernel-owner-link", 7, 1, 1, 0), ("kernel-owner-generation", 7, 1, 1, 0),
    ("kernel-table-collision", 1, 1, 1, 0),
)
BOOT_MAPPING_EXTENTS = {
    "boot-mapping-sparse": 257, "boot-mapping-max": 4096, "boot-mapping-duplicate": 4096,
}
GEOMETRY_EXTENTS = {"service-range-to-index": 96, "service-index-to-range": 96}
GLOBAL_RECORD_EXTENTS = {
    "kernel-global-records": 1260, "kernel-global-duplicate": 1260,
    "service-global-records": 1260, "service-global-duplicate": 1260,
}
MEMBERSHIP_EXTENTS = {"service-membership-max": 1260}
CASE_EXTENTS = {**BOOT_MAPPING_EXTENTS, **GEOMETRY_EXTENTS, **GLOBAL_RECORD_EXTENTS, **MEMBERSHIP_EXTENTS}
POINT_CASES = frozenset((
    "service-next-page", "service-map", "service-page-commit", "service-freeze-plan",
    "service-stale-token", "kernel-page-commit", "kernel-prepare", "kernel-unprepare",
    "kernel-activate", "kernel-commit",
    "service-unvisited-frame", "service-unvisited-input", "service-unvisited-record",
    "kernel-unvisited-record",
    "service-max-frames", "service-max-ranges-pages", "kernel-dense-records",
))


def expected_cases():
    return tuple({
        "version": 1, "case": index, "name": name, "result": result,
        "extent": CASE_EXTENTS.get(name, 0), "phase": phase, "preserved": preserved,
    } for index, (name, result, phase, preserved, _) in enumerate(CASES, 1))


def work_totals(row):
    record_fields(row, WORK_FIELDS)
    if (any(type(row[key]) is not int for key in WORK_FIELDS if key != "counts")
        or type(row["counts"]) is not list or any(type(values) is not list for values in row["counts"])):
        raise ValueError("VM01 native work fields have an invalid type")
    snapshot = Snapshot(
        row["producer"], row["case"], row["owner_mask"], tuple(tuple(values) for values in row["counts"]),
        row["version"], row["reserved0"], row["reserved1"],
    )
    return compose_snapshots((Capture(Status.OK, snapshot),), capture_label=row["case"],
                             expected_producers={PRODUCER: OWNER_MASK})


def parse_native_output(output, channel):
    if channel not in ("ordinary", "work") or type(output) is not str or not output.endswith("\n"):
        raise ValueError("VM01 native transcript is incomplete or has an unknown channel")
    records, work = [], []
    lines = output.splitlines()
    for line in lines[:-1]:
        if line.startswith("MICROS_VM01_CASE "):
            if channel == "work" and len(work) != len(records):
                raise ValueError("VM01 case omitted its work snapshot")
            row = json.loads(line.removeprefix("MICROS_VM01_CASE "), object_pairs_hook=json_pairs)
            record_fields(row, CASE_FIELDS)
            if (any(type(row[key]) is not int or not 0 <= row[key] < 1 << 64 for key in CASE_FIELDS if key != "name")
                or type(row["name"]) is not str or re.fullmatch(r"[a-z-]+", row["name"]) is None
                or row["case"] != len(records) + 1):
                raise ValueError("VM01 native case has malformed fields or order")
            records.append(row)
        elif line.startswith("MICROS_VM01_WORK "):
            row = json.loads(line.removeprefix("MICROS_VM01_WORK "), object_pairs_hook=json_pairs)
            work_totals(row)
            if channel != "work" or row["case"] != len(records) or len(work) + 1 != len(records):
                raise ValueError("VM01 work belongs to another or duplicate case")
            work.append(row)
        else:
            raise ValueError("VM01 native transcript contains an unknown record")
    identities = tuple({key: value for key, value in row.items()
                        if key not in ("before_state", "after_state")} for row in records)
    if (identities != expected_cases() or lines[-1:] != ["MICROS_VM01_NATIVE_PASS cases=" + str(len(records))]
        or channel == "work" and len(work) != len(records)):
        raise ValueError("VM01 native evidence omits or changes an independent case")
    return tuple(records), tuple(work)


def work_failures(records, work):
    if len(records) != len(CASES) or len(work) != len(CASES):
        raise ValueError("VM01 requires its complete independent case set")
    failures = []
    for row, capture, spec in zip(records, work, CASES, strict=True):
        if row["name"] != spec[0] or row["case"] != capture["case"]:
            raise ValueError("VM01 native work borrowed another case")
        totals = work_totals(capture)
        audits = totals.per_kind[Kind.COMPLETE_VALIDATOR_CALL]
        if audits != spec[4]:
            failures.append((row["name"], "complete-audits", audits, spec[4]))
        if row["name"] in ("kernel-owner-unvisited", "kernel-scanned-count"):
            visits = totals.per_owner[Owner.KERNEL_VM][Kind.RECORD_VISIT]
            # Two local checks of the empty loading record; no other slot loop.
            if visits > 2 * 16:
                failures.append((row["name"], "selected-record-visits", visits, 2 * 16))
        if row["name"] in POINT_CASES:
            # Sixteen bounded request passes, local record checks and active geometry;
            # no managed-frame, inactive-input or whole-record-table term is permitted.
            bound = 16 * 4096 + 64 * 20 + 4 * 96 * 20
            for kind in (Kind.RECORD_VISIT, Kind.COMPARISON, Kind.INDEX_PROBE, Kind.RANGE_VISIT):
                if totals.per_kind[kind] > bound:
                    failures.append((row["name"], kind.name.lower(), totals.per_kind[kind], bound))
        if row["name"] in BOOT_MAPPING_EXTENTS:
            # Complete transport/tail work remains; uniqueness uses fixed-width keys.
            count = BOOT_MAPPING_EXTENTS[row["name"]]
            bound = 2 * 364704 + 6 * 32 * count + 4 * count + 16 * 96
            for kind in (Kind.RECORD_VISIT, Kind.COMPARISON, Kind.INDEX_PROBE):
                if totals.per_kind[kind] > bound:
                    failures.append((row["name"], kind.name.lower(), totals.per_kind[kind], bound))
        if row["name"] in GEOMETRY_EXTENTS:
            visits = totals.per_kind[Kind.RANGE_VISIT]
            bound = GEOMETRY_EXTENTS[row["name"]].bit_length()
            if not 1 <= visits <= bound:
                failures.append((row["name"], "range_visits", visits, (1, bound)))
        if row["name"] in GLOBAL_RECORD_EXTENTS:
            frames = GLOBAL_RECORD_EXTENTS[row["name"]]
            keys = frames + 2 * 64
            bound = 6 * 64 * keys + 4 * keys + 32 * frames + 4 * 4096
            for kind in (Kind.RECORD_VISIT, Kind.INDEX_PROBE):
                if totals.per_kind[kind] > bound:
                    failures.append((row["name"], kind.name.lower(), totals.per_kind[kind], bound))
        if row["name"] in MEMBERSHIP_EXTENTS:
            keys = MEMBERSHIP_EXTENTS[row["name"]]
            bound = 6 * 64 * keys + 4 * keys + 8 * (keys + 20) * keys.bit_length() + 4 * 64
            for kind in (Kind.RECORD_VISIT, Kind.INDEX_PROBE):
                if totals.per_kind[kind] > bound:
                    failures.append((row["name"], kind.name.lower(), totals.per_kind[kind], bound))
    return tuple(failures)


def check_native_products(root, executables):
    from tools.generate_selected_vm_fixture import source_bundle

    root = Path(root).resolve()
    build = root / "build/host-debug"
    expected = tuple(str(build / name) for name in TARGETS)
    if tuple(str(Path(path).absolute()) for path in executables) != expected:
        raise ValueError("VM01 native products have foreign ownership")
    products = {path: read_regular(path)[1] for path in expected}
    nm = native_symbols_tool()
    inputs, measured = {}, {}
    for channel, path in zip(("ordinary", "work"), expected, strict=True):
        for relative, raw in source_bundle(root, channel).items():
            generated = build / "selected-vm" / channel / relative
            actual, identity = read_regular(generated)
            if actual != raw or identity["mode"] != 0o644:
                raise ValueError("VM01 compiled input is stale or foreign")
            inputs[str(generated)] = identity
        symbols = subprocess.check_output((nm["path"], "-a", path), cwd=root, text=True, timeout=30)
        wanted = frozenset() if channel == "ordinary" else frozenset((
            "micros_test_work_begin", "micros_test_work_finish", "micros_test_work_record",
        ))
        if observation_symbols(symbols) != wanted:
            raise ValueError("VM01 native product has the wrong collector linkage")
        completed = subprocess.run((path, "--run", channel), cwd=root, stdin=subprocess.DEVNULL,
                                   capture_output=True, text=True, timeout=60)
        if completed.returncode or completed.stderr:
            raise ValueError("VM01 native product failed:\n" + completed.stdout + completed.stderr)
        measured[channel] = parse_native_output(completed.stdout, channel)
        print(f"MICROS_VM01_NATIVE_ACCEPTED channel={channel} cases={len(measured[channel][0])}", flush=True)
    if ({path: read_regular(path)[1] for path in expected} != products
        or {path: read_regular(path)[1] for path in inputs} != inputs or native_symbols_tool() != nm):
        raise ValueError("VM01 native products, inputs or tools changed during execution")
    if measured["ordinary"][0] != measured["work"][0] or measured["ordinary"][1]:
        raise ValueError("VM01 passive observations changed semantics")
    failures = work_failures(*measured["work"])
    if failures:
        raise ValueError("VM01 independent work bounds failed: " + repr(failures))
    return measured

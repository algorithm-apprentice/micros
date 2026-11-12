"""MEM04 actual native and target semantic capture."""

import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

from tests.host.selected_address_space_source import (
    CHANNELS, OWNER_MASK, RECIPE_ADMISSION, STATE_WORK_CONTRACT,
)
from tests.host.work_count_evidence import Capture, Kind, Owner, Snapshot, Stage, Status, compose_snapshots
from tools.generate_selected_address_space_fixture import source_bundle
from tools.stack_artifact_files import read_regular
from tools.validation_inputs import _file_identity


FIXTURE_PATHS = (
    "tests/host/selected_address_space_test.c",
    "tests/host/selected_address_space_test.h",
    "tests/host/selected_address_space_source.py",
    "tests/host/selected_address_space_evidence.py",
    "tests/host/native_observations.py",
    "tests/host/native_observations.json",
    "tests/host/source_snapshot.py",
    "tests/host/selected_release_handoff_source.py",
    "tests/host/c_source_records.py",
    "tests/host/work_count_lexical.py",
    "tests/host/work_count_projection.py",
    "tests/host/work_count_types.py",
    "tests/host/work_count_snapshots.py",
    "tests/host/test_selected_address_space.py",
    "tools/check_selected_address_space.py",
    "tools/generate_selected_address_space_fixture.py",
    "include/micros/work_observation.h",
    "tests/work/work_counts.h",
    "tests/work/work_counts.c",
)

CURSOR_FIELDS = frozenset((
    "version", "observed", "before", "after", "other_state_unchanged",
    "output_unchanged", "allocator_unchanged",
))
SCOPED_FIELDS = frozenset((
    "version", "phase", "diagnostic", "lookup", "physical", "mapped", "permissions",
    "absent", "absent_outputs_unchanged", "activation", "before_satp", "after_satp",
    "expected_satp", "restored_satp", "state_unchanged",
))
QUERY_FIELDS = frozenset((
    "case", "scenario", "operation", "phase", "frames", "ranges", "density", "address", "error",
    "result_matches", "permissions", "contiguous", "outputs_unchanged", "state_unchanged",
    "state_matches", "irq_unchanged", "root_unchanged", "root_matches",
    "allocation_limit", "active", "satp_writes",
))
PRODUCER_BASE = 0x4D454D3034000000


def parse_semantics(text):
    rows = []
    for line in text.splitlines():
        parts = line.split(" ")
        if not parts or parts[0] not in ("MEM04_CURSOR", "MEM04_SCOPED"):
            raise ValueError("MEM04 native capture contains an unknown record")
        values = {}
        for field in parts[1:]:
            match = re.fullmatch(r"([a-z_]+)=(0|[1-9][0-9]*)", field)
            if match is None or match[1] in values:
                raise ValueError("MEM04 native capture has an ambiguous field")
            values[match[1]] = int(match[2])
            if values[match[1]] >= 1 << 64:
                raise ValueError("MEM04 native capture value exceeds its width")
        expected = CURSOR_FIELDS if parts[0] == "MEM04_CURSOR" else SCOPED_FIELDS
        if set(values) != expected or values["version"] != 1:
            raise ValueError("MEM04 native capture omitted or changed its schema")
        if any(value not in (0, 1) for key, value in values.items() if key.endswith("_unchanged")):
            raise ValueError("MEM04 native capture has an invalid comparison result")
        rows.append((parts[0], values))
    if (
        len(rows) != 3 or rows[0][0] != "MEM04_CURSOR"
        or [row[0] for row in rows[1:]] != ["MEM04_SCOPED", "MEM04_SCOPED"]
        or [row[1]["phase"] for row in rows[1:]] != [1, 2]
    ):
        raise ValueError("MEM04 native semantic coverage/order is incomplete")
    return tuple(rows)


def semantic_results(rows):
    cursor = rows[0][1]
    scoped = tuple(row[1] for row in rows[1:])
    selected = (
        cursor["observed"] == 8 and cursor["before"] == cursor["after"] == 64
        and cursor["other_state_unchanged"] == cursor["output_unchanged"] == cursor["allocator_unchanged"] == 1
        and all(row["lookup"] == 0 and row["physical"] == row["mapped"] and row["permissions"] == 3
                and row["state_unchanged"] == 1 for row in scoped)
    )
    activation = all(
        row["activation"] == 0 and row["after_satp"] == row["expected_satp"]
        and row["restored_satp"] == row["before_satp"] and row["state_unchanged"] == 1
        for row in scoped
    )
    diagnostic = all(
        row["diagnostic"] == 12 and row["absent"] == 10
        and row["absent_outputs_unchanged"] == row["state_unchanged"] == 1
        for row in scoped
    )
    return {"selected": selected, "activation": activation, "scoped-diagnostic": diagnostic}


def expected_queries(stage=Stage.RED):
    cases = []
    for phase in (1, 2):
        for frames, ranges in ((512, 1), (4096, 1), (262144, 1), (512, 16)):
            for density in (1, 32):
                for operation, address in (
                    (0, 0x40000000), (1, 0x40000011), (1, 0x7FFFFFFF),
                    (0, 0x40200000), (2, 0x40000000), (3, 0x40000000),
                ):
                    cases.append({
                        "case": len(cases) + 1, "scenario": 0, "operation": operation, "phase": phase,
                        "frames": frames, "ranges": ranges, "density": density, "address": address,
                    })
    for phase in (1, 2):
        for depth, address in ((1, 0x40000000), (2, 0x40200000), (3, 0x40001000)):
            for operation, offset in ((0, 0), (1, 17)):
                cases.append({
                    "case": len(cases) + 1, "scenario": depth, "operation": operation, "phase": phase,
                    "frames": 512, "ranges": 1, "density": 0, "address": address + offset,
                })
    for phase in (1, 2):
        for depth in (0, 1, 2):
            for operation in (0, 2):
                cases.append({
                    "case": len(cases) + 1, "scenario": 10 + depth, "operation": operation,
                    "phase": phase, "frames": 512, "ranges": 1, "density": 0, "address": 0x40000000,
                })
    for scenario in range(20, 30):
        for operation, address in (
            (0, 0x40000000), (0, 0x7FFFE000), (1, 0x7FFFE011),
            (2, 0x40000000), (3, 0x40000000),
        ):
            cases.append({
                "case": len(cases) + 1, "scenario": scenario, "operation": operation,
                "phase": 1 if scenario == 20 else 2, "frames": 512, "ranges": 1,
                "density": 0, "address": address,
            })
    for scenario in range(40, 53):
        operations = (
            ((0, 0x7FFFE000), (1, 0x7FFFE011), (2, 0x7FFFE000), (3, 0x7FFFE000))
            if scenario == 52 else
            ((0, 0x40000000), (0, 0x7FFFE000), (1, 0x7FFFE011),
             (2, 0x40000000), (3, 0x40000000))
        )
        for operation, address in operations:
            cases.append({
                "case": len(cases) + 1, "scenario": scenario, "operation": operation,
                "phase": 1 if scenario == 40 else 2, "frames": 512, "ranges": 1,
                "density": 0, "address": address,
            })
    for shape, address in enumerate((0x40000000, 0x40200000, 0x40001000)):
        for limit in range(4 - shape):
            cases.append({
                "case": len(cases) + 1, "scenario": 70 + shape, "operation": 4,
                "phase": 1, "frames": 512, "ranges": 1, "density": 0,
                "address": address, "allocation_limit": limit,
            })
    for phase in (1, 2):
        for scenario in range(80, 112):
            for operation in (0, 2, 3):
                cases.append({
                    "case": len(cases) + 1, "scenario": scenario, "operation": operation,
                    "phase": phase, "frames": 512, "ranges": 1, "density": 0, "address": 0x40000000,
                })
    for scenario in range(130, 140):
        cases.append({
            "case": len(cases) + 1, "scenario": scenario, "operation": 2 if scenario == 139 else 5,
            "phase": 2 if scenario in (136, 137) else 1, "frames": 512, "ranges": 1,
            "density": 0, "address": 0x40000000, "active": int(scenario >= 138),
        })
    for scenario in range(140, 156):
        address = (
            0 if scenario in (144, 145) else 0x40000001 if scenario == 146
            else 0x40002000 if scenario == 153 else 0x40001000 if scenario >= 154 else 0x40000000
        )
        for operation in ((4,) if scenario >= 154 else (0, 1, 4, 2)):
            cases.append({
                "case": len(cases) + 1, "scenario": scenario, "operation": operation,
                "phase": 3 if scenario in (140, 142) else 2 if scenario == 155 else 1,
                "frames": 512, "ranges": 1, "density": 0, "address": address,
                "active": int(scenario in (149, 151, 152)),
            })
    for scenario, phase in ((30, 1), (31, 2), (53, 1), (54, 2), (32, 2)):
        for operation, address in ((0, 0x7FFFE000), (1, 0x7FFFE011), (2, 0x7FFFE000), (3, 0x7FFFE000)):
            cases.append({
                "case": len(cases) + 1, "scenario": scenario, "operation": operation,
                "phase": phase, "frames": 512, "ranges": 1, "density": 0, "address": address,
            })
    if stage is Stage.GREEN:
        for scenario in range(120, 126):
            cases.append({
                "case": len(cases) + 1, "scenario": scenario, "operation": 6,
                "phase": 1, "frames": 512, "ranges": 1, "density": 0, "address": 0x40000000,
            })
    for case in cases:
        case.setdefault("allocation_limit", (1 << 64) - 1)
        case.setdefault("active", 0)
    return tuple(cases)


def _json_pairs(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("MEM04 work record contains a duplicate field")
        result[key] = value
    return result


def parse_bindings(lines, stage):
    expected_cases = (
        tuple((0, index) for index in range(16)) + tuple((1, index) for index in range(12))
        + ((2, 0), (3, 0))
    )
    if stage is Stage.RED:
        expected_cases = ()
    if len(lines) != len(expected_cases):
        raise ValueError("MEM04 current binding-interface coverage is incomplete")
    rows = []
    for line, (controller, case) in zip(lines, expected_cases, strict=True):
        if not line.startswith("MEM04_BINDING "):
            raise ValueError("MEM04 binding observation is missing")
        matches = [re.fullmatch(r"([a-z_]+)=(0|[1-9][0-9]*)", item)
                   for item in line.removeprefix("MEM04_BINDING ").split(" ")]
        if any(match is None for match in matches):
            raise ValueError("MEM04 binding field is malformed")
        row = _json_pairs((match[1], int(match[2])) for match in matches)
        if set(row) != {
            "controller", "case", "primary", "runtime",
            "output_matches", "output_unchanged", "state_unchanged",
        } or row["controller"] != controller or row["case"] != case:
            raise ValueError("MEM04 binding inputs differ from the frozen case")
        if any(value >= 1 << 64 for value in row.values()) or any(
            row[key] not in (0, 1) for key in ("output_matches", "output_unchanged", "state_unchanged")
        ):
            raise ValueError("MEM04 binding result is outside its schema")
        rows.append(row)
    return tuple(rows)


def binding_failures(rows):
    failures = []
    for row in rows:
        controller, case = row["controller"], row["case"]
        if controller == 0:
            primary = 0 if case < 6 else 7
            runtime = 2 if case == 4 else 1 if case < 6 else 0
        elif controller == 1:
            primary = runtime = 0 if case < 3 else 1 if case < 6 else 2
        elif controller == 2:
            primary, runtime = 1, 0
        else:
            primary = runtime = 0
        expected = {
            "primary": primary, "runtime": runtime, "output_matches": 1,
            "output_unchanged": int(case >= 6 or controller == 2), "state_unchanged": 1,
        }
        for key, value in expected.items():
            if row[key] != value:
                failures.append((controller, case, key, value, row[key]))
    return tuple(failures)


def parse_native_output(text, stage, channel):
    lines = text.splitlines()
    semantics = parse_semantics("\n".join(lines[:3]))
    step = 1 if channel == "ordinary" else 2
    cases = expected_queries(stage)
    query_end = 3 + step * len(cases)
    if len(lines) != query_end + (30 if stage is Stage.GREEN else 0):
        raise ValueError("MEM04 native case/work coverage is incomplete")
    queries, observations = [], []
    for offset, expected in enumerate(cases):
        line = lines[3 + step * offset]
        if not line.startswith("MEM04_QUERY "):
            raise ValueError("MEM04 query record is missing")
        fields = line.removeprefix("MEM04_QUERY ").split(" ")
        pairs = [re.fullmatch(r"([a-z_]+)=(0|[1-9][0-9]*)", item) for item in fields]
        if any(match is None for match in pairs):
            raise ValueError("MEM04 query field is malformed")
        values = _json_pairs((match[1], int(match[2])) for match in pairs)
        if set(values) != QUERY_FIELDS or any(values[key] != value for key, value in expected.items()):
            raise ValueError(
                f"MEM04 query input/identity differs at case {expected['case']}: "
                f"expected {expected}; observed {values}"
            )
        if any(value >= 1 << 64 for value in values.values()) or any(
            value not in (0, 1) for key, value in values.items()
            if key.endswith(("_matches", "_unchanged"))
        ):
            raise ValueError("MEM04 query result has an invalid width or comparison")
        queries.append(values)
        if channel != "ordinary":
            line = lines[4 + step * offset]
            if not line.startswith("MEM04_WORK "):
                raise ValueError("MEM04 native work snapshot is missing")
            value = json.loads(line.removeprefix("MEM04_WORK "), object_pairs_hook=_json_pairs)
            if set(value) != {
                "version", "reserved0", "producer_label", "capture_label",
                "owner_mask", "reserved1", "counts",
            }:
                raise ValueError("MEM04 native work schema changed")
            value["counts"] = tuple(tuple(row) for row in value["counts"])
            snapshot = Snapshot(**value)
            producer = PRODUCER_BASE + (256 if stage is Stage.GREEN else 0) + CHANNELS.index(channel)
            compose_snapshots(
                (Capture(Status.OK, snapshot),), capture_label=expected["case"],
                expected_producers={producer: OWNER_MASK},
            )
            observations.append(value)
    return {
        "semantics": semantics, "queries": tuple(queries), "observations": tuple(observations),
        "bindings": parse_bindings(lines[query_end:], stage),
    }


def _baseline_query_error(row, desired):
    scenario = row["scenario"]
    if scenario in (10, 11, 12, 24, 25, 26, 45, 46, 47):
        return 11
    if scenario in (27, 28, 29, 48, 49, 50, 51):
        return 14
    if 80 <= scenario <= 111:
        if scenario == 110:
            return 0
        if scenario == 102:
            return 14
        return 12 if 80 <= scenario <= 93 or scenario in (101, 111) else 11
    if scenario == 134:
        return 9
    if scenario in (151, 152, 153):
        return 12
    return desired


def query_failures(queries, *, baseline=False):
    failures = []
    for row in queries:
        operation = row["operation"]
        scenario = row["scenario"]
        missing = row["address"] == 0x40200000 or scenario in (1, 2, 3)
        poison = scenario in (10, 11, 12) and (scenario != 12 or operation != 2)
        error = 11 if poison else 10 if missing else 0
        if 20 <= scenario <= 29:
            if scenario in (27, 29) or operation == 3 and scenario == 28:
                error = 14
            elif operation in (2, 3):
                error = 11 if scenario in (24, 25, 26) else 0
            elif row["address"] >= 0x7FFFE000:
                error = 10 if scenario in (20, 21, 22) else 11
            else:
                error = 0
        if 40 <= scenario <= 52:
            if scenario in (48, 49, 51) or operation == 3 and scenario == 50:
                error = 14
            elif operation in (2, 3):
                error = 11 if scenario in (45, 46, 47) else 0
            elif scenario == 52:
                error = 0
            elif row["address"] >= 0x7FFFE000:
                error = 10 if scenario in (40, 41) else 11
            else:
                error = 0
        if scenario in (70, 71, 72):
            error = 0 if row["allocation_limit"] == 73 - scenario else 8
        if 80 <= scenario <= 111:
            diagnostic = 12 if 80 <= scenario <= 93 or scenario in (101, 111) else 11
            diagnostic = 14 if scenario == 102 else 0 if scenario == 110 else diagnostic
            if operation == 3:
                error = diagnostic
            elif operation == 2:
                error = (
                    12 if scenario in (80, 83) else 14 if scenario in (102, 110)
                    else 11 if scenario in (94, 103, 104, 106, 108) else 0
                )
            else:
                error = 0 if scenario in (100, 101, 102, 110) else diagnostic
        if 130 <= scenario <= 139:
            error = 9 if scenario in (131, 132, 133, 135) else 0
        if 140 <= scenario <= 155:
            error = {
                140: 3 if operation == 2 else 1,
                141: 2, 142: 3, 143: 4, 144: 6 if operation == 4 else 5,
                145: 0 if operation == 2 else 6,
                146: 6 if operation in (0, 4) else 0,
                147: 13, 148: 7 if operation == 4 else 5,
                149: 5 if operation == 4 else 0,
                150: 9 if operation == 4 else 0,
                151: 0 if operation == 2 else 12,
                152: 5 if operation == 4 else 0,
                153: 10 if operation in (0, 1) else 0,
                154: 8, 155: 3,
            }[scenario]
        if 120 <= scenario <= 125:
            error = {120: 0, 121: 1, 122: 14, 123: 4, 124: 13, 125: 5}[scenario]
        if baseline:
            error = _baseline_query_error(row, error)
        read = operation in (0, 1) and error == 0
        activated = operation == 2 and error == 0
        allocated = operation == 4 and error == 0
        prepared = operation in (5, 6) and error == 0
        expected = {
            "error": error,
            "result_matches": int(read or allocated or prepared or operation in (2, 3)),
            "permissions": 3 if read else (1 << 32) - 1,
            "contiguous": 4096 - row["address"] % 4096 if operation == 1 and read else (1 << 64) - 1,
            "outputs_unchanged": int(not read and not allocated and not prepared),
            "state_unchanged": int(not allocated and not (operation == 5 and prepared)),
            "state_matches": 1, "irq_unchanged": 1,
            "root_unchanged": int(not activated or row["active"]),
            "root_matches": int(activated or row["active"]), "satp_writes": int(activated),
        }
        if baseline and scenario in (70, 71, 72) and 0 < row["allocation_limit"] < 73 - scenario:
            expected["state_unchanged"] = expected["state_matches"] = 0
        for key, value in expected.items():
            if row[key] != value:
                failures.append((row["case"], key, value, row[key]))
    return tuple(failures)


def native_symbols_tool():
    selected = shutil.which("nm")
    if selected is None:
        raise ValueError("MEM04 ordinary observation absence requires the existing native nm")
    if sys.platform == "darwin":
        selected = subprocess.check_output(("/usr/bin/xcrun", "--find", "nm"), text=True, timeout=30).strip()
    selected = Path(selected).resolve(strict=True)
    return {"path": str(selected), "identity": _file_identity(selected)}


def _normalized_semantics(rows):
    result = []
    for name, value in rows:
        row = dict(value)
        if name == "MEM04_SCOPED":
            before = row["before_satp"]
            if row["lookup"] == 0:
                row["physical"] -= row["mapped"]
            row["mapped"] = (row["mapped"] >> 12) - (before & ((1 << 44) - 1))
            for key in ("after_satp", "expected_satp", "restored_satp"):
                row[key] -= before
            row["before_satp"] = before >> 44
        result.append((name, row))
    return tuple(result)


def _validate_composition(parsed):
    ordinary = parsed["ordinary"]
    for channel in CHANNELS[:-1]:
        rows = parsed[channel]
        if (
            rows["queries"] != ordinary["queries"] or rows["bindings"] != ordinary["bindings"]
            or _normalized_semantics(rows["semantics"]) != _normalized_semantics(ordinary["semantics"])
        ):
            raise ValueError("MEM04 observations changed actual operation results/state")
    for index, row in enumerate(ordinary["queries"]):
        captures, expected = [], {}
        for channel in ("path", "context"):
            value = parsed[channel]["observations"][index]
            snapshot = Snapshot(**value)
            captures.append(Capture(Status.OK, snapshot))
            expected[snapshot.producer_label] = OWNER_MASK
        composed = compose_snapshots(tuple(captures), capture_label=row["case"], expected_producers=expected)
        aggregate = parsed["aggregate"]["observations"][index]["counts"]
        if composed.per_owner != aggregate:
            raise ValueError("MEM04 path/context channels do not exactly compose their aggregate")


def _slot_count(row):
    scenario = row["scenario"]
    return int(
        scenario in (*range(20, 32), *range(40, 52), 53, 54, 136, 137)
    )


def _work_failures(measured):
    failures = {"selected": [], "root": []}
    pairs = {}
    for index, row in enumerate(measured["ordinary"]["queries"]):
        operation = row["operation"]
        if operation == 3:
            continue
        category = "root" if operation in (2, 5, 6) else "selected"
        path = measured["path"]["observations"][index]["counts"]
        context = measured["context"]["observations"][index]["counts"]
        aggregate = measured["aggregate"]["observations"][index]["counts"]
        root = operation in (2, 5, 6)
        slots = _slot_count(row)
        proofs = 3 + 4 * slots if root else 32 if operation == 4 else 4
        g, k = row["ranges"], STATE_WORK_CONTRACT["owner_kinds"]

        def bound(label, observed, maximum):
            if observed > maximum:
                failures[category].append((row["case"], label, observed, maximum))

        bound("complete-audits", sum(owner[Kind.COMPLETE_VALIDATOR_CALL] for owner in aggregate), 0)
        bound("scratch-stores", context[Owner.KERNEL_MEMORY][Kind.CLEAR_UNIT], 0)
        bound("pte-reads", path[Owner.KERNEL_MEMORY][Kind.PTE_VISIT], 1024 + 3 * slots if root else 3)
        bound("selected-owner-reads", path[Owner.KERNEL_MEMORY][Kind.RECORD_VISIT], proofs)
        bound("VM-binding-inspections", context[Owner.KERNEL_VM][Kind.RECORD_VISIT], 1)
        bound("TTY-binding-inspections", context[Owner.KERNEL_TTY][Kind.RECORD_VISIT], 1)
        bound("VM-header-work", context[Owner.KERNEL_VM][Kind.INDEX_PROBE], STATE_WORK_CONTRACT["vm_header_work"])
        bound("TTY-header-work", context[Owner.KERNEL_TTY][Kind.INDEX_PROBE], STATE_WORK_CONTRACT["tty_header_work"])
        bound("active-geometry", context[Owner.KERNEL_MEMORY][Kind.RANGE_VISIT], (proofs + 1) * (8 * g + 2 * k))
        allowance = 512 + 3 * slots if root else 3
        if operation == 4:
            allowance += 6 * 512 + row["frames"] // 64 + 16
        bound("memory-index-work", context[Owner.KERNEL_MEMORY][Kind.INDEX_PROBE],
              (proofs + 1) * (64 * g + 32 * k) + allowance)
        bound("other-capacity-work", sum(
            context[owner][Kind.INDEX_PROBE] for owner in
            (Owner.KERNEL_OBJECTS, Owner.KERNEL_SCHEDULER, Owner.KERNEL_ENDPOINT, Owner.KERNEL_BOOT)
        ), 0)
        bound("PTE-writes", path[Owner.KERNEL_MEMORY][Kind.COPY_UNIT], 3 if operation == 4 else 0)
        if row["scenario"] in (1, 2, 3):
            actual = path[Owner.KERNEL_MEMORY][Kind.PTE_VISIT]
            if actual != row["scenario"]:
                failures[category].append((row["case"], "exact-absence-prefix", actual, row["scenario"]))
        if row["scenario"] == 0 and operation in (0, 1) and row["address"] != 0x40200000:
            actual = path[Owner.KERNEL_MEMORY][Kind.PTE_VISIT]
            if actual != 3:
                failures[category].append((row["case"], "exact-present-path", actual, 3))
        if row["scenario"] == 0 and row["ranges"] == 1:
            key = row["phase"], operation, row["address"]
            if key in pairs and pairs[key] != aggregate:
                failures[category].append((row["case"], "independent-frame-and-leaf-scaling", 1, 0))
            pairs.setdefault(key, aggregate)
    return {key: tuple(value) for key, value in failures.items()}


TARGET_FIELDS = {
    "MEM04_TABLE": frozenset((
        "case", "lookup", "activation", "before_satp", "after_satp",
        "poison_unchanged", "state_unchanged", "outputs_unchanged",
    )),
    "MEM04_ACTIVATION": frozenset((
        "case", "diagnostic", "activation", "release", "destroy", "before_satp",
        "after_satp", "process_satp", "restored_satp", "state_unchanged", "output_unchanged",
    )),
    "MEM04_HANDOFF": frozenset((
        "diagnostic", "translated", "before_satp", "after_satp",
        "poison_unchanged", "state_unchanged", "outputs_unchanged",
    )),
}


def parse_target_output(output, workflow, stage, exit_code):
    if workflow not in STATE_WORK_CONTRACT["target_workflows"] or stage not in (Stage.RED, Stage.GREEN):
        raise ValueError("MEM04 target capture has an unknown workflow or stage")
    rows = []
    for line in output.splitlines():
        if not line.startswith("# MEM04_"):
            continue
        match = re.fullmatch(r"# (MEM04_TABLE|MEM04_ACTIVATION|MEM04_HANDOFF) version=1 (.*)", line)
        if match is None:
            raise ValueError("MEM04 target emitted an unknown semantic witness")
        pairs = [re.fullmatch(r"([a-z_]+)=(0x[0-9a-f]{16})", field) for field in match[2].split(" ")]
        if any(pair is None for pair in pairs):
            raise ValueError("MEM04 target witness has a malformed field")
        value = _json_pairs((pair[1], int(pair[2], 16)) for pair in pairs)
        if set(value) != TARGET_FIELDS[match[1]] or any(
            item not in (0, 1) for key, item in value.items() if key.endswith("_unchanged")
        ):
            raise ValueError("MEM04 target witness changed its schema")
        rows.append((match[1], value))
    user = workflow == "test-qemu-user-address-space"
    expected = (
        (("MEM04_TABLE", 0), ("MEM04_TABLE", 1))
        + tuple(("MEM04_ACTIVATION", index) for index in range(1 if stage is Stage.RED else 5))
        if user else (("MEM04_HANDOFF", None),)
    )
    if tuple((name, value.get("case")) for name, value in rows) != expected:
        raise ValueError("MEM04 target did not execute its exact semantic witness coverage")
    reason = "user-address-space-test" if user else "address-space-handoff-test"
    panics = re.findall(r"^# MICROS_PANIC reason=(.*)$", output, re.M)
    if stage is Stage.RED:
        if type(exit_code) is not int or exit_code not in (1, 2) or panics != [reason]:
            raise ValueError("MEM04 target Red lacks its witnessed component failure")
    else:
        marker = "MICROS_USER_ADDRESS_SPACE_TEST_PASS" if user else "MICROS_ADDRESS_SPACE_HANDOFF_TEST_PASS"
        if exit_code != 0 or panics or len(re.findall(r"^# " + marker + r" .+$", output, re.M)) != 1:
            raise ValueError("MEM04 current target lacks complete original acceptance")
        if not user and len(re.findall(r"^# MICROS_GRANT_SYSCALL_HANDOFF_PASS .+$", output, re.M)) != 1:
            raise ValueError("MEM04 handed-off target omitted its original grant boundary")
    return tuple(rows)


def target_failures(rows, *, baseline=False):
    failures = []
    for name, row in rows:
        expected = {key: 1 for key in row if key.endswith("_unchanged")}
        before = row["before_satp"]
        if before >> 44 != 8 << 16:
            failures.append((name, row.get("case"), "kernel-satp-mode", before, 8 << 60))
        if name == "MEM04_TABLE":
            expected.update(lookup=11, activation=11, after_satp=before)
        elif name == "MEM04_ACTIVATION":
            diagnostic = 12 if row["case"] == 2 else 11
            expected.update(
                diagnostic=diagnostic, release=diagnostic, destroy=diagnostic,
                activation=diagnostic if baseline else 0,
                after_satp=before if baseline else row["process_satp"], restored_satp=before,
            )
            if row["process_satp"] == before or row["process_satp"] >> 44 != before >> 44:
                failures.append((name, row["case"], "distinct-selected-root", row["process_satp"], before))
        else:
            expected.update(diagnostic=12, translated=12 if baseline else 10, after_satp=before)
        for key, value in expected.items():
            if row[key] != value:
                failures.append((name, row.get("case"), key, value, row[key]))
    return tuple(failures)


def check_native_products(root, executable_paths):
    from tests.host.selected_release_handoff_source import measure_audit_boundaries
    from tests.host.native_observations import validate_source

    root = Path(root).resolve()
    expected = tuple(root / "build/host-debug" / name for name in RECIPE_ADMISSION["native_targets"])
    if tuple(map(Path, executable_paths)) != expected:
        raise ValueError("MEM04 CTest requires its four exact declared build products")
    identities = {str(path): read_regular(path)[1] for path in expected}
    fixtures = {path: read_regular(root / path)[1] for path in FIXTURE_PATHS}
    dependencies = {}
    for path in ("kernel/memory/user_address_space_handoff.c",):
        data, identity = read_regular(root / path)
        validate_source(root, path, data.decode())
        dependencies[path] = identity
    nm = native_symbols_tool()
    measured, generated, inputs = {}, {}, {}
    for channel, executable in zip(CHANNELS, expected, strict=True):
        source_inputs, outputs = source_bundle(root, Stage.GREEN, channel)
        directory = root / RECIPE_ADMISSION["generator_output"] / channel
        emitted = {
            path: {"kind": "file", "mode": mode, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
            for path, (data, mode) in outputs.items()
        }
        if {path: read_regular(directory / path)[1] for path in emitted} != emitted:
            raise ValueError("MEM04 CTest generated inputs differ from actual current source")
        generated[channel], inputs[channel] = emitted, source_inputs
        symbols = subprocess.check_output((nm["path"], "-a", str(executable)), cwd=root, text=True, timeout=30)
        observed_symbols = tuple(sorted({
            line.split()[-1].lstrip("_") for line in symbols.splitlines()
            if line.split() and "micros_test_work_" in line.split()[-1]
        }))
        wanted = () if channel == "ordinary" else ("micros_test_work_begin", "micros_test_work_finish", "micros_test_work_record")
        if observed_symbols != wanted:
            raise ValueError("MEM04 CTest product has the wrong collector linkage")
        completed = subprocess.run(
            (str(executable), "green", channel), cwd=root, stdin=subprocess.DEVNULL,
            capture_output=True, text=True, timeout=60,
        )
        if completed.returncode or completed.stderr:
            raise ValueError("MEM04 CTest product failed its semantic capture:\n" + completed.stdout + completed.stderr)
        measured[channel] = parse_native_output(completed.stdout, Stage.GREEN, channel)
    _validate_composition(measured)
    failures = _work_failures(measured)
    ordinary = measured["ordinary"]
    measure_audit_boundaries(root, Stage.GREEN)
    semantics = semantic_results(ordinary["semantics"])
    queries = query_failures(ordinary["queries"])
    bindings = binding_failures(ordinary["bindings"])
    if (
        any(failures.values()) or not all(semantics.values()) or queries or bindings
    ):
        raise ValueError("MEM04 CTest failed selected semantics, work or diagnostic boundaries: " + json.dumps({
            "work": failures, "semantics": semantics, "queries": queries, "bindings": bindings,
        }))
    if (
        {path: read_regular(root / path)[1] for path in FIXTURE_PATHS} != fixtures or native_symbols_tool() != nm
        or {path: read_regular(root / path)[1] for path in dependencies} != dependencies
        or any(read_regular(path)[1] != identities[str(path)] for path in expected)
    ):
        raise ValueError("MEM04 CTest inputs/products changed during execution")
    for channel in CHANNELS:
        directory = root / RECIPE_ADMISSION["generator_output"] / channel
        if {path: read_regular(root / path)[1] for path in inputs[channel]} != inputs[channel] or {
            path: read_regular(directory / path)[1] for path in generated[channel]
        } != generated[channel]:
            raise ValueError("MEM04 CTest source or generated inputs changed during execution")
    return 0

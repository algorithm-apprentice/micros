import copy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from tests.host.selected_ipc_validation_evidence import (
    OWNER_MASK, PRODUCER, TARGET_COUNT_FIELDS, TARGET_FIELDS, TARGET_PASS, TARGET_PRODUCER,
    INDEX_CASES, expected_cases, parse_native_output, parse_target_output, target_failures, work_failures,
)
from tests.host.work_count_types import Kind, Owner
from tests.host.selected_ipc_validation_source import (
    TARGET_PATHS, validate_audit_boundaries, validate_native_sites, validate_target_observations,
    validate_target_source,
)


def transcript(channel="work", mutate=None):
    lines = []
    for row in expected_cases():
        row.update(before_state=0, after_state=0,
                   plan_bytes=144 if row["name"].startswith("completion-") else 0)
        work = {
            "version": 1, "reserved0": 0, "producer": PRODUCER, "case": row["case"],
            "owner_mask": OWNER_MASK, "reserved1": 0,
            "counts": [[0] * len(Kind) for _ in Owner],
        }
        if row["name"].startswith("completion-") and row["name"].endswith("-commit") and row["value"]:
            work["counts"][Owner.KERNEL_IPC][Kind.COPIED_BYTE] = 64
        if row["name"].startswith("dependency-"):
            work["counts"][Owner.KERNEL_IPC][Kind.LINK_TRAVERSE] = row["value"]
            work["counts"][Owner.KERNEL_IPC][Kind.BITMAP_WORD_LOAD] = row["value"] - int(row["result"] == 6)
            work["counts"][Owner.KERNEL_IPC][Kind.CLEARED_BYTE] = 16
        if row["name"].startswith("index-"):
            work["counts"][Owner.KERNEL_OBJECTS][Kind.RECORD_VISIT] = next(
                spec[3] for spec in INDEX_CASES if spec[0] == row["name"]
            )
        if row["name"].startswith("sender-append-") or row["name"] == "sender-no-match":
            work["counts"][Owner.KERNEL_IPC][Kind.LINK_TRAVERSE] = row["value"]
        if mutate is not None and row["case"] == 1:
            mutate(row, work)
        lines.append("MICROS_IPC01_CASE " + json.dumps(row))
        if channel == "work":
            lines.append("MICROS_IPC01_WORK " + json.dumps(work))
    lines.append("MICROS_IPC01_NATIVE_PASS cases=" + str(len(expected_cases())))
    return "\n".join(lines) + "\n"


class SelectedIpcValidationEvidenceTest(unittest.TestCase):
    def test_complete_semantics_and_closed_work_are_independent(self):
        ordinary, absent = parse_native_output(transcript("ordinary"), "ordinary")
        observed, work = parse_native_output(transcript(), "work")
        self.assertEqual(ordinary, observed)
        self.assertEqual((), absent)
        self.assertEqual((), work_failures(observed, work))

    def test_actual_complete_audits_and_repeated_profile_work_fail(self):
        records, work = parse_native_output(transcript(), "work")
        changed = copy.deepcopy(work)
        changed[0]["counts"][Owner.KERNEL_ENDPOINT][Kind.COMPLETE_VALIDATOR_CALL] = 1
        changed[0]["counts"][Owner.KERNEL_ENDPOINT][Kind.RANGE_VISIT] = 2
        self.assertEqual(
            (("profile-first", "complete-audits", 1, 0),
             ("profile-first", "profile-resolutions", 2, 1)),
            work_failures(records, changed),
        )

    def test_partial_duplicate_unknown_or_borrowed_records_fail(self):
        good = transcript()
        lines = good.splitlines()
        for damaged in (
            good[:-1],
            "\n".join(lines[:-1]) + "\n",
            "\n".join(lines[:1] + lines[2:]) + "\n",
            "\n".join(lines[:2] + [lines[1]] + lines[2:]) + "\n",
            good.replace('"case": 1,', '"case": 2,', 1),
            good.replace('"preserved": 1', '"preserved": true', 1),
            good.replace('"result": 0', '"result": 12', 1),
            good.replace('"value": 1', '"value": 0', 1),
            good.replace("MICROS_IPC01_WORK ", "MICROS_IPC01_UNKNOWN ", 1),
            transcript("ordinary"),
        ):
            with self.subTest(damaged=damaged[:100]), self.assertRaises(ValueError):
                parse_native_output(damaged, "work")
        with self.assertRaises(ValueError):
            parse_native_output(good, "ordinary")

    def test_malformed_snapshot_dimensions_and_overflow_fail(self):
        mutations = (
            lambda row, work: work.update(producer=PRODUCER + 1),
            lambda row, work: work.update(owner_mask=OWNER_MASK | 1),
            lambda row, work: work.update(reserved0=1),
            lambda row, work: work.update(case=True),
            lambda row, work: work["counts"].pop(),
            lambda row, work: work["counts"][Owner.KERNEL_ENDPOINT].pop(),
            lambda row, work: work["counts"][Owner.KERNEL_ENDPOINT].__setitem__(0, 1 << 64),
            lambda row, work: work["counts"][Owner.KERNEL_ENDPOINT].__setitem__(0, True),
            lambda row, work: work["counts"][Owner.KERNEL_PLATFORM].__setitem__(0, 1),
        )
        for mutate in mutations:
            with self.subTest(mutate=mutate), self.assertRaises(ValueError):
                parse_native_output(transcript(mutate=mutate), "work")

    def test_completion_cannot_relookup_or_expand_its_plan(self):
        good = transcript()
        for field, value in (("lookups", 1), ("translations", 1), ("plan_bytes", 257)):
            lines = good.splitlines()
            for index, line in enumerate(lines):
                if '"name": "completion-message-commit"' in line:
                    row = json.loads(line.split(" ", 1)[1])
                    row[field] = value
                    lines[index] = "MICROS_IPC01_CASE " + json.dumps(row)
                    break
            with self.subTest(field=field), self.assertRaises(ValueError):
                parse_native_output("\n".join(lines) + "\n", "work")

    def test_selected_queries_and_queue_prefixes_cannot_hide_extra_work(self):
        records, work = parse_native_output(transcript(), "work")
        for name, owner, kind, value in (
            ("profile-first", Owner.KERNEL_OBJECTS, Kind.RECORD_VISIT, 1),
            ("endpoint-last", Owner.KERNEL_ENDPOINT, Kind.INDEX_PROBE, 64),
            ("send-last", Owner.KERNEL_IPC, Kind.LINK_TRAVERSE, 33),
            ("kernel-last", Owner.KERNEL_IPC, Kind.LINK_TRAVERSE, 257),
        ):
            changed = copy.deepcopy(work)
            index = next(index for index, row in enumerate(records) if row["name"] == name)
            changed[index]["counts"][owner][kind] = value
            with self.subTest(name=name):
                self.assertTrue(work_failures(records, changed))

    def test_dependency_work_rejects_table_scans_prefix_search_and_missing_hops(self):
        records, work = parse_native_output(transcript(), "work")
        index = next(index for index, row in enumerate(records) if row["name"] == "dependency-long")
        for owner, kind, value in (
            (Owner.KERNEL_IPC, Kind.INDEX_PROBE, 128 * 63),
            (Owner.KERNEL_IPC, Kind.COMPARISON, 63 * 62 // 2),
            (Owner.KERNEL_IPC, Kind.LINK_TRAVERSE, 62),
            (Owner.KERNEL_IPC, Kind.LINK_TRAVERSE, 64),
            (Owner.KERNEL_IPC, Kind.BITMAP_WORD_LOAD, 62),
            (Owner.KERNEL_IPC, Kind.CLEARED_BYTE, 17),
            (Owner.KERNEL_OBJECTS, Kind.RECORD_VISIT, 1),
        ):
            changed = copy.deepcopy(work)
            changed[index]["counts"][owner][kind] = value
            with self.subTest(owner=owner, kind=kind):
                self.assertTrue(work_failures(records, changed))

    def test_index_lifetime_cannot_add_a_hot_scan_or_omit_cold_repair(self):
        records, work = parse_native_output(transcript(), "work")
        for name, visits in (("index-release-single", 136), ("index-release-pair-high", 8)):
            changed = copy.deepcopy(work)
            index = next(index for index, row in enumerate(records) if row["name"] == name)
            changed[index]["counts"][Owner.KERNEL_OBJECTS][Kind.RECORD_VISIT] = visits
            with self.subTest(name=name):
                self.assertTrue(work_failures(records, changed))

    def test_direct_token_cases_cannot_hide_lookup_or_pending_table_visits(self):
        records, work = parse_native_output(transcript(), "work")
        for name, owner in (
            ("token-call-first", Owner.KERNEL_IPC),
            ("token-reply", Owner.KERNEL_IPC),
            ("token-reply-early", Owner.KERNEL_IPC),
            ("token-staged-request", Owner.KERNEL_ENDPOINT),
        ):
            changed = copy.deepcopy(work)
            index = next(index for index, row in enumerate(records) if row["name"] == name)
            changed[index]["counts"][owner][Kind.RECORD_VISIT] = 1
            with self.subTest(name=name, owner=owner):
                self.assertTrue(work_failures(records, changed))
        index = next(index for index, row in enumerate(records) if row["name"] == "token-reply")
        changed = copy.deepcopy(work)
        changed[index]["counts"][Owner.KERNEL_OBJECTS][Kind.INDEX_PROBE] = 97
        self.assertTrue(work_failures(records, changed))

    def test_sender_append_requires_the_actual_head_walk_without_table_scans(self):
        records, work = parse_native_output(transcript(), "work")
        index = next(index for index, row in enumerate(records) if row["name"] == "sender-append-long")
        for owner, kind, value in (
            (Owner.KERNEL_IPC, Kind.LINK_TRAVERSE, 0),
            (Owner.KERNEL_IPC, Kind.LINK_TRAVERSE, 62),
            (Owner.KERNEL_IPC, Kind.LINK_TRAVERSE, 64),
            (Owner.KERNEL_OBJECTS, Kind.RECORD_VISIT, 1),
        ):
            changed = copy.deepcopy(work)
            changed[index]["counts"][owner][kind] = value
            with self.subTest(owner=owner, kind=kind, value=value):
                self.assertTrue(work_failures(records, changed))
        index = next(index for index, row in enumerate(records) if row["name"] == "sender-suffix-unconsumed")
        for owner, value in ((Owner.KERNEL_SCHEDULER, 9), (Owner.KERNEL_OBJECTS, 1)):
            changed = copy.deepcopy(work)
            changed[index]["counts"][owner][Kind.RECORD_VISIT] = value
            with self.subTest(owner=owner):
                self.assertTrue(work_failures(records, changed))
        for name, owner, kind, value in (
            ("sender-no-match", Owner.KERNEL_IPC, Kind.LINK_TRAVERSE, 6),
            ("sender-close-full", Owner.KERNEL_IPC, Kind.LINK_TRAVERSE, 385),
            ("sender-close-full", Owner.KERNEL_SCHEDULER, Kind.RECORD_VISIT, 513),
        ):
            changed = copy.deepcopy(work)
            index = next(index for index, row in enumerate(records) if row["name"] == name)
            changed[index]["counts"][owner][kind] = value
            with self.subTest(name=name, owner=owner, kind=kind):
                self.assertTrue(work_failures(records, changed))


class SelectedIpcObservationTest(unittest.TestCase):
    def test_current_native_and_target_primitive_coverage(self):
        from tests.host.selected_ipc_validation_evidence import NATIVE_SOURCES

        root = Path(__file__).resolve().parents[2]
        validate_audit_boundaries(root)
        validate_target_observations(root)
        for path in NATIVE_SOURCES:
            validate_native_sites(root, path, (root / path).read_text())

    def test_native_missing_loop_index_and_invocation_sites_fail(self):
        root = Path(__file__).resolve().parents[2]
        original = json.loads((root / "tests/host/native_observations.json").read_text())
        path = "kernel/ipc/ipc_queue.c"
        text = (root / path).read_text()
        for predicate in (
            lambda site: site[1] == "insert" and bool(site[7]),
            lambda site: site[1] == "wrap",
            lambda site: site[3] == "RANGE_VISIT",
        ):
            changed = copy.deepcopy(original)
            for name, sites in changed["families"]["IPC01"]["sites"][path].items():
                match = next((index for index, (_, site) in enumerate(sites) if predicate(site)), None)
                if match is not None:
                    del sites[match]
                    break
            with patch("tests.host.native_observations.source_bytes", return_value=json.dumps(changed).encode()):
                with self.assertRaises(ValueError):
                    validate_native_sites(root, path, text)

    def test_dependency_bitmap_requires_its_actual_read_initializers_and_slot_mapping(self):
        root = Path(__file__).resolve().parents[2]
        path = "kernel/ipc/ipc_deadlock.c"
        text = (root / path).read_text()
        original = json.loads((root / "tests/host/native_observations.json").read_text())
        for kind in ("BITMAP_WORD_LOAD", "CLEARED_BYTE"):
            changed = copy.deepcopy(original)
            sites = changed["families"]["IPC01"]["sites"][path]["ipc_preflight_deadlock"]
            del sites[next(index for index, (_, site) in enumerate(sites) if site[3] == kind)]
            with patch("tests.host.native_observations.source_bytes", return_value=json.dumps(changed).encode()):
                with self.subTest(kind=kind), self.assertRaises(ValueError):
                    validate_native_sites(root, path, text)
        with self.assertRaises(ValueError):
            validate_native_sites(root, path, text.replace("slot % 64", "slot % 63", 1))

    def test_target_audit_and_scope_hooks_cannot_move_or_disappear(self):
        root = Path(__file__).resolve().parents[2]
        path = "kernel/objects/kernel_objects.c"
        text = (root / path).read_text()
        guard = "#if defined(MICROS_BUILD_IPC_SYSCALL_TEST) || defined(MICROS_TEST_BOOTSTRAP_COMMAND_COUNTS)"
        self.assertIn(guard, text)
        for changed in (
            text.replace('MICROS_TEST_WORK_HIT("ipc01-target-objects"', 'MICROS_TEST_WORK_HIT("foreign"'),
            text.replace(guard, "#ifdef UNKNOWN_CAPTURE"),
            text.replace(guard, "if (objects == NULL) { return 0; }\n" + guard),
        ):
            with self.assertRaises(ValueError):
                validate_target_source(path, changed)
        path = "kernel/ipc/ipc_syscall.c"
        text = (root / path).read_text()
        begin = "    MICROS_IPC01_SYSCALL_BEGIN(arguments->a7, context->current);\n"
        changed = text.replace(begin, "").replace(
            "    registry = resolve_current_runtime(hart, frame, context);\n",
            "    registry = resolve_current_runtime(hart, frame, context);\n" + begin,
        )
        with self.assertRaises(ValueError):
            validate_target_source(path, changed)
        with self.assertRaises(ValueError):
            validate_target_source(path, text.replace("MICROS_IPC01_SYSCALL_END(false);", "", 1))
        path = "kernel/ipc/ipc_buffer.c"
        with self.assertRaises(ValueError):
            validate_target_source(path, (root / path).read_text().replace("ipc01-target-buffer-lookup", "foreign"))
        path = "kernel/scheduler/scheduler_completion.c"
        with self.assertRaises(ValueError):
            validate_target_source(path, (root / path).read_text().replace(
                "MICROS_IPC01_COMPLETION_END(false, 0, 0, 0);", "", 1,
            ))

    def test_complete_diagnostic_boundaries_cannot_disappear_or_reenter_queries(self):
        root = Path(__file__).resolve().parents[2]
        subjects = {str(path.relative_to(root)): path.read_text()
                    for path in (root / "kernel").rglob("*.c")
                    if "tests" not in path.relative_to(root / "kernel").parts}
        path = "kernel/tty/tty_handoff_runtime_validate.c"
        call = "micros_endpoint_registry_validate_objects(registry, objects)"
        for replacement in ("MICROS_ENDPOINT_OK", "(" + call + ", " + call + ")"):
            changed = dict(subjects)
            changed[path] = changed[path].replace(call, replacement)
            with self.assertRaises(ValueError):
                validate_audit_boundaries(root, changed)
        changed = dict(subjects)
        path = "kernel/endpoint/endpoint.c"
        changed[path] = changed[path].replace(
            "error = micros_endpoint_select(",
            "micros_endpoint_registry_validate(registry);\n    error = micros_endpoint_select(", 1,
        )
        with self.assertRaises(ValueError):
            validate_audit_boundaries(root, changed)

    def test_disabled_target_hooks_do_not_evaluate_arguments_or_link_observers(self):
        from tests.host.runtime_memory_evidence import native_compiler

        root = Path(__file__).resolve().parents[2]
        compiler = native_compiler(root)
        source = (
            '#include "tests/qemu/selected_ipc_work.h"\n'
            "int main(void) { int value = 0;\n"
            "MICROS_IPC01_SYSCALL_BEGIN(++value, ++value);\n"
            "MICROS_IPC01_SYSCALL_END(++value);\n"
            "MICROS_IPC01_COMPLETION_BEGIN(++value);\n"
            "MICROS_IPC01_COMPLETION_END(++value, ++value, ++value, ++value);\n"
            "return value; }\n"
        )
        with tempfile.TemporaryDirectory(prefix="micros-ipc01-passivity-") as directory:
            executable = str(Path(directory) / "passivity")
            for defines in ((), ("-DMICROS_TEST_WORK_COUNTS=1",)):
                built = subprocess.run(
                    (compiler["path"], "-std=c17", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
                     "-I", str(root), *defines, "-x", "c", "-", "-o", executable),
                    input=source, capture_output=True, text=True, timeout=30,
                )
                self.assertEqual((0, ""), (built.returncode, built.stderr), built.stdout + built.stderr)
                result = subprocess.run((executable,), capture_output=True, text=True, timeout=30)
                self.assertEqual((0, "", ""), (result.returncode, result.stdout, result.stderr))

    def test_target_work_flag_is_confined_to_measured_sources(self):
        from tests.host.build_source_records import cmake_command, cmake_if_blocks

        root = Path(__file__).resolve().parents[2]
        blocks = cmake_if_blocks((root / "CMakeLists.txt").read_text(), "MICROS_BUILD_IPC_SYSCALL_TEST")
        block, = (value for value in blocks if "target_compile_definitions" in value)
        definitions = cmake_command(block, "target_compile_definitions", "micros")
        self.assertNotIn("MICROS_TEST_WORK_COUNTS", definitions)
        properties = cmake_command(block, "set_source_files_properties", TARGET_PATHS[0])
        arguments = properties[properties.index("(") + 1:-1].split()
        boundary = arguments.index("PROPERTIES")
        self.assertEqual(len(arguments[:boundary]), len(set(arguments[:boundary])))
        self.assertEqual({*TARGET_PATHS, "tests/work/work_counts.c"}, set(arguments[:boundary]))
        self.assertEqual(["PROPERTIES", "COMPILE_DEFINITIONS", "MICROS_TEST_WORK_COUNTS=1"], arguments[boundary:])


def target_transcript(mutate=None):
    captures = []
    for index in range(10):
        row = {
            "class": index, "version": 1, "reserved0": 0, "producer": TARGET_PRODUCER,
            "capture": index + 1, "owner_mask": OWNER_MASK, "reserved1": 0,
            "kind": int(index >= 6), "operation": index + 1 if index < 6 else 0,
            "thread_slot": index % 3, "thread_generation": 1,
            "result": (1 << 64) - 2 if index == 7 else 0,
            "chunks": (0, 0, 1, 2)[index - 6] if index >= 6 else 0,
            "token": int(index == 8), "counts": [[0] * len(Kind) for _ in Owner],
        }
        row["counts"][Owner.KERNEL_IPC][Kind.INDEX_PROBE] = (
            0 if index == 5 else 2 if index == 4 else 1 if index < 6 else row["chunks"]
        )
        captures.append(row)
    summary = {"complete": 1, "syscalls": 63, "completions": 15, "captures": 10, "audits": 0, "tokens": 1}
    if mutate is not None:
        mutate(captures, summary)

    def line(marker, fields):
        return "# " + marker + " " + " ".join(f"{key}=0x{value:x}" for key, value in fields.items())

    lines = []
    for row in captures:
        lines.append(line("MICROS_IPC01_CAPTURE", {key: row[key] for key in TARGET_FIELDS}))
        for owner, counts in enumerate(row["counts"]):
            lines.append(line("MICROS_IPC01_CAPTURE_COUNTS", {
                "capture": row["capture"], "owner": owner,
                **dict(zip(TARGET_COUNT_FIELDS, counts, strict=True)),
            }))
    lines.append(line("MICROS_IPC01_CAPTURE_END", summary))
    lines.append("# " + TARGET_PASS)
    return "\n".join(lines) + "\n"


class SelectedIpcTargetEvidenceTest(unittest.TestCase):
    def test_complete_real_class_contract_and_actual_work_failure(self):
        self.assertEqual((), target_failures(parse_target_output(target_transcript(), 0)))

        def audits(captures, summary):
            captures[0]["counts"][Owner.KERNEL_IPC][Kind.COMPLETE_VALIDATOR_CALL] = 1
            summary["audits"] = 1

        self.assertEqual(
            (("all-scopes", "complete-audits", 1, 0),),
            target_failures(parse_target_output(target_transcript(audits), 0)),
        )
        def missing_lookup(captures, summary):
            captures[9]["counts"][Owner.KERNEL_IPC][Kind.INDEX_PROBE] = 1

        self.assertTrue(target_failures(parse_target_output(target_transcript(missing_lookup), 0)))

    def test_missing_duplicate_borrowed_and_overflowing_capture_fails(self):
        mutations = (
            lambda rows, summary: rows.pop(),
            lambda rows, summary: rows[1].update(capture=1),
            lambda rows, summary: rows[0].update(producer=TARGET_PRODUCER + 1),
            lambda rows, summary: rows[0].update(thread_generation=0),
            lambda rows, summary: rows[0].update(operation=2),
            lambda rows, summary: rows[6].update(chunks=1),
            lambda rows, summary: rows[7].update(result=0),
            lambda rows, summary: rows[0]["counts"].pop(),
            lambda rows, summary: rows[0]["counts"][Owner.KERNEL_IPC].__setitem__(0, 1 << 64),
            lambda rows, summary: rows[0]["counts"][Owner.KERNEL_IPC].__setitem__(0, 1),
            lambda rows, summary: summary.update(complete=0),
            lambda rows, summary: summary.update(syscalls=31),
            lambda rows, summary: summary.update(tokens=0),
            lambda rows, summary: summary.update(captures=9),
            lambda rows, summary: rows[0]["counts"][Owner.KERNEL_IPC].__setitem__(
                Kind.COMPLETE_VALIDATOR_CALL, 1
            ),
        )
        for mutate in mutations:
            with self.subTest(mutate=mutate), self.assertRaises(ValueError):
                parse_target_output(target_transcript(mutate), 0)

    def test_marker_failure_and_partial_snapshot_are_not_work_red(self):
        good = target_transcript()
        for output, status in (
            (good[:-1], 0),
            (good, 1),
            (good.replace("# " + TARGET_PASS + "\n", ""), 0),
            (good + "# " + TARGET_PASS + "\n", 0),
            (good + "# MICROS_PANIC unexpected\n", 0),
            (good + "# MICROS_TEST_FAILURE ipc-syscall-test\n", 0),
            (good.replace("MICROS_IPC01_CAPTURE_COUNTS", "MICROS_IPC01_UNKNOWN", 1), 0),
            (good + good.splitlines()[0] + "\n", 0),
        ):
            with self.subTest(status=status), self.assertRaises(ValueError):
                parse_target_output(output, status)


if __name__ == "__main__":
    unittest.main()

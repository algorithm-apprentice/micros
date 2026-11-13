import copy
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from tests.host.selected_grant_validation_evidence import (
    CASES, COLD_CASES, OWNER_MASK, PRODUCER, SPAN_GEOMETRY,
    expected_cases, parse_native_output, span_work, work_failures,
)
from tests.host.selected_grant_validation_source import audit_subjects, validate_audit_boundaries, validate_native_sites
from tests.host.work_count_types import Kind, Owner


def transcript(channel="work"):
    lines = []
    for row, spec in zip(expected_cases(), CASES, strict=True):
        row.update(before_state=0, after_state=0)
        work = {
            "version": 1, "reserved0": 0, "producer": PRODUCER, "case": row["case"],
            "owner_mask": OWNER_MASK, "reserved1": 0,
            "counts": [[0] * len(Kind) for _ in Owner],
        }
        work["counts"][Owner.KERNEL_GRANT][Kind.RECORD_VISIT] = spec[4]
        if row["name"] in COLD_CASES:
            work["counts"][Owner.KERNEL_GRANT][Kind.COMPLETE_VALIDATOR_CALL] = 1
        if row["name"] in SPAN_GEOMETRY:
            spans, units, copied, core = span_work(row["name"])
            work["counts"][Owner.RUNTIME][Kind.COPY_UNIT] = units
            work["counts"][Owner.RUNTIME][Kind.COPIED_BYTE] = copied
            if core:
                work["counts"][Owner.KERNEL_GRANT][Kind.RANGE_VISIT] = spans
                work["counts"][Owner.KERNEL_GRANT][Kind.INDEX_PROBE] = 2 * spans
        lines.append("MICROS_GRANT01_CASE " + json.dumps(row))
        if channel == "work":
            lines.append("MICROS_GRANT01_WORK " + json.dumps(work))
    lines.append("MICROS_GRANT01_NATIVE_PASS cases=" + str(len(CASES)))
    return "\n".join(lines) + "\n"


class SelectedGrantValidationTest(unittest.TestCase):
    def test_shared_provider_inherits_the_grant_consumers_validation(self):
        from tools import validation_plan

        consumer = ["kernel/grant/grant_copy_core.c"]
        for path in ("lib/runtime/memory.c", "lib/runtime/memory.h"):
            with self.subTest(path=path):
                self.assertLessEqual(
                    set(validation_plan.affected_workflows(consumer)),
                    set(validation_plan.affected_workflows([path])),
                )
                self.assertLessEqual(
                    set(validation_plan.affected_slow_models(consumer)),
                    set(validation_plan.affected_slow_models([path])),
                )

    def test_target_copy_ownership_rejects_missing_borrowed_or_unbound_objects(self):
        from tests.host.selected_grant_validation_evidence import _target_copy_objects

        root = Path(__file__).resolve().parents[2]
        directory = root / "build/riscv64-grant-test"
        compiler = "/admitted/clang"
        sources = ("kernel/grant/grant_copy_core.c", "lib/runtime/memory.c")
        owners = {"CMakeFiles/micros.dir/" + name + ".obj" for name in sources}
        commands = [{
            "directory": str(directory), "file": str(root / name),
            "output": "CMakeFiles/micros.dir/" + name + ".obj",
            "command": compiler + " -c " + str(root / name)
                + " -o CMakeFiles/micros.dir/" + name + ".obj",
        } for name in sources]
        self.assertEqual(set(sources), set(_target_copy_objects(root, directory, commands, owners, compiler)))
        for mutation in ("missing", "duplicate", "compiler", "response-file", "operand", "unlinked"):
            changed, linked = copy.deepcopy(commands), set(owners)
            if mutation == "missing":
                changed.pop()
            elif mutation == "duplicate":
                changed.append(changed[0])
            elif mutation == "compiler":
                changed[0]["command"] = changed[0]["command"].replace(compiler, "/foreign/clang")
            elif mutation == "response-file":
                changed[0]["command"] += " @hidden.rsp"
            elif mutation == "operand":
                changed[0]["command"] = changed[0]["command"].replace(" -c ", " -c /wrong/input.c -I ", 1)
            else:
                linked.remove(changed[0]["output"])
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                _target_copy_objects(root, directory, changed, linked, compiler)
        with tempfile.TemporaryDirectory(prefix="micros-grant-object-") as temporary:
            build = (Path(temporary) / "build").resolve()
            target = (Path(temporary) / "objects").resolve()
            build.mkdir()
            target.mkdir()
            (build / "CMakeFiles").symlink_to(target, target_is_directory=True)
            changed = [{**row, "directory": str(build)} for row in commands]
            with self.assertRaisesRegex(ValueError, "linked artifact"):
                _target_copy_objects(root, build, changed, owners, compiler)

    def test_only_the_qualified_memory_provider_can_join_native_inputs(self):
        from tests.host import native_observations
        from tools import generate_selected_grant_validation_fixture as generator

        root = Path(__file__).resolve().parents[2]
        memory = "lib/runtime/memory.c"
        original = (root / memory).read_bytes()
        self.assertIn(memory, native_observations.native_inputs(root, "GRANT01"))
        self.assertIn("lib/runtime/memory.h", native_observations.native_inputs(root, "GRANT01"))
        for channel in ("ordinary", "work"):
            self.assertTrue(generator.source_bundle(root, channel)[memory].endswith(original))
        read = generator.source_bytes
        for replacement in (
            original + b"\nvoid unqualified_memory(void) {}\n",
            original.replace(b'"copy-word-unit"', b'"missing-word-unit"', 1),
        ):
            def changed(project, requested):
                return replacement if requested == root / memory else read(project, requested)

            with self.subTest(replacement=replacement[-60:]), patch.object(generator, "source_bytes", side_effect=changed):
                with self.assertRaises(ValueError):
                    generator.source_bundle(root, "work")
        declared = json.loads((root / "tests/host/native_observations.json").read_text())
        declared["families"]["GRANT01"]["native_sources"].append("lib/runtime/runtime.c")
        with patch.object(native_observations, "source_bytes", return_value=json.dumps(declared).encode()):
            with self.assertRaises(ValueError):
                native_observations.native_inputs(root, "GRANT01")
        declared = json.loads((root / "tests/host/native_observations.json").read_text())
        declared["families"]["GRANT01"]["sites"][memory]["micros_runtime_memcpy"] = [["duplicate"]]
        with patch.object(native_observations, "source_bytes", return_value=json.dumps(declared).encode()):
            with self.assertRaisesRegex(ValueError, "duplicate.*observations"):
                generator.source_bundle(root, "work")

    def test_native_headers_cannot_hide_code_or_escape_the_compiled_input_set(self):
        from tools import generate_selected_grant_validation_fixture as generator

        root = Path(__file__).resolve().parents[2]
        read = generator.source_bytes
        generator.source_bundle(root, "ordinary")
        for path, extra in (
            ("include/micros/grant.h", "\nstatic inline int hidden(void) { return 1; }\n"),
            ("kernel/grant/grant.c", '\n#include "kernel/grant/unqualified.h"\n'),
        ):
            def changed(project, requested):
                raw = read(project, requested)
                return raw + extra.encode() if requested == root / path else raw

            with self.subTest(path=path), patch.object(generator, "source_bytes", side_effect=changed):
                with self.assertRaises(ValueError):
                    generator.source_bundle(root, "ordinary")

    def test_retained_diagnostics_and_exact_device_consumers(self):
        root = Path(__file__).resolve().parents[2]
        subjects = audit_subjects(root)
        validate_audit_boundaries(root, subjects)
        for path, before, after in (
            ("kernel/grant/grant.c", "error = validate_registry_storage(", "error = dropped_audit("),
            ("kernel/grant/grant.c", "error = micros_grant_preflight_context(", "error = micros_grant_registry_validate("),
            ("kernel/grant/grant_copy.c", "micros_tty_handoff_runtime_inspect_device_binding(",
             "micros_tty_handoff_runtime_device_authority("),
        ):
            changed = dict(subjects)
            self.assertIn(before, changed[path])
            changed[path] = changed[path].replace(before, after, 1)
            with self.subTest(path=path, before=before), self.assertRaises(ValueError):
                validate_audit_boundaries(root, changed)

    def test_launcher_rejects_consumed_environment_before_python(self):
        root = Path(__file__).resolve().parents[2]
        result = subprocess.run(
            (str(root / "build/host-debug/selected_grant_validation_test"),
             str(root / "build/host-debug/selected_grant_validation_ordinary")),
            cwd=root, env={**os.environ, "__PYVENV_LAUNCHER__": "/invalid/grant01-python"},
            stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=10,
        )
        self.assertEqual(2, result.returncode)
        self.assertEqual("", result.stdout)
        self.assertEqual("__PYVENV_LAUNCHER__ is unsupported\n", result.stderr)

    def test_sparse_plan_cannot_hide_aggregate_initialization_or_copy(self):
        root = Path(__file__).resolve().parents[2]
        subjects = audit_subjects(root)
        for path, before, after in (
            ("kernel/grant/grant.c", "struct micros_grant_cancel_plan candidate;",
             "struct micros_grant_cancel_plan candidate = {0};"),
            ("kernel/pm/pm_control_syscall_exit.c", "struct micros_grant_cancel_plan grants;",
             "struct micros_grant_cancel_plan grants = {0};"),
            ("kernel/grant/grant.c",
             "copy_bytes(&plan->expected_generation[slot], &candidate->expected_generation[slot],\n"
             "                   sizeof(plan->expected_generation[slot]));",
             "copy_bytes(plan, candidate, sizeof(*plan));"),
        ):
            changed = dict(subjects)
            self.assertIn(before, changed[path])
            changed[path] = changed[path].replace(before, after, 1)
            with self.subTest(path=path, before=before), self.assertRaises(ValueError):
                validate_audit_boundaries(root, changed)

    def test_complete_channels_and_independent_bounds(self):
        ordinary, absent = parse_native_output(transcript("ordinary"), "ordinary")
        observed, work = parse_native_output(transcript(), "work")
        self.assertEqual(ordinary, observed)
        self.assertEqual((), absent)
        self.assertEqual((), work_failures(observed, work))

    def test_audits_scans_and_hidden_bulk_work_fail(self):
        records, work = parse_native_output(transcript(), "work")
        for name, owner, kind, value in (
            ("inspect-last", Owner.KERNEL_GRANT, Kind.COMPLETE_VALIDATOR_CALL, 1),
            ("inspect-last", Owner.KERNEL_GRANT, Kind.RECORD_VISIT, 1),
            ("authority-first", Owner.KERNEL_ENDPOINT, Kind.RECORD_VISIT, 1),
            ("revoke-first", Owner.KERNEL_GRANT, Kind.COPIED_BYTE, 257),
            ("inspect-first", Owner.KERNEL_GRANT, Kind.COMPARISON, 513),
            ("create-empty", Owner.KERNEL_GRANT, Kind.RECORD_VISIT, 1),
            ("create-last", Owner.KERNEL_GRANT, Kind.RECORD_VISIT, 1),
            ("cancel-empty", Owner.KERNEL_GRANT, Kind.RECORD_VISIT, 1),
            ("cancel-sparse", Owner.KERNEL_GRANT, Kind.RECORD_VISIT, 3),
            ("cancel-empty", Owner.KERNEL_GRANT, Kind.COPIED_BYTE, 33),
            ("cancel-sparse", Owner.KERNEL_GRANT, Kind.CLEARED_BYTE, 33),
            ("cancel-commit-empty", Owner.KERNEL_GRANT, Kind.RECORD_VISIT, 1),
            ("cancel-commit-sparse", Owner.KERNEL_GRANT, Kind.RECORD_VISIT, 3),
            ("cancel-commit-mixed", Owner.KERNEL_GRANT, Kind.RECORD_VISIT, 9),
            ("cancel-commit-empty", Owner.KERNEL_GRANT, Kind.CLEARED_BYTE, 33),
            ("cancel-commit-sparse", Owner.KERNEL_GRANT, Kind.COPIED_BYTE, 105),
            ("diagnostic-dense", Owner.KERNEL_GRANT, Kind.COMPLETE_VALIDATOR_CALL, 0),
        ):
            changed = copy.deepcopy(work)
            index = next(index for index, row in enumerate(records) if row["name"] == name)
            changed[index]["counts"][owner][kind] = value
            with self.subTest(name=name, owner=owner, kind=kind):
                self.assertTrue(work_failures(records, changed))

    def test_span_oracle_rejects_byte_fallback_replanning_and_hidden_writes(self):
        self.assertEqual((1, 8, 64, 1), span_work("span-block"))
        self.assertEqual((3, 512, 4096, 1), span_work("span-unequal-cross"))
        self.assertEqual((3, 519, 4096, 1), span_work("span-unaligned-cross"))
        self.assertEqual((1, 64, 64, 1), span_work("span-different-residues"))
        records, work = parse_native_output(transcript(), "work")
        for name, owner, kind, value in (
            ("span-block", Owner.RUNTIME, Kind.COPY_UNIT, 64),
            ("span-block", Owner.RUNTIME, Kind.COPY_UNIT, 0),
            ("span-block", Owner.RUNTIME, Kind.COPIED_BYTE, 65),
            ("span-unequal-cross", Owner.KERNEL_GRANT, Kind.RANGE_VISIT, 4),
            ("span-unequal-cross", Owner.KERNEL_GRANT, Kind.INDEX_PROBE, 7),
            ("span-block", Owner.KERNEL_OBJECTS, Kind.COPY_UNIT, 1),
            ("span-validate-page", Owner.RUNTIME, Kind.COPY_UNIT, 1),
            ("span-late-fault", Owner.RUNTIME, Kind.COPIED_BYTE, 1),
        ):
            changed = copy.deepcopy(work)
            index = next(index for index, row in enumerate(records) if row["name"] == name)
            changed[index]["counts"][owner][kind] = value
            with self.subTest(name=name, kind=kind):
                self.assertTrue(work_failures(records, changed))

    def test_partial_borrowed_unknown_or_malformed_records_fail(self):
        good = transcript()
        lines = good.splitlines()
        for damaged in (
            good[:-1], "\n".join(lines[:-1]) + "\n",
            "\n".join(lines[:1] + lines[2:]) + "\n",
            "\n".join(lines[:2] + [lines[1]] + lines[2:]) + "\n",
            good.replace('"case": 1,', '"case": 2,', 1),
            good.replace('"preserved": 0', '"preserved": true', 1),
            good.replace('"producer": ' + str(PRODUCER), '"producer": ' + str(PRODUCER + 1), 1),
            good.replace("MICROS_GRANT01_WORK ", "UNKNOWN ", 1),
            transcript("ordinary"),
        ):
            with self.subTest(damaged=damaged[:80]), self.assertRaises(ValueError):
                parse_native_output(damaged, "work")
        with self.assertRaises(ValueError):
            parse_native_output(good, "ordinary")

    def test_current_primitive_coverage_and_missing_sites_fail(self):
        from tests.host.selected_grant_validation_evidence import NATIVE_SOURCES, OBSERVED_HEADERS

        root = Path(__file__).resolve().parents[2]
        for path in (*NATIVE_SOURCES, *OBSERVED_HEADERS):
            validate_native_sites(root, path, (root / path).read_text())
        path = "kernel/grant/grant.c"
        text = (root / path).read_text()
        original = json.loads((root / "tests/host/native_observations.json").read_text())
        for kind in ("COMPLETE_VALIDATOR_CALL", "RECORD_VISIT", "INDEX_PROBE"):
            changed = copy.deepcopy(original)
            for sites in changed["families"]["GRANT01"]["sites"][path].values():
                match = next((index for index, (_, site) in enumerate(sites) if site[3] == kind), None)
                if match is not None:
                    del sites[match]
                    break
            with patch("tests.host.native_observations.source_bytes", return_value=json.dumps(changed).encode()):
                with self.subTest(kind=kind), self.assertRaises(ValueError):
                    validate_native_sites(root, path, text)

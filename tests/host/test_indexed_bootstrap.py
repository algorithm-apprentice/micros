import copy
import json
import os
from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch

from tests.host.indexed_bootstrap_evidence import (
    CASES, NATIVE_SOURCES, OBSERVED_HEADERS, OWNER_MASK, PRODUCER,
    expected_cases, parse_native_output, work_failures,
)
from tests.host.indexed_bootstrap_source import (
    audit_subjects, validate_audit_boundaries, validate_native_sites, validate_selected_movement,
)
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
        work["counts"][Owner.KERNEL_BOOT][Kind.COMPLETE_VALIDATOR_CALL] = spec[7]
        lines.append("MICROS_BOOT01_CASE " + json.dumps(row))
        if channel == "work":
            lines.append("MICROS_BOOT01_WORK " + json.dumps(work))
    lines.append("MICROS_BOOT01_NATIVE_PASS cases=" + str(len(CASES)))
    return "\n".join(lines) + "\n"


class IndexedBootstrapTest(unittest.TestCase):
    def test_complete_channels_and_independent_bounds(self):
        ordinary, absent = parse_native_output(transcript("ordinary"), "ordinary")
        observed, work = parse_native_output(transcript(), "work")
        self.assertEqual(ordinary, observed)
        self.assertEqual((), absent)
        self.assertEqual((), work_failures(observed, work))

    def test_scans_full_snapshots_and_dropped_audits_fail(self):
        records, work = parse_native_output(transcript(), "work")
        for name, kind, value in (
            ("binding-last", Kind.RECORD_VISIT, 1),
            ("classify-last", Kind.RECORD_VISIT, 1),
            ("runtime-release-many", Kind.RECORD_VISIT, 2),
            ("runtime-release-many", Kind.COPIED_BYTE, 384),
            ("runtime-ready-many", Kind.CLEARED_BYTE, 384),
            ("control-release", Kind.COPIED_BYTE, 384),
            ("control-release", Kind.COMPLETE_VALIDATOR_CALL, 1),
            ("control-ready-plan", Kind.COPIED_BYTE, 384),
            ("control-complete-plan", Kind.CLEARED_BYTE, 520),
        ):
            changed = copy.deepcopy(work)
            index = next(index for index, row in enumerate(records) if row["name"] == name)
            changed[index]["counts"][Owner.KERNEL_BOOT][kind] = value
            with self.subTest(name=name, kind=kind):
                self.assertTrue(work_failures(records, changed))

    def test_partial_borrowed_or_malformed_records_fail(self):
        good = transcript()
        lines = good.splitlines()
        for damaged in (
            good[:-1], "\n".join(lines[:-1]) + "\n", "\n".join(lines[:1] + lines[2:]) + "\n",
            "\n".join(lines[:2] + [lines[1]] + lines[2:]) + "\n",
            good.replace('"case": 1,', '"case": 2,', 1),
            good.replace('"preserved": 1', '"preserved": true', 1),
            good.replace('"producer": ' + str(PRODUCER), '"producer": ' + str(PRODUCER + 1), 1),
            good.replace("MICROS_BOOT01_WORK ", "UNKNOWN ", 1),
            transcript("ordinary"),
        ):
            with self.subTest(damaged=damaged[:70]), self.assertRaises(ValueError):
                parse_native_output(damaged, "work")
        records, work = parse_native_output(good, "work")
        changed = copy.deepcopy(records)
        changed[0]["value"] += 1
        with self.assertRaises(ValueError):
            work_failures(changed, work)

    def test_retained_control_and_runtime_audits_cannot_disappear(self):
        root = Path(__file__).resolve().parents[2]
        subjects = audit_subjects(root)
        validate_audit_boundaries(root, subjects)
        for path, before in (
            ("kernel/boot/bootstrap_control_core.c", "micros_bootstrap_control_validate(state, registry, objects)"),
            ("kernel/boot/bootstrap_syscall.c", "micros_bootstrap_runtime_validate()"),
        ):
            changed = dict(subjects)
            self.assertIn(before, changed[path])
            changed[path] = changed[path].replace(before, "dropped_audit()", 1)
            with self.subTest(path=path), self.assertRaises(ValueError):
                validate_audit_boundaries(root, changed)

    def test_current_primitive_coverage_and_missing_sites_fail(self):
        root = Path(__file__).resolve().parents[2]
        for path in (*NATIVE_SOURCES, *OBSERVED_HEADERS):
            validate_native_sites(root, path, (root / path).read_text())
        path = "kernel/boot/bootstrap_control_core.c"
        text = (root / path).read_text()
        original = json.loads((root / "tests/host/native_observations.json").read_text())
        for kind in ("COMPLETE_VALIDATOR_CALL", "RECORD_VISIT", "INDEX_PROBE"):
            changed = copy.deepcopy(original)
            for sites in changed["families"]["BOOT01"]["sites"][path].values():
                match = next((index for index, (_, site) in enumerate(sites) if site[3] == kind), None)
                if match is not None:
                    del sites[match]
                    break
            with patch("tests.host.native_observations.source_bytes", return_value=json.dumps(changed).encode()):
                with self.subTest(kind=kind), self.assertRaises(ValueError):
                    validate_native_sites(root, path, text)

    def test_headers_cannot_hide_code_or_escape_the_compiled_inputs(self):
        from tools import generate_indexed_bootstrap_fixture as generator

        root = Path(__file__).resolve().parents[2]
        read = generator.source_bytes
        generator.source_bundle(root, "ordinary")
        for path, extra in (
            ("include/micros/bootstrap.h", "\nstatic inline int hidden(void) { return 1; }\n"),
            ("kernel/boot/bootstrap_transitions.c", '\n#include "kernel/boot/unqualified.h"\n'),
        ):
            def changed(project, requested):
                raw = read(project, requested)
                return raw + extra.encode() if requested == root / path else raw

            with self.subTest(path=path), patch.object(generator, "source_bytes", side_effect=changed):
                with self.assertRaises(ValueError):
                    generator.source_bundle(root, "ordinary")

    def test_launcher_rejects_consumed_environment_before_python(self):
        root = Path(__file__).resolve().parents[2]
        result = subprocess.run(
            (str(root / "build/host-debug/indexed_bootstrap_test"),
             str(root / "build/host-debug/indexed_bootstrap_ordinary")),
            cwd=root, env={**os.environ, "__PYVENV_LAUNCHER__": "/invalid/boot01-python"},
            stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=10,
        )
        self.assertEqual(2, result.returncode)
        self.assertEqual("", result.stdout)
        self.assertEqual("__PYVENV_LAUNCHER__ is unsupported\n", result.stderr)

    def test_selected_movement_rejects_unobserved_aggregate_work(self):
        root = Path(__file__).resolve().parents[2]
        path = "kernel/boot/bootstrap_transitions.c"
        text = (root / path).read_text()
        validate_selected_movement(path, text)
        for before, after in (
            ("struct bootstrap_transition_plan candidate;", "struct micros_bootstrap_runtime snapshot;"),
            ("sizeof(plan->entry)", "sizeof(*runtime)"),
            ("runtime->phase = plan->phase;", "memcpy(runtime, plan, sizeof(*runtime));"),
            ("runtime->phase = plan->phase;", "*runtime = *runtime;"),
        ):
            with self.subTest(after=after), self.assertRaises(ValueError):
                validate_selected_movement(path, text.replace(before, after, 1))

    def test_declared_subsets_preserve_actual_context_storage_and_functions(self):
        from tests.host.c_source_records import file_scope_objects, function_records
        from tests.host.indexed_bootstrap_evidence import NATIVE_SUBSETS
        from tools.generate_indexed_bootstrap_fixture import source_bundle

        root = Path(__file__).resolve().parents[2]
        for channel in ("ordinary", "work"):
            bundle = source_bundle(root, channel)
            for path, names in NATIVE_SUBSETS.items():
                original = (root / path).read_text()
                rendered = bundle[path].decode()
                self.assertEqual(set(names), set(function_records(rendered)))
                self.assertEqual(file_scope_objects(original), file_scope_objects(rendered))
                if channel == "ordinary":
                    for name in names:
                        self.assertEqual(function_records(original)[name].body, function_records(rendered)[name].body)

    def test_shared_header_consumers_observe_the_same_indexed_accesses(self):
        root = Path(__file__).resolve().parents[2]
        declared = json.loads((root / "tests/host/native_observations.json").read_text())
        path = "kernel/boot/bootstrap_control_internal.h"
        name = "micros_bootstrap_control_find_binding_bounded"
        expected = declared["families"]["BOOT01"]["sites"][path][name]
        for family in ("MEM04", "MEM05", "GRANT01"):
            rows = declared["families"][family]["sites"][path][name]
            self.assertEqual([row[1][1:] for row in expected], [row[1][1:] for row in rows])
            self.assertTrue(all(row[0] == (["work"] if family == "GRANT01" else ["aggregate", "context"]) for row in rows))

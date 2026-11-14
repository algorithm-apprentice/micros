"""Current bootstrap allocation semantics, work and native-source ownership."""

from dataclasses import replace
from pathlib import Path
import re
import unittest
from unittest.mock import patch

from tests.host import native_observations
from tests.host.build_source_records import cmake_command
from tests.host.work_count_types import Stage


ROOT = Path(__file__).resolve().parents[2]


class BootstrapAllocationSourceTest(unittest.TestCase):
    def test_current_native_generation_does_not_restore_an_ancestor(self):
        from tools import generate_bootstrap_allocation_fixture as generator

        with patch.object(generator, "fixed_inputs", side_effect=AssertionError("historical native inputs"), create=True):
            inputs, outputs = generator.source_bundle(ROOT, Stage.GREEN, observed=False)
        self.assertIn("kernel/memory/frame_allocator.c", inputs)
        self.assertEqual(set(inputs), set(outputs))
        self.assertTrue(all(value["mode"] == 0o644 for value in inputs.values()))

    def test_current_native_recipe_contains_every_declared_source_once(self):
        text = (ROOT / "CMakeLists.txt").read_text()
        block = cmake_command(text, "set", "MEM01_NATIVE_INPUTS")
        observed = re.findall(r"(?:kernel|lib)/[A-Za-z0-9_/]+\.c", block)
        expected = native_observations.declarations(ROOT)["families"]["MEM01"]["native_sources"]
        self.assertEqual(len(observed), len(set(observed)))
        self.assertEqual(set(expected), set(observed))
        for tier in ("test-unit-fast", "test-unit"):
            self.assertEqual(1, len(re.findall(
                r"\badd_dependencies\(\s*" + re.escape(tier) + r"\s+bootstrap_allocation_test\s*\)", text,
            )))


class BootstrapAllocationNativeTest(unittest.TestCase):
    def test_actual_current_runtime_passes_work_semantics_and_rollback(self):
        from tests.host.bootstrap_allocation_evidence import BootstrapAllocationOracles, measure_native

        measured = measure_native(ROOT, Stage.GREEN)
        ordinary = measure_native(ROOT, Stage.GREEN, observed=False)
        self.assertEqual(tuple(replace(row, capture=None) for row in measured.records), ordinary.records)
        self.assertTrue(measured.records)
        self.assertTrue(all(row.capture is None for row in ordinary.records))
        self.assertEqual((), ordinary.observation_symbols)
        for method in (
            "test_bootstrap_range_work", "test_allocator_prefix_work", "test_owner_cursor_composition",
        ):
            case = BootstrapAllocationOracles(method)
            case.measured = measured
            result = unittest.TestResult()
            case.run(result)
            with self.subTest(oracle=method):
                self.assertEqual(1, result.testsRun)
                self.assertFalse(result.errors, result.errors)
                self.assertFalse(result.failures, result.failures)
                self.assertFalse(result.skipped)


if __name__ == "__main__":
    unittest.main()

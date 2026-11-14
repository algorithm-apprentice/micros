"""Current kernel mapping topology, primitive work and hardware record contracts."""

import json
from pathlib import Path
import re
import unittest

from tests.host.build_source_records import cmake_command
from tests.host import native_observations


ROOT = Path(__file__).resolve().parents[2]


class KernelMappingSourceTest(unittest.TestCase):
    def test_current_native_recipe_contains_every_declared_source_once(self):
        text = (ROOT / "CMakeLists.txt").read_text()
        observed = re.findall(r"(?:kernel|lib)/[A-Za-z0-9_/]+\.c", cmake_command(text, "set", "MEM02_NATIVE_INPUTS"))
        expected = native_observations.declarations(ROOT)["families"]["MEM02"]["native_sources"]
        self.assertEqual(len(observed), len(set(observed)))
        self.assertEqual(set(expected), set(observed))
        for tier in ("test-unit-fast", "test-unit"):
            self.assertEqual(1, len(re.findall(
                r"\badd_dependencies\(\s*" + re.escape(tier) + r"\s+kernel_mapping_test\s*\)", text,
            )))

    def test_current_native_generation_does_not_restore_an_ancestor(self):
        from unittest.mock import patch
        from tests.host.work_count_types import Stage
        from tools import generate_kernel_mapping_fixture as generator

        with patch.object(generator, "fixed_inputs", side_effect=AssertionError("historical native inputs"), create=True):
            inputs, outputs = generator.source_bundle(ROOT, Stage.GREEN, observed=False)
        self.assertIn("kernel/memory/address_space.c", inputs)
        self.assertEqual(set(inputs), set(outputs))


class KernelMappingNativeTest(unittest.TestCase):
    def test_actual_current_mapper_passes_semantics_work_and_observation_absence(self):
        from tests.host.kernel_mapping_evidence import KernelMappingOracles, decode_measurements, measure_native
        from tests.host.work_count_types import Stage

        measured = measure_native(ROOT, Stage.GREEN)
        ordinary = measure_native(ROOT, Stage.GREEN, observed=False)
        self.assertEqual(85, len(measured.records))
        self.assertEqual(
            [(row.case, row.operation, row.tables, row.middle_tables, row.leaves) for row in measured.records],
            [(row.case, row.operation, row.tables, row.middle_tables, row.leaves) for row in ordinary.records],
        )
        self.assertEqual((), ordinary.observation_symbols)
        lines = measured.output.splitlines()
        with self.assertRaises(ValueError):
            decode_measurements("\n".join(lines[:-1]) + "\n", Stage.GREEN, observed=True)
        changed = json.loads(lines[1])
        changed["capture"]["snapshot"]["counts"][2][5] = -1
        with self.assertRaises(ValueError):
            decode_measurements(
                "\n".join((lines[0], json.dumps(changed), *lines[2:])) + "\n",
                Stage.GREEN, observed=True,
            )
        changed = json.loads(lines[1])
        changed["capture"]["snapshot"]["capture_label"] = 2
        with self.assertRaises(ValueError):
            decode_measurements(
                "\n".join((lines[0], json.dumps(changed), *lines[2:])) + "\n",
                Stage.GREEN, observed=True,
            )
        for method in ("test_large_leaf_topology", "test_direct_table_work"):
            case = KernelMappingOracles(method)
            case.measured = measured
            result = unittest.TestResult()
            case.run(result)
            with self.subTest(oracle=method):
                self.assertFalse(result.errors, result.errors)
                self.assertFalse(result.failures, result.failures)
                self.assertEqual(1, result.testsRun)


class KernelMappingTargetRecordTest(unittest.TestCase):
    def fixture(self, *, largest):
        from tests.host.qemu_smoke_test_fixture import FRAME_ALLOCATOR_OUTPUT, FRAME_ALLOCATOR_READY_RECORD, MMU_TEST_PASS
        from tools.qemu_smoke_records import QemuResult

        binding = {"linker_bounds": {
            "__kernel_start": 0x80200000, "__kernel_text_start": 0x80200000,
            "__kernel_text_end": 0x80201000, "__kernel_rodata_start": 0x80201000,
            "__kernel_rodata_end": 0x80202000, "__kernel_writable_start": 0x80202000,
            "__kernel_writable_end": 0x80203000, "__kernel_end": 0x80203000,
        }}
        output = FRAME_ALLOCATOR_OUTPUT.replace(
            FRAME_ALLOCATOR_READY_RECORD,
            "MICROS_FRAME_ALLOCATOR_READY managed=0x0000000000007dfd free=0x0000000000007dfd\n",
        )
        tables = 7 if largest else 69
        leaves = (516, 62, 0) if largest else (32260, 0, 0)
        observation = (
            f"MICROS_KERNEL_MAPPING_OBSERVATION version=1 tables=0x{tables:016x} "
            f"leaves-4k=0x{leaves[0]:016x} leaves-2m=0x{leaves[1]:016x} "
            f"leaves-1g=0x{leaves[2]:016x} largest={int(largest)}\n"
        )
        guard = "MICROS_KERNEL_MAPPING_GUARDS aliases=2m,1g misaligned=rejected resources=preserved\n"
        passing = (
            "MICROS_KERNEL_MAPPING_TEST_PASS topology=largest aliases=2m,1g "
            "misaligned=rejected resources=preserved\n"
        )
        output += (
            f"MICROS_MMU_READY mode=sv39 root=0x0000000080203000 tables=0x{tables:016x}\n"
            f"MICROS_FRAME_OWNERSHIP_READY owned=0x{tables:016x} kernel-tables=0x{tables:016x} phase=bootstrap\n"
            + observation + guard + (passing if largest else "") + MMU_TEST_PASS
            + "MICROS_TTY_CONTROLLER_TEST_PASS mapping=exact priority=disabled enable=disabled "
            "context=supervisor seie=independent\n"
        )
        return QemuResult(output, 0, False), binding, observation, guard, passing

    def test_synthetic_protocol_controls_distinguish_baseline_from_corrected_topology(self):
        from tests.host.kernel_mapping_evidence import decode_target_output

        for largest in (False, True):
            result, binding, _, _, _ = self.fixture(largest=largest)
            observed = decode_target_output(result, binding)
            with self.subTest(largest=largest):
                self.assertIs(largest, observed["largest"])
                self.assertEqual(7, observed["expected_tables"])
                self.assertEqual((516, 62, 0), observed["expected_leaves"])

    def test_records_cannot_replace_original_guards_or_borrow_other_topology(self):
        from dataclasses import replace
        from tests.host.kernel_mapping_evidence import decode_target_output

        result, binding, observation, guard, passing = self.fixture(largest=True)
        corruptions = (
            result.output.replace(guard, ""),
            result.output.replace(observation, observation * 2),
            result.output.replace(observation + guard, guard + observation),
            result.output.replace(observation, observation.rstrip("\n")),
            result.output.replace("traps=0x0000000000000002", "traps=0x0000000000000003"),
            result.output.replace("leaves-2m=0x000000000000003e", "leaves-2m=0x000000000000003f"),
            result.output + passing,
        )
        for output in corruptions:
            with self.subTest(output=output), self.assertRaises(ValueError):
                decode_target_output(replace(result, output=output), binding)
        for changed in (replace(result, timed_out=True), replace(result, return_code=1)):
            with self.assertRaises(ValueError):
                decode_target_output(changed, binding)
        baseline, binding, _, guard, passing = self.fixture(largest=False)
        forged = baseline.output.replace("largest=0", "largest=1").replace(guard, guard + passing)
        with self.assertRaises(ValueError):
            decode_target_output(replace(baseline, output=forged), binding)

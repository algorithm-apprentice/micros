"""MEM03 independent selected-work and complete-diagnostic evidence."""

import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
from unittest import mock

from tests.host.build_source_records import cmake_command
from tests.host import selected_ownership_source as source
from tests.host.work_count_types import Stage


ROOT = Path(__file__).resolve().parents[2]


class SelectedOwnershipNativeTest(unittest.TestCase):
    def test_current_native_generation_does_not_restore_an_ancestor(self):
        from tools import generate_selected_ownership_fixture as generator

        with mock.patch.object(generator, "fixed_inputs", side_effect=AssertionError("historical native inputs"), create=True):
            inputs, outputs = generator.source_bundle(ROOT, Stage.GREEN, "ordinary")
        self.assertIn("kernel/memory/frame_ownership_validation.c", inputs)
        self.assertEqual(set(inputs), set(outputs))

    def test_launcher_rejects_consumed_environment_before_interpreter_exec(self):
        from tests.host.c_source_records import function_records
        from tests.host.runtime_memory_evidence import native_compiler

        text = (ROOT / "tests/host/selected_ownership_test.c").read_text()
        launcher = function_records(text)["launch_native_products"]
        compiler = native_compiler(ROOT)
        with tempfile.TemporaryDirectory(prefix="micros-mem03-launcher-") as temporary:
            directory = Path(temporary)
            executable = directory / "launcher"
            probe = directory / "launcher.c"
            probe.write_text(
                "#include <stddef.h>\n#include <stdio.h>\n"
                "#include <stdlib.h>\n#include <unistd.h>\n"
                + launcher.signature + "{" + launcher.body + "}\n"
                + "int main(int argc, char **argv)\n{\n"
                "    if (argc == 6) {\n"
                "        return launch_native_products(argc, argv);\n"
                "    }\n"
                '    puts("interpreter execution reached");\n'
                "    return 37;\n}\n"
            )
            built = subprocess.run(
                (
                    compiler["path"], "-std=c17", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
                    "-DMICROS_SELECTED_OWNERSHIP_PYTHON=" + json.dumps(str(executable)),
                    '-DMICROS_SELECTED_OWNERSHIP_DRIVER="unused.py"',
                    "-DMICROS_SELECTED_OWNERSHIP_ROOT=" + json.dumps(str(directory)),
                    str(probe), "-o", str(executable),
                ),
                cwd=ROOT, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=30,
            )
            self.assertEqual(0, built.returncode, built.stdout + built.stderr)
            for value in (None, "", str(executable)):
                environment = dict(os.environ)
                environment.pop("__PYVENV_LAUNCHER__", None)
                if value is not None:
                    environment["__PYVENV_LAUNCHER__"] = value
                with self.subTest(launcher_environment=value):
                    result = subprocess.run(
                        (str(executable),) * 6, env=environment,
                        stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=10,
                    )
                    if value:
                        self.assertEqual(2, result.returncode)
                        self.assertEqual("", result.stdout)
                        self.assertIn("__PYVENV_LAUNCHER__ is unsupported", result.stderr)
                    else:
                        self.assertEqual(37, result.returncode)
                        self.assertEqual("interpreter execution reached\n", result.stdout)
                        self.assertEqual("", result.stderr)


class SelectedOwnershipObservationTest(unittest.TestCase):
    def test_current_audit_boundaries_do_not_read_an_ancestor(self):
        with mock.patch.object(source, "baseline_source", side_effect=AssertionError("historical audit input"), create=True):
            measured = source.measure_audit_boundaries(ROOT, Stage.GREEN)
        self.assertEqual(8, sum(row[-1] for row in measured["complete"]))
        self.assertEqual(21, len(measured["selected"]))

    def test_current_work_preserves_pm_counts_without_a_historical_product(self):
        from tests.host.selected_ownership_evidence import _work_failures, expected_cases
        from tests.host.work_count_types import Kind, Owner

        measured = {
            channel: {"observations": [{"counts": [[0] * len(Kind) for _ in Owner]} for _ in expected_cases()]}
            for channel in source.STATE_WORK_CONTRACT["channels"]
        }
        for case in expected_cases():
            if case["operation"] == 6:
                counts = measured["aggregate"]["observations"][case["case"] - 1]["counts"]
                for owner, probes in ((Owner.KERNEL_OBJECTS, 50632), (Owner.KERNEL_SCHEDULER, 264)):
                    counts[owner][Kind.INDEX_PROBE] = probes
                    counts[owner][Kind.COMPLETE_VALIDATOR_CALL] = 1
        self.assertFalse(any(_work_failures(measured).values()))
        case = next(row for row in expected_cases() if row["operation"] == 6)
        measured["aggregate"]["observations"][case["case"] - 1]["counts"][Owner.KERNEL_OBJECTS][Kind.INDEX_PROBE] += 1
        with self.assertRaisesRegex(ValueError, "PM"):
            _work_failures(measured)


class SelectedOwnershipIntegrationTest(unittest.TestCase):
    def test_native_orchestration_is_full_only_and_labeled_slow(self):
        recipe = (ROOT / "CMakeLists.txt").read_text()
        for product in source.RECIPE_ADMISSION["native_targets"]:
            self.assertRegex(recipe, r"add_dependencies\(test-unit\s+[^)]*\b" + product + r"\b")
            self.assertNotRegex(recipe, r"add_dependencies\(test-unit-fast\s+[^)]*\b" + product + r"\b")
        self.assertIn("LABELS slow", cmake_command(recipe, "set_tests_properties", "selected-ownership"))


    def test_single_ctest_tracks_all_six_products_with_the_unchanged_manifest_policy(self):
        from tools.validation_build_manifest import native_outputs
        from tools.validation_inputs import UnsupportedContext

        recipe = "".join(
            "add_executable(" + name + " fixture.c)\n" for name in source.RECIPE_ADMISSION["native_targets"]
        ) + (
            "add_test(NAME selected-ownership COMMAND selected_ownership_ordinary_test)\n"
            "set_tests_properties(selected-ownership PROPERTIES LABELS slow)\n"
        )
        self.assertEqual(
            ["selected-ownership"],
            re.findall(r"add_test\(\s*NAME\s+([A-Za-z0-9_-]+)", recipe),
        )
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            directory = root / "build"
            directory.mkdir()
            (root / "CMakeLists.txt").write_text(recipe)
            products = tuple(directory / name for name in source.RECIPE_ADMISSION["native_targets"])
            for path in products:
                path.write_bytes(b"fixture-output")
            command = [str(products[-1]), *map(str, products[:-1])]
            slow = [{"name": "LABELS", "value": ["slow"]}]
            inventory = {"tests": [{"name": "selected-ownership", "command": command, "properties": slow}]}
            with mock.patch("tests.host.work_count_recipe.native_test_declarations", return_value=[]):
                self.assertEqual(set(products), set(native_outputs(root, directory, inventory)))
                inventory["tests"][0]["properties"] = []
                with self.assertRaisesRegex(UnsupportedContext, "slow CTest"):
                    native_outputs(root, directory, inventory)
                inventory["tests"][0]["properties"] = slow
                inventory["tests"][0]["command"] = [*command, "--native"]
                with self.assertRaises(UnsupportedContext):
                    native_outputs(root, directory, inventory)


class SelectedOwnershipProtocolTest(unittest.TestCase):
    def transcript(self, mutate=None):
        from tests.host.selected_ownership_evidence import expected_cases, expected_guards

        lines = []
        for case in expected_cases():
            lines.append("MICROS_MEM03_CASE " + json.dumps(case))
            observation = {
                "version": 1, "reserved0": 0, "reserved1": 0,
                "producer_label": source.STATE_WORK_CONTRACT["producer_base"] + source.STATE_WORK_CONTRACT["current_producer_offset"] + 1,
                "capture_label": case["case"], "owner_mask": source.STATE_WORK_CONTRACT["owner_mask"],
                "counts": [[0] * 14 for _ in range(27)],
            }
            if mutate is not None and case["case"] == 1:
                mutate(observation)
            lines.append("MICROS_MEM03_WORK " + json.dumps(observation))
        lines.extend("MICROS_MEM03_GUARD " + guard + " PASS" for guard in expected_guards(Stage.GREEN))
        lines.append("MICROS_MEM03_SEMANTICS_PASS cases=99")
        return "\n".join(lines) + "\n"

    def test_snapshot_schema_is_complete_and_bound_to_external_stage_and_channel(self):
        from tests.host.selected_ownership_evidence import parse_native_output

        output = self.transcript()
        cases, work = parse_native_output(output, Stage.GREEN, "aggregate")
        self.assertEqual((99, 99), (len(cases), len(work)))
        for stage, channel in ((Stage.RED, "aggregate"), (Stage.GREEN, "active-frame"), (Stage.GREEN, "ordinary")):
            with self.subTest(stage=stage, channel=channel), self.assertRaises(ValueError):
                parse_native_output(output, stage, channel)
        mutations = (
            lambda value: value.pop("reserved0"),
            lambda value: value.update(reserved1=1),
            lambda value: value.update(capture_label=True),
            lambda value: value.update(owner_mask=1),
            lambda value: value["counts"].pop(),
            lambda value: value["counts"][2].pop(),
            lambda value: value["counts"][2].__setitem__(1, -1),
            lambda value: value["counts"][2].__setitem__(1, 1 << 64),
            lambda value: value["counts"][2].__setitem__(1, False),
            lambda value: value["counts"][0].__setitem__(1, 1),
        )
        for index, mutate in enumerate(mutations):
            with self.subTest(mutation=index), self.assertRaises(ValueError):
                parse_native_output(self.transcript(mutate), Stage.GREEN, "aggregate")

    def test_record_order_duplicates_and_component_contamination_are_rejected(self):
        from tests.host.selected_ownership_evidence import parse_native_output

        lines = self.transcript().splitlines(keepends=True)
        for index, changed in enumerate((
            [lines[1], lines[0], *lines[2:]],
            [lines[0], *lines[2:]],
            [lines[0], lines[1], lines[1], *lines[2:]],
            [lines[0], lines[0], *lines[1:]],
            [*lines[:-2], lines[-1]],
            [*lines[:-1], lines[-2], lines[-1]],
        )):
            with self.subTest(order=index), self.assertRaises(ValueError):
                parse_native_output("".join(changed), Stage.GREEN, "aggregate")
        original = self.transcript()
        producer = source.STATE_WORK_CONTRACT["producer_base"] + source.STATE_WORK_CONTRACT["current_producer_offset"] + 1
        component = original.replace(str(producer), str(producer + 1))
        parse_native_output(component, Stage.GREEN, "active-frame")
        lines = component.splitlines(keepends=True)
        record = json.loads(lines[1].removeprefix("MICROS_MEM03_WORK "))
        record["counts"][2][4] = 1
        lines[1] = "MICROS_MEM03_WORK " + json.dumps(record) + "\n"
        with self.assertRaises(ValueError):
            parse_native_output("".join(lines), Stage.GREEN, "active-frame")


if __name__ == "__main__":
    unittest.main()

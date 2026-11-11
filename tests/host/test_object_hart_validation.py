from copy import deepcopy
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from tests.host.object_hart_validation_evidence import (
    check_semantics, expected_cases, parse_native_output, producer_label,
)
from tests.host.object_hart_validation_source import (
    observe_current_source, project_observations,
)
from tests.host.common_scheduler_return_source import validate_current_native_source
from tests.host.work_count_types import Kind, Owner


ROOT = Path(__file__).resolve().parents[2]


def semantic_rows():
    return tuple({
        **row, "version": 1, "preserved": 1, "outputs_preserved": 1,
        "before_state": 1, "after_state": 1, "before_output": 2, "after_output": 2,
        "value": row["harts"] - 1 if row["variant"] == "last-hart"
        else int(row["variant"] == "assigned-current") if row["operation"] == "is-current"
        else (1 << 64) - 1,
    } for row in expected_cases("current"))


class ObjectHartCurrentContractTest(unittest.TestCase):
    def test_current_audit_boundaries_need_no_historical_source(self):
        from tests.host import object_hart_validation_source as source

        with patch.object(source, "fixed_audit_source", side_effect=AssertionError("historical audit source"), create=True):
            measured = source.measure_audit_boundaries(ROOT, "current")
        self.assertEqual(26, len(measured["retained"]))
        self.assertEqual((), measured["migrated"])
        self.assertEqual(1, measured["base_edge"])

    def test_current_audit_census_rejects_removed_or_added_complete_calls(self):
        from tests.host import object_hart_validation_source as source

        paths = {*source.AUDIT_SOURCES, "kernel/scheduler/scheduler_core_validate.c"}
        originals = {path: (ROOT / path).read_bytes() for path in paths}
        source.measure_audit_boundaries(ROOT, "current", current_sources=originals)
        path = "kernel/objects/kernel_objects.c"
        for changed in (
            originals[path].replace(b"micros_scheduler_core_validate(", b"unqualified_validate(", 1),
            originals[path] + b"\nvoid extra(void) { micros_scheduler_core_validate(0); }\n",
        ):
            with self.assertRaises(ValueError):
                source.measure_audit_boundaries(ROOT, "current", current_sources={**originals, path: changed})
        path = "kernel/scheduler/scheduler_core_validate.c"
        with self.assertRaises(ValueError):
            source.measure_audit_boundaries(ROOT, "current", current_sources={
                **originals, path: originals[path].replace(
                    b"micros_kernel_objects_validate_base(", b"unqualified_validate(", 1,
                ),
            })

    def test_current_reference_rejects_changed_inputs_outputs_and_effects(self):
        from tests.host.object_hart_validation_evidence import check_current_reference

        expected = json.loads((ROOT / "tests/host/object_hart_reference.json").read_text())
        records, plans = [], []
        for case, before, after, before_output, after_output, value, has_effect, effect in expected["records"]:
            records.append({
                "case": case, "before_state": before, "after_state": after,
                "before_output": before_output ^ 17, "after_output": after_output ^ 17,
                "value": value,
            })
            plans.append({"case": case, "before": 17, "after": 17, "has_effect": has_effect, "effect": effect})
        check_current_reference(ROOT, records, plans)
        for field in ("before_state", "after_state", "before_output", "after_output", "value"):
            changed = deepcopy(records)
            changed[0][field] ^= 1
            with self.subTest(field=field), self.assertRaises(ValueError):
                check_current_reference(ROOT, changed, plans)
        changed = deepcopy(plans)
        changed[-1]["effect"] ^= 1
        with self.assertRaises(ValueError):
            check_current_reference(ROOT, records, changed)
        with self.assertRaises(ValueError):
            check_current_reference(ROOT, records[:-1], plans[:-1])

    def test_current_semantics_do_not_consult_a_retirement_claim(self):
        from tests.host import common_scheduler_return_source as current

        with patch.object(
            current, "_current_authority",
            side_effect=AssertionError("current semantics consulted historical authority"), create=True,
        ):
            check_semantics(semantic_rows(), "current")

    def test_current_object_input_needs_no_historical_source(self):
        path = "kernel/objects/kernel_object_hart.c"
        text = "/* Current object input. */\n" + (ROOT / path).read_text()
        validate_current_native_source(path, text)

    def test_current_observations_reject_added_loops_and_changed_geometric_comparisons(self):
        from tests.host.work_count_lexical import function_spans

        path = "kernel/objects/kernel_object_validation.c"
        original = (ROOT / path).read_text()
        start = function_spans(original)["kernel_object_validation_stack_accumulate"].body_start
        altered = original.replace("base != geometry->bases[cohort]", "base == geometry->bases[cohort]", 1)
        self.assertNotEqual(original, altered)
        for changed in (
            altered,
            original[:start] + "\nfor (size_t hidden = 0; hidden < 128; ++hidden) {}\n" + original[start:],
        ):
            with self.assertRaises(ValueError):
                observe_current_source(path, changed, "stack")

    def test_current_observations_are_reversible_without_relocation(self):
        path = "kernel/objects/kernel_object_validation.c"
        original = (ROOT / path).read_text()
        observed = observe_current_source(path, original, "aggregate")
        self.assertEqual(original, project_observations(
            ROOT, path, observed, "aggregate", "current", original=original,
        ))
        first = observed.index("MICROS_TEST_WORK_HIT(")
        end = observed.index(";", first) + 1
        annotation = observed[first:end]
        for changed in (
            observed[:first] + observed[end:],
            observed[:first] + observed[end:] + "\n" + annotation,
            observed.replace("MICROS_WORK_KIND_INDEX_PROBE", "MICROS_WORK_KIND_RECORD_VISIT", 1),
        ):
            self.assertNotEqual(observed, changed)
            with self.assertRaises(ValueError):
                project_observations(ROOT, path, changed, "aggregate", "current", original=original)


class ObjectHartNativeProtocolTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.semantic_rows = semantic_rows()
        row = cls.semantic_rows[0]
        snapshot = {
            "version": 1, "reserved0": 0, "producer": producer_label("current", "aggregate"),
            "case": 1, "owner_mask": 24, "reserved1": 0, "counts": [[0] * len(Kind) for _ in Owner],
        }
        case = "MICROS_OBJ01_CASE " + json.dumps(row) + "\n"
        plan = "MICROS_OBJ01_PLAN " + json.dumps({
            "version": 1, "case": 1, "bytes": 220, "before": 4, "after": 4,
            "has_effect": 0, "effect": 0,
        }) + "\n"
        cls.ordinary_output = case + plan + "MICROS_OBJ01_NATIVE_PASS cases=1\n"
        cls.work_output = case + plan + "MICROS_OBJ01_WORK " + json.dumps(snapshot) + "\nMICROS_OBJ01_NATIVE_PASS cases=1\n"

    def test_partial_or_misattributed_work_cannot_supply_a_complete_native_product(self):
        output = self.work_output
        rows, _, _ = parse_native_output(output, "aggregate", "current")
        with self.assertRaises(ValueError):
            check_semantics(rows, "current")
        lines = output.splitlines(keepends=True)
        work_index = next(index for index, line in enumerate(lines) if line.startswith("MICROS_OBJ01_WORK "))
        original = json.loads(lines[work_index].removeprefix("MICROS_OBJ01_WORK "))
        mutations = (
            ("producer", 2), ("case", 2), ("reserved0", 1), ("reserved1", 1),
            ("owner_mask", 1 << Owner.KERNEL_OBJECTS), ("version", True),
            ("counts", original["counts"][:-1]),
        )
        for field, value in mutations:
            with self.subTest(field=field):
                mutated = deepcopy(original)
                mutated[field] = value
                changed = list(lines)
                changed[work_index] = "MICROS_OBJ01_WORK " + json.dumps(mutated) + "\n"
                with self.assertRaises(ValueError):
                    parse_native_output("".join(changed), "aggregate", "current")
        for changed in (
            "".join(lines[:work_index] + lines[work_index + 1:]),
            "".join(lines[:work_index] + [lines[work_index]] + lines[work_index:]),
            "".join(line for line in lines if not line.startswith("MICROS_OBJ01_PLAN ")),
            output[:-1], self.ordinary_output,
        ):
            with self.assertRaises(ValueError):
                parse_native_output(changed, "aggregate", "current")

    def test_semantic_corpus_rejects_missing_cases_and_changed_failure_state(self):
        rows = self.semantic_rows
        check_semantics(rows, "current")
        with self.assertRaises(ValueError):
            check_semantics(rows[:-1], "current")
        for field, value in (("after_state", 0), ("outputs_preserved", 0), ("result", 0)):
            changed = deepcopy(rows)
            index = next(index for index, row in enumerate(changed) if row["result"] == 8)
            changed[index][field] = value
            with self.assertRaises(ValueError):
                check_semantics(changed, "current")


class ObjectHartLauncherTest(unittest.TestCase):
    def test_actual_launcher_closes_python_prefix_root_and_preexec_environment(self):
        from tests.host.c_source_records import function_records
        from tests.host.runtime_memory_evidence import native_compiler

        record = function_records((ROOT / "tests/host/object_hart_validation_test.c").read_text())["launch_native_products"]
        compiler = native_compiler(ROOT)
        with tempfile.TemporaryDirectory(prefix="micros-obj01-launcher-") as directory:
            directory = Path(directory).resolve()
            child, launcher = directory / "child", directory / "launcher"
            child_source = directory / "child.c"
            child_source.write_text(
                "#include <stdio.h>\n#include <unistd.h>\n"
                "int main(int argc, char **argv) {\n"
                "    char path[4096]; if (getcwd(path, sizeof(path)) == NULL) return 3;\n"
                "    puts(path); for (int index = 1; index < argc; ++index) puts(argv[index]); return 0;\n}\n"
            )
            source = directory / "launcher.c"
            source.write_text(
                "#include <stdio.h>\n#include <stdlib.h>\n#include <unistd.h>\n"
                "#define MICROS_OBJECT_HART_VALIDATION_PYTHON " + json.dumps(str(child)) + "\n"
                "#define MICROS_OBJECT_HART_VALIDATION_DRIVER " + json.dumps(str(ROOT / "tools/check_object_hart_validation.py")) + "\n"
                "#define MICROS_OBJECT_HART_VALIDATION_ROOT " + json.dumps(str(ROOT)) + "\n"
                + record.signature + "{" + record.body + "}\n"
                "int main(int argc, char **argv) { return argc == 3 ? launch_native_products(argc, argv) : 2; }\n"
            )
            for input_path, output_path in ((child_source, child), (source, launcher)):
                completed = subprocess.run(
                    (compiler["path"], "-std=c17", "-Wall", "-Wextra", "-Werror", str(input_path), "-o", str(output_path)),
                    capture_output=True, text=True, timeout=30,
                )
                self.assertEqual((0, ""), (completed.returncode, completed.stderr), completed.stdout)
            products = ("current-aggregate", "current-stack")
            environment = dict(os.environ)
            environment.pop("__PYVENV_LAUNCHER__", None)
            completed = subprocess.run(
                (str(launcher), *products), cwd=directory, env=environment,
                capture_output=True, text=True, timeout=30,
            )
            expected = [str(ROOT), "-E", "-S", "-B", str(ROOT / "tools/check_object_hart_validation.py"), "--native"]
            for product in (*products, str(launcher)):
                expected.extend(("--executable", product))
            self.assertEqual((0, ""), (completed.returncode, completed.stderr))
            self.assertEqual(expected, completed.stdout.splitlines())
            completed = subprocess.run(
                (str(launcher), *products), cwd=directory, env={**environment, "__PYVENV_LAUNCHER__": "unsupported"},
                capture_output=True, text=True, timeout=30,
            )
            self.assertEqual((2, ""), (completed.returncode, completed.stdout))
            self.assertEqual("__PYVENV_LAUNCHER__ is unsupported\n", completed.stderr)


if __name__ == "__main__":
    unittest.main()

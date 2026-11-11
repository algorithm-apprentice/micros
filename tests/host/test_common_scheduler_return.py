import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tests.host.common_scheduler_return_evidence import (
    compare_plan_observations,
    validate_plan_observations,
)

ROOT = Path(__file__).resolve().parents[2]


class LegacyPlanObservationTest(unittest.TestCase):
    @staticmethod
    def product(plan_bytes, before_plan, after_plan, *, result=0):
        row = {
            "case": 1, "operation": "return-plan", "variant": "valid",
            "result": result, "diagnostic": 0, "preserved": 1,
            "outputs_preserved": int(result != 0),
            "before_state": 101, "after_state": 101,
            "before_output": 37 ^ before_plan,
            "after_output": 37 ^ after_plan,
        }
        plan = {
            "version": 1, "case": 1, "bytes": plan_bytes,
            "before": before_plan, "after": after_plan,
            "has_effect": int(result == 0),
            "effect": 73 if result == 0 else 0,
        }
        return (row,), (plan,)

    def test_different_plan_abis_keep_every_nonplan_and_effect_comparison(self):
        before, old_plan = self.product(1344, 17, 19)
        after, new_plan = self.product(264, 29, 31)
        compare_plan_observations(before, after, old_plan, new_plan)
        for field in ("before_output", "after_output", "before_state", "after_state"):
            with self.subTest(field=field):
                changed = ({**after[0], field: after[0][field] ^ 1},)
                with self.assertRaises(ValueError):
                    compare_plan_observations(before, changed, old_plan, new_plan)
        with self.assertRaises(ValueError):
            compare_plan_observations(
                before, after, old_plan, ({**new_plan[0], "effect": 74},),
            )

    def test_side_records_cannot_omit_duplicate_or_borrow_case_identity(self):
        rows, plans = self.product(264, 29, 31)
        validate_plan_observations(rows, plans)
        for changed in (
            (), (*plans, plans[0]),
            ({**plans[0], "case": 2},),
            ({**plans[0], "version": 2},),
            ({**plans[0], "has_effect": 0},),
            ({**plans[0], "bytes": True},),
            ({**plans[0], "before": -1},),
            ({**plans[0], "extra": 0},),
        ):
            with self.subTest(plans=changed), self.assertRaises(ValueError):
                validate_plan_observations(rows, changed)

    def test_failure_checks_actual_plan_bytes_not_only_preservation_boolean(self):
        rows, plans = self.product(264, 29, 29, result=15)
        validate_plan_observations(rows, plans)
        with self.assertRaises(ValueError):
            validate_plan_observations(rows, ({**plans[0], "after": 31},))
        with self.assertRaises(ValueError):
            validate_plan_observations(rows, ({**plans[0], "effect": 73},))
        untouched = ({**rows[0], "operation": "hold", "result": 0},)
        validate_plan_observations(untouched, plans)
        with self.assertRaises(ValueError):
            validate_plan_observations(untouched, ({**plans[0], "after": 31},))

class CommonReturnFixtureTest(unittest.TestCase):
    def test_current_header_admission_does_not_restore_history_or_freeze_comments(self):
        from tests.host.common_scheduler_return_source import validate_current_native_source

        path = "include/micros/scheduler_core.h"
        text = (ROOT / path).read_text() + "\n/* Current declarations are unchanged. */\n"
        validate_current_native_source(path, text)

    def test_current_primitive_admission_uses_no_historical_source(self):
        from tests.host.common_scheduler_return_source import PLAN_PATH, validate_current_native_source

        text = "/* Current measured algorithm. */\n" + (ROOT / PLAN_PATH).read_text()
        validate_current_native_source(PLAN_PATH, text)

    def test_current_return_inputs_reject_hidden_state_and_header_bodies(self):
        from tests.host.common_scheduler_return_source import PLAN_PATH, validate_current_native_source
        from tests.host.work_count_lexical import function_spans

        text = (ROOT / PLAN_PATH).read_text()
        start = function_spans(text)["clear_return_candidate"].body_start
        header = "include/micros/scheduler_core.h"
        mutations = (
            (PLAN_PATH, "static unsigned int hidden[128];\n" + text),
            (PLAN_PATH, text[:start] + "\nstatic unsigned int hidden;\n" + text[start:]),
            (header, (ROOT / header).read_text() + "\nstatic inline int hidden(void) { return 1; }\n"),
        )
        for path, changed in mutations:
            with self.subTest(path=path), self.assertRaises(ValueError):
                validate_current_native_source(path, changed)

    def test_current_return_cannot_hide_a_new_loop_without_primitive_stores(self):
        from tests.host.common_scheduler_return_source import PLAN_PATH, validate_current_native_source
        from tests.host.work_count_lexical import function_spans

        text = (ROOT / PLAN_PATH).read_text()
        start = function_spans(text)["clear_return_candidate"].body_start
        changed = text[:start] + "\nfor (size_t hidden = 0; hidden < 128; ++hidden) {}\n" + text[start:]
        with self.assertRaisesRegex(ValueError, "loop"):
            validate_current_native_source(PLAN_PATH, changed)

class ContextCaptureProtocolTest(unittest.TestCase):
    @staticmethod
    def transcript(baseline=False):
        from tests.host.common_scheduler_return_evidence import TARGET_COUNT_FIELDS, TARGET_MARKERS
        from tests.host.work_count_types import Kind, Owner

        def line(name, fields):
            return "# " + name + "".join(f" {key}=0x{value:016x}" for key, value in fields.items()) + "\n"

        output = ""
        for index in range(3):
            output += line("MICROS_OBJ02_CONTEXT", {
                "class": index, "version": 1, "reserved0": 0, "producer": 0x4f424a3032540001,
                "capture": index + 1, "owner_mask": 1 << Owner.KERNEL_EXECUTION, "reserved1": 0,
                "mode": 1 if index != 2 else 2, "outgoing_slot": 0,
                "outgoing_generation": int(index != 2), "selected_slot": int(index != 0),
                "selected_generation": 1,
            })
            for owner in Owner:
                output += line("MICROS_OBJ02_CONTEXT_COUNTS", {
                    "capture": index + 1, "owner": int(owner),
                    **{name: (2 if index == 1 or baseline and index == 0 else 1)
                       if owner is Owner.KERNEL_EXECUTION and kind is Kind.COMPLETE_VALIDATOR_CALL else 0
                       for name, kind in zip(TARGET_COUNT_FIELDS, Kind, strict=True)},
                })
        output += line("MICROS_OBJ02_CONTEXT_END", {"valid": int(not baseline), "modes": 15, "captures": 7})
        return output + (
            "# MICROS_TEST_FAILURE scheduler-test\n" if baseline
            else "".join("# " + marker + "\n" for marker in TARGET_MARKERS)
        )

    def test_requires_complete_actual_mode_and_count_witnesses(self):
        from tests.host.common_scheduler_return_evidence import parse_target_output, target_failures

        current = parse_target_output(self.transcript(), "current", 0)
        self.assertEqual(target_failures(current), ())
        original = parse_target_output(self.transcript(True), "baseline", 1)
        self.assertEqual(target_failures(original), ((0, 2, 1),))
        invalid = (
            self.transcript().replace(" selected_generation=0x0000000000000001", "", 1),
            self.transcript().replace(" owner=0x0000000000000001", " owner=0x0000000000000000", 1),
            self.transcript().replace(" mode=0x0000000000000002", " mode=0x0000000000000001", 1),
            self.transcript().replace(" valid=0x0000000000000001", " valid=0x0000000000000000"),
            self.transcript() + "# MICROS_PANIC reason=unexpected\n",
            self.transcript().replace("# MICROS_TTY_TRAP_TEST_PASS", "# OMITTED_TTY_TRAP_TEST_PASS"),
        )
        for text in invalid:
            with self.subTest(text=text[-160:]), self.assertRaises(ValueError):
                parse_target_output(text, "current", 0)
        with self.assertRaises(ValueError):
            parse_target_output(self.transcript(True), "baseline", 0)
        with self.assertRaises(ValueError):
            parse_target_output(self.transcript(True).replace(
                "# MICROS_TEST_FAILURE scheduler-test", "# MICROS_TEST_FAILURE other-test",
            ), "baseline", 1)

    def test_current_hooks_are_passive_and_close_before_timer_work(self):
        from tests.host.common_scheduler_return_source import (
            TARGET_PATHS, extend_target_observations, project_target_observations,
        )

        for path in TARGET_PATHS:
            observed = (ROOT / path).read_text()
            original = project_target_observations(path, observed)
            self.assertEqual(observed, extend_target_observations(path, original))
            if "MICROS_OBJ02_RETURN_END(plan.selected);" in observed:
                removed = observed.replace("MICROS_OBJ02_RETURN_END(plan.selected);", "(void)0;", 1)
                with self.subTest(path=path), self.assertRaises(ValueError):
                    project_target_observations(path, removed)
                late = observed.replace(
                    "MICROS_OBJ02_RETURN_END(plan.selected);", "MICROS_OBJ02_RETURN_CANCEL();", 1,
                )
                with self.subTest(path=path), self.assertRaises(ValueError):
                    project_target_observations(path, late)


class CommonReturnBoundaryTest(unittest.TestCase):
    def test_scheduler_success_keeps_the_actual_context_transcript(self):
        from tests.host.build_source_records import cmake_command

        command = cmake_command((ROOT / "CMakeLists.txt").read_text(), "add_custom_target", "test-qemu-scheduler")
        self.assertEqual(1, command.count("--report-transcript"))

    def test_finish_report_keeps_its_current_stack_observer_hook(self):
        from tests.host.stack_source_projection import project_stack_source

        path = "kernel/tests/scheduler/scheduler_test_finish.c"
        actual = (ROOT / path).read_text()
        self.assertIn("micros_obj02_return_report()", project_stack_source(path, actual))
        with self.assertRaises(AssertionError):
            project_stack_source(path, actual.replace("MICROS_TEST_STACK_REPORT();", "", 1))

    def test_current_bootstrap_callers_compile_against_the_private_header(self):
        with tempfile.TemporaryDirectory(prefix="micros-obj02-callers-") as directory:
            directory = Path(directory)
            path = "kernel/scheduler/scheduler_runtime_internal.h"
            header = directory / path
            header.parent.mkdir(parents=True)
            header.write_bytes((ROOT / path).read_bytes())
            for profile in (
                "MICROS_BUILD_USER_EXECUTION_TEST", "MICROS_BUILD_ADDRESS_SPACE_HANDOFF_TEST",
                "MICROS_BUILD_IPC_ECALL_CORE_TEST", "MICROS_BUILD_GRANT_SYSCALL_TEST",
            ):
                completed = subprocess.run(
                    (os.environ["MICROS_CLANG"], "--target=riscv64-unknown-elf",
                     "-march=rv64imac_zicsr_zifencei", "-mabi=lp64", "-std=c17",
                     "-ffreestanding", "-fno-builtin", "-Wall", "-Wextra", "-Werror",
                     "-fsyntax-only", "-D" + profile + "=1",
                     "-I", str(directory), "-I", str(ROOT), "-I", str(ROOT / "include"),
                     str(ROOT / "kernel/tests/scheduler/scheduler_test_support.c")),
                    capture_output=True, text=True, timeout=30,
                )
                with self.subTest(profile=profile):
                    self.assertEqual((0, ""), (completed.returncode, completed.stderr), completed.stdout)


class CommonReturnLauncherTest(unittest.TestCase):
    def test_actual_launcher_preserves_prefix_and_rejects_consumed_environment(self):
        from tests.host.c_source_records import function_records
        from tests.host.runtime_memory_evidence import native_compiler

        record = function_records((ROOT / "tests/host/common_scheduler_return_test.c").read_text())["launch_native_products"]
        compiler = native_compiler(ROOT)
        with tempfile.TemporaryDirectory(prefix="micros-obj02-launcher-") as directory:
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
                "#define MICROS_COMMON_SCHEDULER_RETURN_PYTHON " + json.dumps(str(child)) + "\n"
                "#define MICROS_COMMON_SCHEDULER_RETURN_DRIVER " + json.dumps(str(ROOT / "tools/check_common_scheduler_return.py")) + "\n"
                "#define MICROS_COMMON_SCHEDULER_RETURN_ROOT " + json.dumps(str(ROOT)) + "\n"
                + record.signature + "{" + record.body + "}\n"
                "int main(int argc, char **argv) { return argc == 2 ? launch_native_products(argv) : 2; }\n"
            )
            for input_path, output_path in ((child_source, child), (source, launcher)):
                completed = subprocess.run(
                    (compiler["path"], "-std=c17", "-Wall", "-Wextra", "-Werror", str(input_path), "-o", str(output_path)),
                    capture_output=True, text=True, timeout=30,
                )
                self.assertEqual((0, ""), (completed.returncode, completed.stderr), completed.stdout)
            products = ("current-ordinary",)
            environment = dict(os.environ)
            environment.pop("__PYVENV_LAUNCHER__", None)
            completed = subprocess.run(
                (str(launcher), *products), cwd=directory, env=environment,
                capture_output=True, text=True, timeout=30,
            )
            expected = [str(ROOT), "-E", "-S", "-B", str(ROOT / "tools/check_common_scheduler_return.py"), "--native"]
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

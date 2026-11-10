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

    def test_actual_fixed_legacy_fixture_emits_every_raw_plan_observation(self):
        from tests.host.object_hart_validation_evidence import measure_native_channel

        measured = measure_native_channel(ROOT, "baseline", "ordinary")
        self.assertEqual(len(measured["records"]), 171)
        validate_plan_observations(measured["records"], measured["plans"])
        self.assertTrue(any(row["has_effect"] for row in measured["plans"]))


class CommonReturnFixtureTest(unittest.TestCase):
    def test_fixed_source_executes_the_complete_independent_queue_model(self):
        from tests.host.common_scheduler_return_evidence import (
            check_native_semantics,
            measure_native_channel,
        )

        measured = measure_native_channel(ROOT, "baseline", "ordinary")
        check_native_semantics(measured["records"], "baseline")
        self.assertEqual(measured["work"], ())

    def test_fixed_source_exposes_real_projection_shift_and_apply_work(self):
        from tests.host.common_scheduler_return_evidence import measure_native_channel, native_work_failures

        measured = measure_native_channel(ROOT, "baseline", "work")
        failures = native_work_failures(measured)
        self.assertTrue({"workspace", "plan-size", "copied-bytes", "zeroed-bytes", "queue-shifts", "apply-stores"}
                        <= {row[1] for row in failures})


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

    def test_passive_hooks_preserve_fixed_source_and_close_before_timer_work(self):
        from tests.host.common_scheduler_return_source import (
            TARGET_PATHS, extend_target_observations, fixed_source, project_target_observations,
        )

        for path in TARGET_PATHS:
            original = fixed_source(str(ROOT), path)
            observed = extend_target_observations(path, original)
            self.assertEqual(original, project_target_observations(path, observed))
            if "MICROS_OBJ02_RETURN_END(plan.selected);" in observed:
                removed = observed.replace("MICROS_OBJ02_RETURN_END(plan.selected);", "(void)0;", 1)
                with self.subTest(path=path), self.assertRaises(ValueError):
                    project_target_observations(path, removed)
                late = observed.replace(
                    "MICROS_OBJ02_RETURN_END(plan.selected);", "MICROS_OBJ02_RETURN_CANCEL();", 1,
                )
                with self.subTest(path=path), self.assertRaises(ValueError):
                    project_target_observations(path, late)


class CommonReturnAuthorityTest(unittest.TestCase):
    def test_task_definition_reuses_one_complete_predecessor_resolution(self):
        from tests.host import common_scheduler_return_authority as authority
        from tests.host import object_hart_validation_authority as previous
        from tests.host.evidence_authority_types import TransitionClaim, TransitionRegistry

        claims = tuple(
            TransitionClaim("prior-" + str(index), "prior", (), (),
                            tuple("prior-key-" + str(index * 35 + key) for key in range(35)))
            for index in range(8)
        )
        with (
            patch.object(previous, "committed_claim", return_value=claims[-1]),
            patch.object(previous, "task_definition", return_value=(
                None, {}, None, TransitionRegistry(claims),
            )) as resolve,
        ):
            _, _, plan, registry = authority.task_definition(ROOT)
            self.assertEqual(58, len(plan.planned_retirement_identity_keys))
            self.assertEqual(9, len(registry.claims))
            self.assertEqual(1, resolve.call_count)

    def test_registered_source_scope_preserves_every_predecessor_and_exact_selected_record(self):
        from tests.host.common_scheduler_return_authority import predecessor_records
        from tests.host.common_scheduler_return_source import (
            NATIVE_FAMILY, SOURCE_PROJECTION, candidate_paths, native_assertion_records,
        )
        from tests.host.evidence_authority_inventory import current_inventory
        from tests.host.evidence_authority_types import fact_set_digest

        inventory = current_inventory(ROOT)
        prior = predecessor_records(ROOT, inventory)
        selected = tuple(fact for fact in prior if fact.key in SOURCE_PROJECTION["retirement_keys"])
        self.assertEqual(SOURCE_PROJECTION["affected_facts_sha256"], fact_set_digest(selected))
        self.assertEqual(set(candidate_paths()), {path for fact in selected for path in (fact.owner_path, *fact.payload_paths)})
        actual = tuple(fact for fact in inventory.facts if fact.family == NATIVE_FAMILY)
        self.assertEqual(len(native_assertion_records(ROOT)), len(actual))
        self.assertTrue(actual, "the current source predicates need real individual authority identities")

    def test_scaffold_or_foreign_process_cannot_retire_a_source_identity(self):
        from types import SimpleNamespace
        from tests.host import common_scheduler_return_authority as authority
        from tests.host.common_scheduler_return_source import SOURCE_PROJECTION

        with (
            patch.object(authority, "_provisional", None),
            patch.object(authority, "committed_claim", return_value=None),
            patch.object(authority, "read_current", return_value=None),
        ):
            self.assertEqual(frozenset(), authority.retired_identity_keys(ROOT))
            self.assertTrue(authority.identity_required(ROOT, SOURCE_PROJECTION["retirement_keys"][0]))
            with patch.object(authority, "_provisional", (os.getpid() + 1, SimpleNamespace(root=ROOT))):
                self.assertEqual(frozenset(), authority.retired_identity_keys(ROOT))

    def test_complete_recipe_and_adapters_preserve_the_fixed_predecessor(self):
        from tests.host import common_scheduler_return_source as source

        source.validate_scaffold(ROOT)
        original = source.fixed_source(str(ROOT), "CMakeLists.txt")
        current = (ROOT / "CMakeLists.txt").read_text()
        self.assertEqual(original, source.project_cmake(current))
        with self.assertRaises(ValueError):
            source.project_cmake(current.replace(
                "set_tests_properties(common-scheduler-return PROPERTIES LABELS slow)",
                "set_tests_properties(common-scheduler-return PROPERTIES LABELS fast)",
            ))
        for path in source.SOURCE_ROLES["fixed_adapters"]:
            fixed = source.fixed_source(str(ROOT), path)
            actual = (ROOT / path).read_text()
            self.assertEqual(fixed, source.project_legacy_adapter(ROOT, path, actual))
            if source._legacy_changes(path):
                with self.subTest(path=path), self.assertRaises(ValueError):
                    source.project_legacy_adapter(ROOT, path, actual + "\n")

    def test_scheduler_success_keeps_the_actual_context_transcript(self):
        from tests.host.test_scheduler_source_layout import cmake_command

        command = cmake_command((ROOT / "CMakeLists.txt").read_text(), "add_custom_target", "test-qemu-scheduler")
        self.assertEqual(1, command.count("--report-transcript"))

    def test_finish_report_preserves_the_original_stack_observer_slot(self):
        from tests.host.common_scheduler_return_source import fixed_source
        from tests.host.stack_source_projection import project_stack_source

        path = "kernel/tests/scheduler/scheduler_test_finish.c"
        actual = (ROOT / path).read_text()
        original = fixed_source(str(ROOT), path)
        self.assertEqual(original.splitlines()[1], actual.splitlines()[1])
        self.assertIn("micros_obj02_return_report()", project_stack_source(path, actual))
        lines = actual.splitlines(keepends=True)
        lines[1], lines[2] = lines[2], lines[1]
        with self.assertRaises(AssertionError):
            project_stack_source(path, "".join(lines))

    def test_unchanged_bootstrap_callers_compile_with_the_declared_private_header(self):
        from tests.host.common_scheduler_return_source import declared_header

        with tempfile.TemporaryDirectory(prefix="micros-obj02-callers-") as directory:
            directory = Path(directory)
            path = "kernel/scheduler/scheduler_runtime_internal.h"
            header = directory / path
            header.parent.mkdir(parents=True)
            header.write_text(declared_header(ROOT, path))
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
                "int main(int argc, char **argv) { return argc == 4 ? launch_native_products(argv) : 2; }\n"
            )
            for input_path, output_path in ((child_source, child), (source, launcher)):
                completed = subprocess.run(
                    (compiler["path"], "-std=c17", "-Wall", "-Wextra", "-Werror", str(input_path), "-o", str(output_path)),
                    capture_output=True, text=True, timeout=30,
                )
                self.assertEqual((0, ""), (completed.returncode, completed.stderr), completed.stdout)
            products = ("baseline-ordinary", "baseline-work", "current-ordinary")
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

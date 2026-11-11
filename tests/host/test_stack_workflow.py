"""Stack admission extends, rather than substitutes for, existing validation."""

from pathlib import Path
import tempfile
import unittest

from tools import validation_plan


ROOT = Path(__file__).resolve().parents[2]
COMMAND = ("cmake", "--workflow", "--preset", "test-stack-evidence")


class StackWorkflowTest(unittest.TestCase):
    def test_current_aggregate_does_not_freeze_unrelated_root_recipe_text(self):
        from tests.host.stack_recipe import MODULE_PATH, RECIPE, validate_stack_recipe

        with tempfile.TemporaryDirectory(prefix="micros-current-stack-recipe-") as temporary:
            root = Path(temporary)
            (root / "CMakeLists.txt").write_text(
                (ROOT / "CMakeLists.txt").read_text() + "\n# Unrelated current build documentation.\n",
            )
            module = root / MODULE_PATH
            module.parent.mkdir(parents=True)
            module.write_text(RECIPE)
            validate_stack_recipe(root)

    def test_full_and_affected_runtime_plans_require_one_stack_gate(self):
        full = validation_plan.plan([], "full")
        self.assertEqual(1, full.count(COMMAND))
        for workflow in (*validation_plan.QEMU_WORKFLOWS, *validation_plan.IMAGE_WORKFLOWS):
            self.assertIn(validation_plan.workflow(workflow), full)
        for path in (
            "arch/riscv64/trap.S", "servers/vm/vm_load.c",
            "servers/vfs/vfs_service.c", "lib/runtime/application.c",
        ):
            for tier in ("fast", "pr"):
                with self.subTest(path=path, tier=tier):
                    self.assertEqual(1, validation_plan.plan([path], tier).count(COMMAND))
        self.assertIn(COMMAND[-1], validation_plan.canonical_workflows())

    def test_documentation_and_existing_host_only_fast_loops_stay_narrow(self):
        for path in ("docs/testing-strategy.md", "tests/host/elf_spawn_fixture_test.c"):
            self.assertNotIn(COMMAND, validation_plan.plan([path], "fast"))



    def test_current_aggregate_keeps_host_ownership_and_the_real_checker(self):
        from tests.host.stack_recipe import INCLUDE, MODULE_PATH, RECIPE, validate_stack_recipe

        validate_stack_recipe(ROOT)
        source = (ROOT / "CMakeLists.txt").read_text()
        with tempfile.TemporaryDirectory(prefix="micros-stack-recipe-") as temporary:
            root = Path(temporary)
            (root / "CMakeLists.txt").write_text(source)
            module = root / MODULE_PATH
            module.parent.mkdir(parents=True)
            module.write_text(RECIPE)
            validate_stack_recipe(root)
            for changed in (
                source.replace(INCLUDE, ""),
                INCLUDE + source.replace(INCLUDE, ""),
                source + INCLUDE,
            ):
                (root / "CMakeLists.txt").write_text(changed)
                with self.assertRaises(AssertionError):
                    validate_stack_recipe(root)
            (root / "CMakeLists.txt").write_text(source)
            module.write_text(RECIPE.replace("check_stack_evidence.py", "check_docs.py"))
            with self.assertRaises(AssertionError):
                validate_stack_recipe(root)

    def test_workflow_build_steps_cannot_select_a_subset_or_other_target(self):
        import json
        from tests.host.stack_recipe import MODULE_PATH, RECIPE, validate_stack_workflow

        validate_stack_workflow(ROOT)
        with tempfile.TemporaryDirectory(prefix="micros-stack-workflow-") as temporary:
            root = Path(temporary)
            (root / "CMakeLists.txt").write_text((ROOT / "CMakeLists.txt").read_text())
            module = root / MODULE_PATH
            module.parent.mkdir(parents=True)
            module.write_text(RECIPE)
            (root / "docs").mkdir()
            (root / "docs/testing-strategy.md").write_text("cmake --workflow --preset test-stack-evidence\n")
            presets = json.loads((ROOT / "CMakePresets.json").read_text())
            (root / "CMakePresets.json").write_text(json.dumps(presets))
            validate_stack_workflow(root)
            gate, = (row for row in presets["buildPresets"] if row["name"] == COMMAND[-1])
            gate["targets"] = ["test-unit"]
            (root / "CMakePresets.json").write_text(json.dumps(presets))
            with self.assertRaises(ValueError):
                validate_stack_workflow(root)


if __name__ == "__main__":
    unittest.main()

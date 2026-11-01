"""Stack admission extends, rather than substitutes for, existing validation."""

from pathlib import Path
import tempfile
import unittest

from tools import validation_plan


ROOT = Path(__file__).resolve().parents[2]
COMMAND = ("cmake", "--workflow", "--preset", "test-stack-evidence")


class StackWorkflowTest(unittest.TestCase):
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

    def test_projection_retains_old_commands_and_rejects_duplicate_or_relocated_gates(self):
        from tests.host.stack_recipe import project_command_extension

        commands = validation_plan.plan([], "full")
        original = project_command_extension(commands)
        self.assertEqual(len(commands) - 1, len(original))
        self.assertEqual(commands[-3:], original[-3:])
        self.assertIn(validation_plan.UNIT_FULL, original)
        for changed in (commands + [COMMAND], [COMMAND] + original):
            with self.assertRaises(AssertionError):
                project_command_extension(changed)

    def test_preset_projection_keeps_existing_order_and_cannot_erase_a_changed_gate(self):
        import json
        from tests.host.stack_recipe import project_presets

        source = (ROOT / "CMakePresets.json").read_text()
        original = json.loads(project_presets(source))
        current = json.loads(source)
        self.assertEqual(original["buildPresets"], current["buildPresets"][:-1])
        self.assertEqual(original["workflowPresets"], current["workflowPresets"][:-1])
        with self.assertRaises(AssertionError):
            project_presets(source.replace(
                '"targets": [\n        "test-stack-evidence"\n',
                '"targets": [\n        "test-unit"\n',
            ))

    def test_only_the_closed_host_extension_can_be_projected(self):
        from tests.host.stack_recipe import INCLUDE, MODULE_PATH, RECIPE, project_root_recipe, validate_stack_recipe

        validate_stack_recipe(ROOT)
        source = (ROOT / "CMakeLists.txt").read_text()
        self.assertNotIn(INCLUDE, project_root_recipe(source))
        with self.assertRaises(AssertionError):
            project_root_recipe(INCLUDE + source.replace(INCLUDE, ""))
        with tempfile.TemporaryDirectory(prefix="micros-stack-recipe-") as temporary:
            root = Path(temporary)
            (root / "CMakeLists.txt").write_text(source)
            module = root / MODULE_PATH
            module.parent.mkdir(parents=True)
            module.write_text(RECIPE)
            validate_stack_recipe(root)
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

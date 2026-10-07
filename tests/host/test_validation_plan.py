import unittest
from unittest import mock

from tools import validation_plan


class ValidationPlanTests(unittest.TestCase):
    def assert_workflow_selected(self, path, expected):
        commands = validation_plan.plan([path], "pr")
        self.assertIn(validation_plan.workflow(expected), commands)

    def test_docs_only_avoids_builds_in_both_tiers(self):
        expected = [
            validation_plan.DOCS,
            *validation_plan.diff_commands("origin/main"),
        ]
        self.assertEqual(
            expected,
            validation_plan.plan(["docs/testing-strategy.md"], "fast"),
        )
        self.assertEqual(
            expected,
            validation_plan.plan(["docs/testing-strategy.md"], "pr"),
        )

    def test_fast_ipc_runs_one_component(self):
        commands = validation_plan.plan(["kernel/ipc.c"], "fast")
        self.assertEqual(validation_plan.UNIT_FAST, commands[0])
        self.assertIn(validation_plan.IPC_MODEL, commands)
        self.assertIn(
            validation_plan.workflow("test-qemu-ipc"),
            commands,
        )

    def test_fast_full_escalation_runs_complete_qemu(self):
        commands = validation_plan.plan(
            ["kernel/unclassified_target_code.c"],
            "fast",
        )
        self.assertEqual(validation_plan.UNIT_FULL, commands[0])
        for name in validation_plan.QEMU_WORKFLOWS:
            self.assertIn(validation_plan.workflow(name), commands)

    def test_pr_endpoint_uses_complete_native_and_ipc(self):
        commands = validation_plan.plan(["kernel/endpoint.c"], "pr")
        self.assertEqual(validation_plan.UNIT_FULL, commands[0])
        self.assertNotIn(validation_plan.IPC_MODEL, commands)
        self.assertIn(
            validation_plan.workflow("test-qemu-endpoint"),
            commands,
        )
        self.assertIn(
            validation_plan.workflow("test-qemu-ipc"),
            commands,
        )

    def test_shared_harness_runs_full_qemu_matrix(self):
        commands = validation_plan.plan(
            ["tools/run_qemu_smoke.py"],
            "pr",
        )
        for name in validation_plan.QEMU_WORKFLOWS:
            self.assertIn(validation_plan.workflow(name), commands)

    def test_unmapped_and_mixed_target_paths_fail_closed(self):
        for paths in (
            ["kernel/unclassified_target_code.c"],
            ["kernel/ipc_transport.c"],
            ["kernel/ipc.c", "kernel/unclassified_target_code.c"],
        ):
            commands = validation_plan.plan(paths, "pr")
            for name in validation_plan.QEMU_WORKFLOWS:
                self.assertIn(validation_plan.workflow(name), commands)

    def test_ownership_entries_are_exact_unless_directory_scoped(self):
        self.assertTrue(
            validation_plan.path_matches(
                "include/micros/new_header.h",
                ("include/micros/",),
            )
        )
        self.assertFalse(
            validation_plan.path_matches(
                "kernel/ipc_transport.c",
                ("kernel/ipc.c",),
            )
        )

    def test_cross_gate_dependencies_are_selected(self):
        trap_commands = validation_plan.plan(["arch/riscv64/trap.S"], "pr")
        self.assertIn(
            validation_plan.workflow("test-qemu-nested-trap"),
            trap_commands,
        )
        scheduler_commands = validation_plan.plan(
            ["kernel/scheduler.c"],
            "pr",
        )
        self.assertIn(
            validation_plan.workflow(
                "test-qemu-scheduler-invalid-outgoing"
            ),
            scheduler_commands,
        )
        self.assertIn(
            validation_plan.workflow("test-qemu-scheduler-invalid-next"),
            scheduler_commands,
        )

    def test_toolchain_and_post_link_inputs_run_full_qemu(self):
        for path in (
            "cmake/toolchains/riscv64-clang.cmake",
            "tools/check_elf_sections.py",
        ):
            commands = validation_plan.plan([path], "pr")
            for name in validation_plan.QEMU_WORKFLOWS:
                self.assertIn(
                    validation_plan.workflow(name),
                    commands,
                )

    def test_shared_headers_and_ownership_runtime_run_full_qemu(self):
        for path in (
            "include/micros/scheduler.h",
            "include/micros/frame_ownership_runtime.h",
            "kernel/bootstrap_memory.c",
            "kernel/frame_ownership.c",
        ):
            commands = validation_plan.plan([path], "pr")
            for name in validation_plan.QEMU_WORKFLOWS:
                self.assertIn(
                    validation_plan.workflow(name),
                    commands,
                )

    def test_conditional_multi_image_sources_run_full_qemu(self):
        for path in (
            "kernel/trap.c",
            "kernel/scheduler.c",
            "kernel/timer.c",
            "kernel/user_execution.c",
            "include/micros/scheduler_core.h",
            "arch/riscv64/trap_context.h",
        ):
            commands = validation_plan.plan([path], "pr")
            for name in validation_plan.QEMU_WORKFLOWS:
                self.assertIn(
                    validation_plan.workflow(name),
                    commands,
                )

    def test_slow_model_entry_point_selects_slow_gate(self):
        commands = validation_plan.plan(
            ["tests/host/ipc_model_main.c"],
            "fast",
        )
        self.assertIn(validation_plan.IPC_MODEL, commands)

    def test_slow_model_shared_dependencies_select_slow_gate(self):
        for path in (
            "kernel/kernel_objects.c",
            "kernel/scheduler_core.c",
            "include/micros/kernel_objects.h",
            "include/micros/user_context.h",
        ):
            commands = validation_plan.plan([path], "fast")
            self.assertTrue(
                validation_plan.IPC_MODEL in commands
                or commands[0] == validation_plan.UNIT_FULL
            )

    def test_each_gate_has_target_specific_input(self):
        representatives = {
            "test-qemu-smoke": "kernel/fdt.c",
            "test-qemu-panic": "kernel/panic.c",
            "test-qemu-trap": "arch/riscv64/trap.S",
            "test-qemu-timer": "kernel/timer.c",
            "test-qemu-frame-allocator": "kernel/frame_allocator.c",
            "test-qemu-trap-panic": "arch/riscv64/trap_test.S",
            "test-qemu-mmu": "kernel/mmu_test.c",
            "test-qemu-object-model": "kernel/object_model_test.c",
            "test-qemu-endpoint": "kernel/endpoint_test.c",
            "test-qemu-ipc": "kernel/ipc_test.c",
            "test-qemu-nested-trap": "arch/riscv64/nested_trap_test.S",
            "test-qemu-frame-ownership": "kernel/frame_ownership_test.c",
            "test-qemu-user-address-space": "kernel/user_address_space_test.c",
            "test-qemu-user-execution": "kernel/user_execution_test.c",
            "test-qemu-scheduler": "kernel/scheduler_test.c",
            "test-qemu-scheduler-invalid-outgoing": (
                "kernel/scheduler_invalid_test.c"
            ),
            "test-qemu-scheduler-invalid-next": (
                "arch/riscv64/scheduler_test.S"
            ),
        }
        for expected, path in representatives.items():
            with self.subTest(workflow=expected, path=path):
                self.assert_workflow_selected(path, expected)

    def test_diff_checks_cover_index_worktree_and_branch(self):
        commands = validation_plan.plan(["kernel/ipc.c"], "fast", "upstream")
        self.assertEqual(
            list(validation_plan.diff_commands("upstream")),
            commands[-3:],
        )

    @mock.patch("tools.validation_plan.subprocess.run")
    def test_rename_discovery_keeps_both_paths(self, run):
        run.side_effect = (
            mock.Mock(stdout="docs/old.md\nkernel/new.c\n"),
            mock.Mock(stdout=""),
            mock.Mock(stdout=""),
            mock.Mock(stdout=""),
        )

        paths = validation_plan.changed_paths("origin/main", "/repo")

        self.assertEqual(["docs/old.md", "kernel/new.c"], paths)
        self.assertIn("--no-renames", run.call_args_list[0].args[0])
        self.assertEqual("/repo", run.call_args_list[0].kwargs["cwd"])

    def test_dot_relative_paths_are_normalized(self):
        commands = validation_plan.plan(["./kernel/ipc.c"], "pr")
        self.assertIn(
            validation_plan.workflow("test-qemu-ipc"),
            commands,
        )

    def test_explicit_paths_are_caller_relative(self):
        root = "/repo"
        self.assertEqual(
            "kernel/ipc.c",
            validation_plan.normalize_explicit_path(
                "./ipc.c",
                root,
                "/repo/kernel",
            ),
        )

    def test_explicit_paths_outside_planner_repository_are_rejected(self):
        with self.assertRaises(ValueError):
            validation_plan.normalize_explicit_path(
                "/other/kernel/ipc.c",
                "/repo",
                "/repo",
            )

    def test_explicit_paths_union_with_discovered_changes(self):
        self.assertEqual(
            ["docs/change.md", "kernel/ipc.c"],
            validation_plan.combine_paths(
                ["docs/change.md"],
                ["kernel/ipc.c"],
            ),
        )

    @mock.patch("tools.validation_plan.untracked_paths")
    def test_execute_rejects_untracked_files(self, untracked):
        untracked.return_value = ["new.c"]
        with self.assertRaises(ValueError):
            validation_plan.ensure_execution_ready("/repo")

    def test_authoritative_inventories_match_repository(self):
        validation_plan.validate_inventory(
            validation_plan.planner_root()
        )

    def test_full_runs_complete_matrix(self):
        commands = validation_plan.plan([], "full")
        self.assertEqual(validation_plan.UNIT_FULL, commands[0])
        self.assertEqual(
            len(validation_plan.QEMU_WORKFLOWS) + 5,
            len(commands),
        )


if __name__ == "__main__":
    unittest.main()

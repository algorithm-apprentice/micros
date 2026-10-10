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
        self.assertIn(
            validation_plan.workflow("test-qemu-ipc-syscall"),
            commands,
        )
        self.assertIn(
            validation_plan.workflow("test-qemu-ipc-syscall-panic"),
            commands,
        )

    def test_fast_tty_runs_persistent_model(self):
        commands = validation_plan.plan(
            ["servers/tty/tty_core.c"],
            "fast",
        )
        self.assertEqual(validation_plan.UNIT_FAST, commands[0])
        self.assertIn(validation_plan.TTY_MODEL, commands)
        self.assertIn(
            validation_plan.workflow("test-qemu-tty"),
            commands,
        )

    def test_fast_ramfs_runs_persistent_model(self):
        commands = validation_plan.plan(
            ["servers/ramfs/ramfs_core.c"],
            "fast",
        )
        self.assertEqual(validation_plan.UNIT_FAST, commands[0])
        self.assertIn(validation_plan.RAMFS_MODEL, commands)
        self.assertIn(
            validation_plan.workflow("test-qemu-ramfs"),
            commands,
        )

    def test_fast_vfs_runs_persistent_model(self):
        commands = validation_plan.plan(
            ["servers/vfs/vfs_core.c"],
            "fast",
        )
        self.assertEqual(validation_plan.UNIT_FAST, commands[0])
        self.assertIn(validation_plan.VFS_MODEL, commands)
        self.assertIn(
            validation_plan.workflow("test-qemu-vfs"),
            commands,
        )

    def test_syscall_production_paths_own_acceptance_gates(self):
        for path in (
            "kernel/ipc_abi.c",
            "kernel/ipc_syscall.c",
            "kernel/ipc.c",
            "kernel/kernel_objects.c",
            "kernel/address_space.c",
            "kernel/sv39.c",
            "kernel/user_address_space.c",
            "arch/riscv64/mmu.S",
        ):
            with self.subTest(path=path):
                commands = validation_plan.plan([path], "fast")
                self.assertIn(
                    validation_plan.workflow("test-qemu-ipc-syscall"),
                    commands,
                )
                self.assertIn(
                    validation_plan.workflow(
                        "test-qemu-ipc-syscall-panic"
                    ),
                    commands,
                )
                self.assertIn(
                    validation_plan.workflow(
                        "test-qemu-grant-syscall"
                    ),
                    commands,
                )
                self.assertIn(
                    validation_plan.workflow(
                        "test-qemu-address-space-handoff"
                    ),
                    commands,
                )

    def test_bootstrap_launcher_dependencies_select_gate(self):
        for path in (
            "arch/riscv64/sbi.c",
            "arch/riscv64/uart.c",
            "kernel/fdt.c",
            "kernel/frame_allocator.c",
            "kernel/ipc_abi.c",
            "kernel/user_address_space_core.c",
            "lib/runtime/memory.h",
            "lib/runtime/raw_syscall.h",
            "tests/qemu/bootstrap_launcher_control.c",
            "tests/qemu/bootstrap_launcher_control.h",
        ):
            with self.subTest(path=path):
                self.assert_workflow_selected(
                    path,
                    "test-qemu-bootstrap-launcher",
                )

    def test_bootstrap_fatal_gates_own_panic_paths(self):
        for path in ("arch/riscv64/panic.S", "kernel/panic.c"):
            with self.subTest(path=path):
                self.assert_workflow_selected(
                    path,
                    "test-qemu-bootstrap-ready-timeout",
                )
                self.assert_workflow_selected(
                    path,
                    "test-qemu-bootstrap-manifest-panic",
                )
                self.assert_workflow_selected(
                    path,
                    "test-qemu-vm-ready-early",
                )

    def test_uart_console_paths_own_component_gate(self):
        for path in (
            "arch/riscv64/platform.h",
            "arch/riscv64/uart.c",
            "kernel/uart_console_core.c",
            "kernel/uart_console_core.h",
            "kernel/uart_console_test.c",
            "kernel/uart_console_test.h",
            *validation_plan.TTY_CONTROL_INPUTS,
        ):
            with self.subTest(path=path):
                self.assert_workflow_selected(
                    path,
                    "test-qemu-uart-console",
                )

    def test_tty_service_paths_own_image_gate(self):
        for path in (
            "servers/tty/tty_service.c",
            "servers/tty/tty_service_core.c",
            "servers/tty/tty_uart.c",
            "servers/tty/tty_control.c",
            "tools/check_user_elf.py",
        ):
            with self.subTest(path=path):
                self.assert_workflow_selected(
                    path,
                    "build-tty-service-image",
                )

    def test_ramfs_service_paths_own_image_gate(self):
        for path in (
            "servers/ramfs/ramfs_core.c",
            "servers/ramfs/ramfs_seed.c",
            "servers/ramfs/ramfs_service.c",
            "servers/ramfs/ramfs_embedded_seed.h",
            "servers/ramfs/seed.json",
            "servers/ramfs/seed/etc/motd",
            "tools/check_user_elf.py",
            "tools/embed_ramfs_seed.py",
            "tools/generate_ramfs_seed.py",
        ):
            with self.subTest(path=path):
                self.assert_workflow_selected(
                    path,
                    "build-ramfs-service-image",
                )

    def test_vfs_service_paths_own_image_gate(self):
        for path in (
            "include/micros/vfs.h",
            "servers/vfs/vfs_core.c",
            "servers/vfs/vfs_core.h",
            "servers/vfs/vfs_service.c",
            "servers/vfs/vfs_service_core.c",
            "servers/vfs/vfs_service_core.h",
            "tools/check_user_elf.py",
        ):
            with self.subTest(path=path):
                self.assert_workflow_selected(
                    path,
                    "build-vfs-service-image",
                )

    def test_tty_production_and_fixture_paths_own_qemu_gate(self):
        for path in (
            *validation_plan.TTY_CONTROL_INPUTS,
            "include/micros/tty.h",
            "kernel/tty_service_test.c",
            "kernel/tty_service_test.h",
            "kernel/tty_service_test_fixture.h",
            "servers/tty/tty_service.c",
            "servers/tty/tty_service_core.c",
            "servers/tty/tty_uart.c",
            "tests/host/test_generate_tty_service_fixture.py",
            "tests/qemu/tty_handoff_protocol.h",
            "tests/qemu/tty_service_claim_wait.S",
            "tests/qemu/tty_service_report.S",
            "tests/qemu/tty_service_vfs.c",
            "tools/generate_tty_service_fixture.py",
        ):
            with self.subTest(path=path):
                self.assert_workflow_selected(
                    path,
                    "test-qemu-tty",
                )

    def test_tty_trap_paths_own_scheduler_gate(self):
        for path in validation_plan.TTY_TRAP_INPUTS:
            with self.subTest(path=path):
                self.assert_workflow_selected(
                    path,
                    "test-qemu-scheduler",
                )

    def test_ramfs_production_and_fixture_paths_own_qemu_gate(self):
        for path in (
            "include/micros/ramfs.h",
            "kernel/ramfs_service_test.c",
            "kernel/ramfs_service_test.h",
            "kernel/ramfs_service_test_fixture.h",
            "servers/ramfs/ramfs_core.c",
            "servers/ramfs/ramfs_seed.c",
            "servers/ramfs/ramfs_service.c",
            "tests/host/test_generate_ramfs_service_fixture.py",
            "tests/qemu/ramfs_service_protocol.h",
            "tests/qemu/ramfs_service_report.S",
            "tests/qemu/ramfs_service_vfs.c",
            "tools/generate_ramfs_service_fixture.py",
        ):
            with self.subTest(path=path):
                self.assert_workflow_selected(
                    path,
                    "test-qemu-ramfs",
                )

    def test_ramfs_model_entry_point_owns_qemu_gate(self):
        self.assert_workflow_selected(
            "tests/host/ramfs_model_test.c",
            "test-qemu-ramfs",
        )

    def test_vfs_production_and_fixture_paths_own_qemu_gate(self):
        for path in (
            "include/micros/vfs.h",
            "kernel/vfs_service_test.c",
            "kernel/vfs_service_test.h",
            "kernel/vfs_service_test_fixture.h",
            "servers/vfs/vfs_core.c",
            "servers/vfs/vfs_service.c",
            "servers/vfs/vfs_service_core.c",
            "tests/host/test_generate_vfs_service_fixture.py",
            "tests/host/vfs_model_test.c",
            "tests/host/vfs_service_test.c",
            "tests/host/vfs_test.c",
            "tests/host/vfs_test_fixture.c",
            "tests/qemu/vfs_service_probe.c",
            "tests/qemu/vfs_service_protocol.h",
            "tests/qemu/vfs_service_report.S",
            "tools/generate_vfs_service_fixture.py",
        ):
            with self.subTest(path=path):
                self.assert_workflow_selected(
                    path,
                    "test-qemu-vfs",
                )

    def test_ramfs_startup_paths_select_retained_service_gates(self):
        for path in (
            "servers/ramfs/ramfs_embedded_seed.h",
            "servers/ramfs/ramfs_service.c",
            "servers/ramfs/seed.json",
            "servers/ramfs/seed/etc/motd",
            "tools/embed_ramfs_seed.py",
            "tools/generate_ramfs_seed.py",
            "tools/generate_ramfs_service_fixture.py",
        ):
            commands = validation_plan.plan([path], "fast")
            with self.subTest(path=path):
                for workflow_name in (
                    "test-qemu-user-runtime",
                    "test-qemu-bootstrap-launcher",
                    "test-qemu-vm-handoff",
                    "test-qemu-pm-service",
                    "test-qemu-tty",
                    "test-qemu-ramfs",
                    "test-qemu-vfs",
                ):
                    self.assertIn(
                        validation_plan.workflow(workflow_name),
                        commands,
                    )

    def test_bootstrap_production_paths_select_cross_gate_union(self):
        for path in (
            "kernel/bootstrap_runtime.c",
            "kernel/bootstrap_syscall.c",
        ):
            commands = validation_plan.plan([path], "fast")
            with self.subTest(path=path):
                for workflow_name in (
                    validation_plan.BOOTSTRAP_CROSS_GATE_WORKFLOWS
                ):
                    self.assertIn(
                        validation_plan.workflow(workflow_name),
                        commands,
                    )

    def test_pm_bootstrap_paths_select_pm_gate(self):
        for path in (
            "kernel/bootstrap_control_core.c",
            "kernel/bootstrap_manifest.c",
            "kernel/bootstrap_runtime.c",
            "kernel/bootstrap_syscall.c",
        ):
            with self.subTest(path=path):
                self.assert_workflow_selected(
                    path,
                    "test-qemu-pm-service",
                )

    def test_pm_production_and_fixture_paths_own_pm_gate(self):
        for path in (
            "kernel/pm_control_core.c",
            "kernel/pm_control_core.h",
            "kernel/pm_control_runtime.c",
            "kernel/pm_control_runtime.h",
            "kernel/pm_control_syscall_core.c",
            "kernel/pm_control_syscall_core.h",
            "kernel/pm_control_syscall.c",
            "kernel/pm_control_syscall.h",
            "kernel/pm_service_test.c",
            "kernel/pm_service_test.h",
            "kernel/pm_service_test_fixture.h",
            "servers/pm/pm_control.c",
            "servers/pm/pm_control.h",
            "servers/pm/pm_core.c",
            "servers/pm/pm_core.h",
            "servers/pm/pm_service.c",
            "tests/host/pm_control_test.c",
            "tests/host/pm_test.c",
            "tests/host/test_generate_pm_service_fixture.py",
            "tests/qemu/pm_service_probe.c",
            "tests/qemu/pm_service_protocol.h",
            "tests/qemu/pm_service_report.S",
            "tools/generate_pm_service_fixture.py",
        ):
            with self.subTest(path=path):
                commands = validation_plan.plan([path], "fast")
                self.assertEqual(validation_plan.UNIT_FAST, commands[0])
                self.assertIn(
                    validation_plan.workflow("test-qemu-pm-service"),
                    commands,
                )

    def test_pm_dispatch_and_vm_fixture_paths_select_pm_gate(self):
        for path in (
            "kernel/syscall.c",
            "tests/qemu/vm_server.c",
            "tools/generate_bootstrap_fixture.py",
        ):
            with self.subTest(path=path):
                self.assert_workflow_selected(
                    path,
                    "test-qemu-pm-service",
                )

    def test_vm_fault_source_selects_all_vm_workflows(self):
        commands = validation_plan.plan(
            ["tests/qemu/vm_self_fault.S"],
            "fast",
        )
        for workflow_name in (
            "test-qemu-vm-handoff",
            "test-qemu-vm-ready-early",
            "test-qemu-vm-self-fault",
            "test-qemu-vm-self-fault-sealed",
        ):
            self.assertIn(
                validation_plan.workflow(workflow_name),
                commands,
            )

    def test_grant_syscall_paths_own_unified_gates(self):
        for path in (
            "kernel/syscall.c",
            "kernel/ipc_syscall.c",
            "kernel/grant_abi.c",
            "kernel/grant_syscall_core.c",
            "kernel/grant_syscall.c",
            "kernel/grant.c",
            "kernel/grant_copy.c",
            "kernel/grant_copy_core.c",
            "kernel/grant_runtime.c",
            "tests/host/grant_syscall_core_test.c",
            "kernel/grant_syscall_test.c",
            "arch/riscv64/grant_syscall_test.S",
        ):
            with self.subTest(path=path):
                commands = validation_plan.plan([path], "fast")
                self.assertIn(
                    validation_plan.workflow(
                        "test-qemu-grant-syscall"
                    ),
                    commands,
                )
                self.assertIn(
                    validation_plan.workflow(
                        "test-qemu-address-space-handoff"
                    ),
                    commands,
                )
        for path in (
            "kernel/syscall.c",
            "kernel/ipc_syscall.c",
            "kernel/grant_abi.c",
            "kernel/grant_syscall_core.c",
            "kernel/grant_syscall.c",
            "kernel/grant.c",
            "kernel/grant_copy.c",
            "kernel/grant_copy_core.c",
            "kernel/grant_runtime.c",
        ):
            with self.subTest(shared_path=path):
                commands = validation_plan.plan([path], "fast")
                for workflow_name in (
                    "test-qemu-ipc-ecall-core",
                    "test-qemu-ipc-syscall",
                    "test-qemu-ipc-syscall-panic",
                ):
                    self.assertIn(
                        validation_plan.workflow(workflow_name),
                        commands,
                    )
                self.assertIn(validation_plan.IPC_MODEL, commands)

    def test_grant_paths_own_endpoint_gate(self):
        for path in (
            "kernel/grant.c",
            "kernel/grant_runtime.c",
        ):
            with self.subTest(path=path):
                commands = validation_plan.plan([path], "fast")
                self.assertIn(
                    validation_plan.workflow("test-qemu-endpoint"),
                    commands,
                )
                self.assertIn(
                    validation_plan.workflow("test-qemu-grant"),
                    commands,
                )

        commands = validation_plan.plan(["kernel/ipc.c"], "fast")
        self.assertIn(
            validation_plan.workflow("test-qemu-endpoint"),
            commands,
        )
        commands = validation_plan.plan(
            ["tests/host/grant_test.c"],
            "fast",
        )
        self.assertIn(validation_plan.IPC_MODEL, commands)

    def test_user_runtime_paths_own_native_static_and_qemu_gate(self):
        for path in (
            "include/micros/runtime.h",
            "lib/runtime/runtime.c",
            "lib/runtime/memory.c",
            "lib/runtime/start.S",
            "lib/runtime/raw_syscall.S",
            "lib/runtime/user.ld",
            "tests/host/runtime_test.c",
            "tests/host/test_check_user_elf.py",
            "tools/check_user_elf.py",
            "tools/generate_user_runtime_fixture.py",
            "tests/qemu/user_runtime_service.c",
            "kernel/user_runtime_test.c",
        ):
            with self.subTest(path=path):
                commands = validation_plan.plan([path], "fast")
                self.assertIn(
                    validation_plan.workflow(
                        "test-qemu-user-runtime"
                    ),
                    commands,
                )
                self.assertTrue(
                    validation_plan.UNIT_FAST in commands
                    or validation_plan.UNIT_FULL in commands
                )

    def test_checked_copy_paths_own_grant_gate(self):
        for path in (
            "kernel/grant.c",
            "kernel/grant_copy.c",
            "kernel/grant_copy_core.c",
            "kernel/grant_runtime.c",
            "kernel/user_address_space.c",
            "kernel/address_space.c",
            "kernel/sv39.c",
            "kernel/frame_ownership.c",
            "arch/riscv64/mmu.S",
        ):
            with self.subTest(path=path):
                commands = validation_plan.plan([path], "fast")
                self.assertIn(
                    validation_plan.workflow("test-qemu-grant"),
                    commands,
                )
                self.assertIn(
                    validation_plan.workflow(
                        "test-qemu-user-address-space"
                    ),
                    commands,
                )
                self.assertTrue(
                    validation_plan.IPC_MODEL in commands
                    or validation_plan.UNIT_FULL in commands
                )
        commands = validation_plan.plan(
            ["tests/host/grant_copy_test.c"],
            "fast",
        )
        self.assertIn(validation_plan.IPC_MODEL, commands)

    def test_wired_handoff_paths_own_component_gate(self):
        for path in (
            "arch/riscv64/address_space_handoff_test.S",
            "kernel/address_space_handoff_test.h",
            "kernel/address_space_handoff_test.c",
            "kernel/bootstrap_memory.c",
            "kernel/frame_ownership.c",
            "kernel/user_address_space.c",
            "kernel/user_address_space_core.c",
            "kernel/user_execution.c",
            "kernel/ipc_buffer.c",
            "kernel/grant_copy.c",
            "kernel/address_space.c",
            "kernel/sv39.c",
            "kernel/scheduler.c",
            "arch/riscv64/mmu.S",
        ):
            with self.subTest(path=path):
                commands = validation_plan.plan([path], "fast")
                for workflow_name in (
                    "test-qemu-address-space-handoff",
                    "test-qemu-frame-ownership",
                    "test-qemu-user-address-space",
                    "test-qemu-user-execution",
                    "test-qemu-grant",
                    "test-qemu-grant-syscall",
                    "test-qemu-ipc-syscall",
                    "test-qemu-ipc-syscall-panic",
                    "test-qemu-scheduler",
                ):
                    self.assertIn(
                        validation_plan.workflow(workflow_name),
                        commands,
                    )
                self.assertTrue(
                    validation_plan.IPC_MODEL in commands
                    or validation_plan.UNIT_FULL in commands
                )

    def test_fast_full_escalation_runs_complete_qemu(self):
        commands = validation_plan.plan(
            ["kernel/unclassified_target_code.c"],
            "fast",
        )
        self.assertEqual(validation_plan.UNIT_FULL, commands[0])
        for name in validation_plan.QEMU_WORKFLOWS:
            self.assertIn(validation_plan.workflow(name), commands)
        for name in validation_plan.IMAGE_WORKFLOWS:
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

    def test_vfs_protocol_headers_own_vfs_model(self):
        for path in (
            "include/micros/vfs.h",
            "include/micros/tty.h",
            "include/micros/ramfs.h",
        ):
            with self.subTest(path=path):
                self.assertIn(
                    "test-vfs-model",
                    validation_plan.affected_slow_models([path]),
                )

    def test_each_gate_has_target_specific_input(self):
        representatives = {
            "test-qemu-smoke": "kernel/fdt.c",
            "test-qemu-panic": "kernel/panic.c",
            "test-qemu-trap": "arch/riscv64/trap_test.S",
            "test-qemu-timer": "arch/riscv64/sbi.c",
            "test-qemu-uart-console": "kernel/uart_console_test.c",
            "test-qemu-frame-allocator": "kernel/frame_allocator.c",
            "test-qemu-trap-panic": "arch/riscv64/trap_test.S",
            "test-qemu-mmu": "kernel/mmu_test.c",
            "test-qemu-object-model": "kernel/object_model_test.c",
            "test-qemu-endpoint": "kernel/endpoint_test.c",
            "test-qemu-grant": "kernel/grant_test.c",
            "test-qemu-grant-syscall": "kernel/grant_syscall_test.c",
            "test-qemu-user-runtime": "kernel/user_runtime_test.c",
            "test-qemu-bootstrap-launcher": "kernel/bootstrap_test.c",
            "test-qemu-vm-handoff": "kernel/vm_handoff_test.c",
            "test-qemu-pm-service": "kernel/pm_service_test.c",
            "test-qemu-tty": "kernel/tty_service_test.c",
            "test-qemu-ramfs": "kernel/ramfs_service_test.c",
            "test-qemu-vfs": "kernel/vfs_service_test.c",
            "test-qemu-vm-ready-early": "tests/qemu/vm_server.c",
            "test-qemu-vm-self-fault": "tests/qemu/vm_self_fault.S",
            "test-qemu-vm-self-fault-sealed": "tests/qemu/vm_self_fault.S",
            "test-qemu-bootstrap-ready-timeout": "kernel/bootstrap_test.c",
            "test-qemu-bootstrap-manifest-panic": "kernel/bootstrap_test.c",
            "test-qemu-ipc": "kernel/ipc_test.c",
            "test-qemu-ipc-ecall-core": "kernel/ipc_ecall_test.c",
            "test-qemu-ipc-syscall": "kernel/ipc_syscall_test.c",
            "test-qemu-ipc-syscall-panic":
                "kernel/ipc_syscall_panic_test.c",
            "test-qemu-nested-trap": "arch/riscv64/nested_trap_test.S",
            "test-qemu-frame-ownership": "kernel/frame_ownership_test.c",
            "test-qemu-user-address-space": "kernel/user_address_space_test.c",
            "test-qemu-address-space-handoff": (
                "kernel/address_space_handoff_test.c"
            ),
            "test-qemu-user-execution": "kernel/user_execution_test.c",
            "test-qemu-scheduler": "kernel/scheduler_test.c",
            "test-qemu-scheduler-invalid-outgoing": (
                "kernel/scheduler_invalid_test.c"
            ),
            "test-qemu-scheduler-invalid-next": (
                "arch/riscv64/scheduler_test.S"
            ),
        }
        self.assertEqual(
            set(validation_plan.QEMU_WORKFLOWS),
            set(representatives),
        )
        for expected, path in representatives.items():
            with self.subTest(workflow=expected, path=path):
                self.assertTrue(
                    validation_plan.path_matches(
                        path,
                        validation_plan.GATE_INPUTS[expected],
                    )
                )
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
            len(validation_plan.QEMU_WORKFLOWS)
            + len(validation_plan.IMAGE_WORKFLOWS)
            + 5,
            len(commands),
        )


if __name__ == "__main__":
    unittest.main()

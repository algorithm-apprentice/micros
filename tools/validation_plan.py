#!/usr/bin/env python3

import argparse
import json
import os
import re
import shlex
import subprocess
import sys
from pathlib import Path

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tools import validation_build_impact, validation_context, validation_execution


UNIT_FAST = ("cmake", "--workflow", "--preset", "test-unit-fast")
UNIT_FULL = ("cmake", "--workflow", "--preset", "test-unit")
IPC_MODEL = ("cmake", "--workflow", "--preset", "test-ipc-model")
TTY_MODEL = ("cmake", "--workflow", "--preset", "test-tty-model")
RAMFS_MODEL = ("cmake", "--workflow", "--preset", "test-ramfs-model")
VFS_MODEL = ("cmake", "--workflow", "--preset", "test-vfs-model")
ELF_SPAWN_MODEL = ("cmake", "--workflow", "--preset", "test-elf-spawn-model")
DOCS = ("python3", "tools/check_docs.py")
SMOKE = ("cmake", "--workflow", "--preset", "test-qemu-smoke")

QEMU_WORKFLOWS = (
    "test-qemu-smoke",
    "test-qemu-panic",
    "test-qemu-trap",
    "test-qemu-timer",
    "test-qemu-uart-console",
    "test-qemu-frame-allocator",
    "test-qemu-trap-panic",
    "test-qemu-mmu",
    "test-qemu-object-model",
    "test-qemu-endpoint",
    "test-qemu-grant",
    "test-qemu-grant-syscall",
    "test-qemu-user-runtime",
    "test-qemu-bootstrap-launcher",
    "test-qemu-vm-handoff",
    "test-qemu-pm-service",
    "test-qemu-vm-scratch-preemption",
    "test-qemu-tty",
    "test-qemu-ramfs",
    "test-qemu-vfs",
    "test-qemu-elf-spawn",
    "test-qemu-vm-ready-early",
    "test-qemu-vm-self-fault",
    "test-qemu-vm-self-fault-sealed",
    "test-qemu-bootstrap-ready-timeout",
    "test-qemu-bootstrap-manifest-panic",
    "test-qemu-ipc",
    "test-qemu-ipc-ecall-core",
    "test-qemu-ipc-syscall",
    "test-qemu-ipc-syscall-panic",
    "test-qemu-nested-trap",
    "test-qemu-frame-ownership",
    "test-qemu-user-address-space",
    "test-qemu-address-space-handoff",
    "test-qemu-user-execution",
    "test-qemu-scheduler",
    "test-qemu-scheduler-invalid-outgoing",
    "test-qemu-scheduler-invalid-next",
)

IMAGE_WORKFLOWS = (
    "build-ramfs-service-image",
    "build-tty-service-image",
    "build-vfs-service-image",
    "build-application-images",
)

IMAGE_INPUTS = {
    "build-tty-service-image": (
        "CMakeLists.txt",
        "CMakePresets.json",
        "cmake/",
        "include/micros/",
        "lib/runtime/",
        "servers/tty/",
        "tools/check_user_elf.py",
    ),
    "build-ramfs-service-image": (
        "CMakeLists.txt",
        "CMakePresets.json",
        "cmake/",
        "include/micros/",
        "lib/runtime/",
        "servers/ramfs/",
        "tools/check_user_elf.py",
        "tools/embed_ramfs_seed.py",
        "tools/generate_ramfs_seed.py",
    ),
    "build-vfs-service-image": (
        "CMakeLists.txt",
        "CMakePresets.json",
        "cmake/",
        "include/micros/",
        "lib/runtime/",
        "servers/vfs/",
        "tools/check_user_elf.py",
    ),
    "build-application-images": (
        "CMakeLists.txt",
        "CMakePresets.json",
        "cmake/",
        "include/micros/",
        "lib/runtime/",
        "tests/qemu/application_runtime_probe.c",
        "tests/qemu/application_minimal_probe.c",
        "tests/qemu/pm_service_protocol.h",
        "tools/check_user_elf.py",
        "tools/check_application_elf.py",
        "tools/application_elf_policy.py",
        "tools/generate_application_seed.py",
        "tools/generate_ramfs_seed.py",
        "tools/generate_elf_spawn_seed.py",
        "tests/qemu/elf_spawn_init.c",
        "tests/qemu/elf_spawn_child.c",
        "tests/qemu/elf_spawn_client.c",
        "tests/qemu/elf_spawn_client_vfs.c",
        "tests/qemu/elf_spawn_client.h",
        "tests/qemu/elf_spawn_request.c",
        "tests/qemu/elf_spawn_request.h",
        "tests/qemu/elf_spawn_report.S",
        "tests/qemu/elf_spawn_protocol.h",
    ),
}

PLANNER_INPUTS = (
    "tools/validation_plan.py",
    "tests/host/test_validation_plan.py",
    "tools/validation_build_impact.py",
    "tests/host/test_validation_build_impact.py",
    "tools/validation_evidence.py",
    "tests/host/test_validation_evidence.py",
    "tools/validation_context.py",
    "tests/host/test_validation_context.py",
    "tools/validation_execution.py",
    "tests/host/test_validation_execution.py",
    "tools/validation_inputs.py",
    "tools/validation_build_manifest.py",
    "tests/host/test_validation_context_layout.py",
    "docs/adr/0032-fail-closed-change-aware-validation.md",
    "docs/adr/0064-fail-closed-validation-evidence-reuse.md",
)

PATH_CLASSIFICATION_EQUIVALENTS = (('tools/run_qemu_smoke.py',
  ('tools/qemu_smoke_records.py',
   'tools/qemu_smoke_platform.py',
   'tools/qemu_smoke_objects.py',
   'tools/qemu_smoke_faults.py',
   'tools/qemu_smoke_command.py',
   'tools/qemu_smoke_execution.py',
   'tools/qemu_smoke_outcome.py',
   'tools/qemu_smoke_arguments.py',
   'tools/qemu_smoke_cli.py')),
 ('tests/host/test_run_qemu_smoke.py',
  ('tests/host/qemu_smoke_test_fixture.py',
   'tests/host/qemu_smoke_classification_cases.py',
   'tests/host/qemu_smoke_panic_cases.py',
   'tests/host/qemu_smoke_platform_cases.py',
   'tests/host/qemu_smoke_memory_cases.py',
   'tests/host/qemu_smoke_object_cases.py',
   'tests/host/qemu_smoke_execution_cases.py')),
 ('kernel/tty_handoff_runtime.c',
  ('kernel/tty_handoff_runtime_state.c',
   'kernel/tty_handoff_runtime_validate.c',
   'kernel/tty_handoff_runtime_transitions.c',
   'kernel/tty_handoff_runtime_internal.h',
   'kernel/tty/tty_handoff_runtime.c',
   'kernel/tty/tty_handoff_runtime_state.c',
   'kernel/tty/tty_handoff_runtime_validate.c',
   'kernel/tty/tty_handoff_runtime_transitions.c',
   'kernel/tty/tty_handoff_runtime_internal.h')),
 ('servers/tty/tty_service_core.c',
  ('servers/tty/tty_service_validate.c',
   'servers/tty/tty_service_read.c',
   'servers/tty/tty_service_write.c',
   'servers/tty/tty_service_request.c',
   'servers/tty/tty_service_internal.h')),
 ('tests/host/tty_model_test.c',
  ('tests/host/tty_model_common.c',
   'tests/host/tty_model_reference.c',
   'tests/host/tty_model_io.c',
   'tests/host/tty_model_protocol.c',
   'tests/host/tty_model_operations.c',
   'tests/host/tty_model_validate.c',
   'tests/host/tty_model_internal.h')),
 ('tests/host/tty_handoff_test.c',
  ('tests/host/tty_handoff_test_fixture.c',
   'tests/host/tty_handoff_device_test.c',
   'tests/host/tty_handoff_runtime_test.c',
   'tests/host/tty_handoff_test_internal.h')),
 ('tests/host/tty_control_syscall_test.c',
  ('tests/host/tty_control_syscall_test_fixture.c',
   'tests/host/tty_control_syscall_test_observe.c',
   'tests/host/tty_control_syscall_test_controller.c',
   'tests/host/tty_control_syscall_test_internal.h')),
 ('kernel/kernel_objects.c',
  ('kernel/kernel_object_state.c',
   'kernel/kernel_object_identity.c',
   'kernel/kernel_object_stack.c',
   'kernel/kernel_object_process.c',
   'kernel/kernel_object_thread.c',
   'kernel/kernel_object_context.c',
   'kernel/kernel_object_hart.c',
   'kernel/kernel_object_validation.c',
   'kernel/kernel_object_private.h',
   'kernel/objects/kernel_objects.c',
   'kernel/objects/kernel_object_state.c',
   'kernel/objects/kernel_object_identity.c',
   'kernel/objects/kernel_object_stack.c',
   'kernel/objects/kernel_object_process.c',
   'kernel/objects/kernel_object_thread.c',
   'kernel/objects/kernel_object_context.c',
   'kernel/objects/kernel_object_hart.c',
   'kernel/objects/kernel_object_validation.c',
   'kernel/objects/kernel_object_private.h')),
 ('tests/host/kernel_objects_test.c',
  ('tests/host/kernel_objects_test_fixture.c',
   'tests/host/kernel_objects_initialization_test.c',
   'tests/host/kernel_objects_process_test.c',
   'tests/host/kernel_objects_thread_test.c',
   'tests/host/kernel_objects_hart_test.c',
   'tests/host/kernel_objects_identity_test.c',
   'tests/host/kernel_objects_validation_test.c',
   'tests/host/kernel_objects_model_test.c',
   'tests/host/kernel_objects_model_reference.c',
   'tests/host/kernel_objects_model_projection.c',
   'tests/host/kernel_objects_model_operations.c',
   'tests/host/kernel_objects_test_internal.h')),
 ('kernel/object_model_test.c',
  ('kernel/object_model_test_scenarios.c',
   'kernel/object_model_test_internal.h',
   'kernel/tests/objects/object_model_test.c',
   'kernel/tests/objects/object_model_test_scenarios.c',
   'kernel/tests/objects/object_model_test_internal.h')),
 ('kernel/endpoint.c',
  ('kernel/endpoint_codec.c',
   'kernel/endpoint_identity.c',
   'kernel/endpoint_registry.c',
   'kernel/endpoint_privilege.c',
   'kernel/endpoint_completion_validation.c',
   'kernel/endpoint_queue_validation.c',
   'kernel/endpoint_object_validation.c',
   'kernel/endpoint_publication.c',
   'kernel/endpoint_close.c',
   'kernel/endpoint_private.h',
   'kernel/endpoint/endpoint.c',
   'kernel/endpoint/endpoint_codec.c',
   'kernel/endpoint/endpoint_identity.c',
   'kernel/endpoint/endpoint_registry.c',
   'kernel/endpoint/endpoint_privilege.c',
   'kernel/endpoint/endpoint_completion_validation.c',
   'kernel/endpoint/endpoint_queue_validation.c',
   'kernel/endpoint/endpoint_object_validation.c',
   'kernel/endpoint/endpoint_publication.c',
   'kernel/endpoint/endpoint_close.c',
   'kernel/endpoint/endpoint_private.h')),
 ('tests/host/endpoint_test.c',
  ('tests/host/endpoint_test_fixture.c',
   'tests/host/endpoint_identity_test.c',
   'tests/host/endpoint_profile_test.c',
   'tests/host/endpoint_validation_test.c',
   'tests/host/endpoint_lifecycle_test.c',
   'tests/host/endpoint_source_only_test.c',
   'tests/host/endpoint_test_internal.h')),
 ('tests/host/endpoint_model_test.c',
  ('tests/host/endpoint_model_fixture.c',
   'tests/host/endpoint_model_reference.c',
   'tests/host/endpoint_model_projection.c',
   'tests/host/endpoint_model_operations.c',
   'tests/host/endpoint_model_internal.h')),
 ('kernel/endpoint_test.c',
  ('kernel/endpoint_test_fixture.c',
   'kernel/endpoint_test_scenarios.c',
   'kernel/endpoint_test_internal.h',
   'kernel/tests/endpoint/endpoint_test.c',
   'kernel/tests/endpoint/endpoint_test_fixture.c',
   'kernel/tests/endpoint/endpoint_test_scenarios.c',
   'kernel/tests/endpoint/endpoint_test_internal.h')),
 ('servers/ramfs/ramfs_core.c',
  ('servers/ramfs/ramfs_codec.c',
   'servers/ramfs/ramfs_ownership.c',
   'servers/ramfs/ramfs_state.c',
   'servers/ramfs/ramfs_transfer.c',
   'servers/ramfs/ramfs_namespace.c',
   'servers/ramfs/ramfs_io.c',
   'servers/ramfs/ramfs_directory.c',
   'servers/ramfs/ramfs_internal.h')),
 ('tests/host/ramfs_test.c',
  ('tests/host/ramfs_test_fixture.c',
   'tests/host/ramfs_protocol_test.c',
   'tests/host/ramfs_namespace_test.c',
   'tests/host/ramfs_io_test.c',
   'tests/host/ramfs_directory_test.c',
   'tests/host/ramfs_test_internal.h')),
 ('tests/host/ramfs_seed_test.c',
  ('tests/host/ramfs_seed_test_fixture.c',
   'tests/host/ramfs_seed_boundary_test.c',
   'tests/host/ramfs_seed_corruption_test.c',
   'tests/host/ramfs_seed_test_internal.h')),
 ('tests/host/ramfs_model_test.c',
  ('tests/host/ramfs_model_fixture.c',
   'tests/host/ramfs_model_reference.c',
   'tests/host/ramfs_model_projection.c',
   'tests/host/ramfs_model_protocol.c',
   'tests/host/ramfs_model_operations.c',
   'tests/host/ramfs_model_internal.h')),
 ('tests/qemu/ramfs_service_vfs.c',
  ('tests/qemu/ramfs_service_vfs_fixture.c',
   'tests/qemu/ramfs_service_vfs_ramfs.c',
   'tests/qemu/ramfs_service_vfs_scenario.c',
   'tests/qemu/ramfs_service_vfs_tty.c',
   'tests/qemu/ramfs_service_vfs_internal.h')),
 ('kernel/ramfs_service_test.c',
  ('kernel/ramfs_service_test_fixture.c',
   'kernel/ramfs_service_test_report.c',
   'kernel/ramfs_service_test_internal.h',
   'kernel/tests/ramfs/ramfs_service_test.c',
   'kernel/tests/ramfs/ramfs_service_test_fixture.c',
   'kernel/tests/ramfs/ramfs_service_test_report.c',
   'kernel/tests/ramfs/ramfs_service_test_internal.h')),
 ('tools/check_user_elf.py',
  ('tools/user_elf_format.py',
   'tools/user_elf_parse.py',
   'tools/user_elf_structure.py',
   'tools/user_elf_instructions.py',
   'tools/user_elf_policy.py')),
 ('tests/host/test_check_user_elf.py',
  ('tests/host/user_elf_test_fixture.py',
   'tests/host/user_elf_parse_cases.py',
   'tests/host/user_elf_structure_cases.py',
   'tests/host/user_elf_policy_cases.py',
   'tests/host/user_elf_cli_cases.py')),
 ('kernel/address_space.c',
  ('kernel/address_space_validation.c',
   'kernel/address_space_internal.h',
   'kernel/memory/address_space.c',
   'kernel/memory/address_space_validation.c',
   'kernel/memory/address_space_internal.h')),
 ('kernel/user_address_space.c',
  ('kernel/user_address_space_runtime.c',
   'kernel/user_address_space_table.c',
   'kernel/user_address_space_validation.c',
   'kernel/user_address_space_mapping.c',
   'kernel/user_address_space_mutation.c',
   'kernel/user_address_space_handoff.c',
   'kernel/user_address_space_tty.c',
   'kernel/user_address_space_vm_scratch.c',
   'kernel/user_address_space_vm_freeze.c',
   'kernel/user_address_space_vm_teardown.c',
   'kernel/user_address_space_activation.c',
   'kernel/user_address_space_internal.h',
   'kernel/memory/user_address_space.c',
   'kernel/memory/user_address_space_runtime.c',
   'kernel/memory/user_address_space_table.c',
   'kernel/memory/user_address_space_validation.c',
   'kernel/memory/user_address_space_mapping.c',
   'kernel/memory/user_address_space_mutation.c',
   'kernel/memory/user_address_space_handoff.c',
   'kernel/memory/user_address_space_tty.c',
   'kernel/memory/user_address_space_vm_scratch.c',
   'kernel/memory/user_address_space_vm_freeze.c',
   'kernel/memory/user_address_space_vm_teardown.c',
   'kernel/memory/user_address_space_activation.c',
   'kernel/memory/user_address_space_internal.h')),
 ('kernel/user_address_space_core.c',
  ('kernel/user_address_space_vm_plan.c',
   'kernel/memory/user_address_space_core.c',
   'kernel/memory/user_address_space_vm_plan.c')),
 ('kernel/user_execution.c',
  ('kernel/user_execution_validation.c',
   'kernel/user_execution_trap.c',
   'kernel/user_execution_internal.h',
   'kernel/execution/user_execution.c',
   'kernel/execution/user_execution_validation.c',
   'kernel/execution/user_execution_trap.c',
   'kernel/execution/user_execution_internal.h')),
 ('kernel/user_address_space_test.c',
  ('kernel/user_address_space_test_fixture.c',
   'kernel/user_address_space_test_lifecycle.c',
   'kernel/user_address_space_test_mapping.c',
   'kernel/user_address_space_test_validation.c',
   'kernel/user_address_space_test_internal.h',
   'kernel/tests/memory/user_address_space_test.c',
   'kernel/tests/memory/user_address_space_test_fixture.c',
   'kernel/tests/memory/user_address_space_test_lifecycle.c',
   'kernel/tests/memory/user_address_space_test_mapping.c',
   'kernel/tests/memory/user_address_space_test_validation.c',
   'kernel/tests/memory/user_address_space_test_internal.h')),
 ('kernel/user_execution_test.c',
  ('kernel/user_execution_test_fixture.c',
   'kernel/user_execution_test_prepare.c',
   'kernel/user_execution_test_trap.c',
   'kernel/user_execution_test_lifecycle.c',
   'kernel/user_execution_test_internal.h',
   'kernel/tests/execution/user_execution_test.c',
   'kernel/tests/execution/user_execution_test_fixture.c',
   'kernel/tests/execution/user_execution_test_prepare.c',
   'kernel/tests/execution/user_execution_test_trap.c',
   'kernel/tests/execution/user_execution_test_lifecycle.c',
   'kernel/tests/execution/user_execution_test_internal.h')),
 ('tests/host/user_address_space_core_test.c',
  ('tests/host/user_address_space_owner_test.c',
   'tests/host/user_address_space_vm_plan_test.c',
   'tests/host/user_address_space_core_test_suites.h')),
 ('kernel/frame_ownership.c',
  ('kernel/frame_ownership_validation.c',
   'kernel/frame_ownership_bootstrap.c',
   'kernel/frame_ownership_handoff.c',
   'kernel/frame_ownership_vm_claim.c',
   'kernel/frame_ownership_vm_freeze.c',
   'kernel/frame_ownership_vm_release.c',
   'kernel/frame_ownership_internal.h',
   'kernel/memory/frame_ownership.c',
   'kernel/memory/frame_ownership_validation.c',
   'kernel/memory/frame_ownership_bootstrap.c',
   'kernel/memory/frame_ownership_handoff.c',
   'kernel/memory/frame_ownership_vm_claim.c',
   'kernel/memory/frame_ownership_vm_freeze.c',
   'kernel/memory/frame_ownership_vm_release.c',
   'kernel/memory/frame_ownership_internal.h')),
 ('kernel/frame_ownership_test.c',
  ('kernel/frame_ownership_test_fixture.c',
   'kernel/frame_ownership_test_lifecycle.c',
   'kernel/frame_ownership_test_handoff.c',
   'kernel/frame_ownership_test_internal.h',
   'kernel/tests/memory/frame_ownership_test.c',
   'kernel/tests/memory/frame_ownership_test_fixture.c',
   'kernel/tests/memory/frame_ownership_test_lifecycle.c',
   'kernel/tests/memory/frame_ownership_test_handoff.c',
   'kernel/tests/memory/frame_ownership_test_internal.h')),
 ('tests/host/frame_ownership_test.c',
  ('tests/host/frame_ownership_test_fixture.c',
   'tests/host/frame_ownership_test_bootstrap.c',
   'tests/host/frame_ownership_test_validation.c',
   'tests/host/frame_ownership_test_handoff.c',
   'tests/host/frame_ownership_test_model.c',
   'tests/host/frame_ownership_test_vm.c',
   'tests/host/frame_ownership_test_internal.h')),
 ('tests/host/frame_allocator_test.c',
  ('tests/host/frame_allocator_test_cases.c',
   'tests/host/frame_allocator_model_test.c',
   'tests/host/frame_allocator_test_internal.h')),
 ('kernel/vm_handoff_core.c',
  ('kernel/vm_handoff_abi.c',
   'kernel/vm_handoff_boot.c',
   'kernel/vm_handoff_load.c',
   'kernel/vm_handoff_scratch.c',
   'kernel/vm_handoff_freeze.c',
   'kernel/vm_handoff_commit.c',
   'kernel/vm_handoff_release.c',
   'kernel/vm_handoff_internal.h',
   'kernel/vm/vm_handoff_core.c',
   'kernel/vm/vm_handoff_abi.c',
   'kernel/vm/vm_handoff_boot.c',
   'kernel/vm/vm_handoff_load.c',
   'kernel/vm/vm_handoff_scratch.c',
   'kernel/vm/vm_handoff_freeze.c',
   'kernel/vm/vm_handoff_commit.c',
   'kernel/vm/vm_handoff_release.c',
   'kernel/vm/vm_handoff_internal.h')),
 ('kernel/vm_handoff_syscall.c',
  ('kernel/vm_handoff_syscall_runtime.c',
   'kernel/vm_handoff_syscall_memory.c',
   'kernel/vm_handoff_syscall_boot.c',
   'kernel/vm_handoff_syscall_load.c',
   'kernel/vm_handoff_syscall_scratch.c',
   'kernel/vm_handoff_syscall_freeze.c',
   'kernel/vm_handoff_syscall_release.c',
   'kernel/vm_handoff_syscall_internal.h',
   'kernel/vm/vm_handoff_syscall.c',
   'kernel/vm/vm_handoff_syscall_runtime.c',
   'kernel/vm/vm_handoff_syscall_memory.c',
   'kernel/vm/vm_handoff_syscall_boot.c',
   'kernel/vm/vm_handoff_syscall_load.c',
   'kernel/vm/vm_handoff_syscall_scratch.c',
   'kernel/vm/vm_handoff_syscall_freeze.c',
   'kernel/vm/vm_handoff_syscall_release.c',
   'kernel/vm/vm_handoff_syscall_internal.h')),
 ('kernel/vm_snapshot.c',
  ('kernel/vm_snapshot_validation.c',
   'kernel/vm_snapshot_internal.h',
   'kernel/vm/vm_snapshot.c',
   'kernel/vm/vm_snapshot_validation.c',
   'kernel/vm/vm_snapshot_internal.h')),
 ('kernel/address_space_handoff_test.c',
  ('kernel/address_space_handoff_test_fixture.c',
   'kernel/address_space_handoff_test_setup.c',
   'kernel/address_space_handoff_test_snapshot.c',
   'kernel/address_space_handoff_test_handoff.c',
   'kernel/address_space_handoff_test_runtime.c',
   'kernel/address_space_handoff_test_syscall.c',
   'kernel/address_space_handoff_test_tty.c',
   'kernel/address_space_handoff_test_trap.c',
   'kernel/address_space_handoff_test_internal.h',
   'kernel/tests/memory/address_space_handoff_test.c',
   'kernel/tests/memory/address_space_handoff_test_fixture.c',
   'kernel/tests/memory/address_space_handoff_test_setup.c',
   'kernel/tests/memory/address_space_handoff_test_snapshot.c',
   'kernel/tests/memory/address_space_handoff_test_handoff.c',
   'kernel/tests/memory/address_space_handoff_test_runtime.c',
   'kernel/tests/memory/address_space_handoff_test_syscall.c',
   'kernel/tests/memory/address_space_handoff_test_tty.c',
   'kernel/tests/memory/address_space_handoff_test_trap.c',
   'kernel/tests/memory/address_space_handoff_test_internal.h')),
 ('tests/host/vm_handoff_syscall_test.c',
  ('tests/host/vm_handoff_syscall_test_fixture.c',
   'tests/host/vm_handoff_syscall_test_boot.c',
   'tests/host/vm_handoff_syscall_test_load.c',
   'tests/host/vm_handoff_syscall_test_runtime_backend.c',
   'tests/host/vm_handoff_syscall_test_boot_backend.c',
   'tests/host/vm_handoff_syscall_test_load_backend.c',
   'tests/host/vm_handoff_syscall_test_internal.h')),
 ('tests/host/vm_handoff_test.c',
  ('tests/host/vm_handoff_test_fixture.c',
   'tests/host/vm_handoff_abi_test.c',
   'tests/host/vm_handoff_boot_test.c',
   'tests/host/vm_handoff_load_test.c',
   'tests/host/vm_handoff_test_internal.h')),
 ('tests/host/vm_bootstrap_test.c',
  ('tests/host/vm_bootstrap_test_fixture.c',
   'tests/host/vm_bootstrap_abi_test.c',
   'tests/host/vm_bootstrap_build_test.c',
   'tests/host/vm_bootstrap_test_internal.h')),
 ('kernel/pm_control_core.c',
  ('kernel/pm_control_abi.c',
   'kernel/pm_control_validation.c',
   'kernel/pm_control_reservation.c',
   'kernel/pm_control_execution.c',
   'kernel/pm_control_activation.c',
   'kernel/pm_control_exit.c',
   'kernel/pm_control_internal.h',
   'kernel/pm/pm_control_core.c',
   'kernel/pm/pm_control_abi.c',
   'kernel/pm/pm_control_validation.c',
   'kernel/pm/pm_control_reservation.c',
   'kernel/pm/pm_control_execution.c',
   'kernel/pm/pm_control_activation.c',
   'kernel/pm/pm_control_exit.c',
   'kernel/pm/pm_control_internal.h')),
 ('kernel/pm_control_syscall.c',
  ('kernel/pm_control_syscall_runtime.c',
   'kernel/pm_control_syscall_reservation.c',
   'kernel/pm_control_syscall_execution.c',
   'kernel/pm_control_syscall_activation.c',
   'kernel/pm_control_syscall_exit.c',
   'kernel/pm_control_syscall_internal.h',
   'kernel/pm/pm_control_syscall.c',
   'kernel/pm/pm_control_syscall_runtime.c',
   'kernel/pm/pm_control_syscall_reservation.c',
   'kernel/pm/pm_control_syscall_execution.c',
   'kernel/pm/pm_control_syscall_activation.c',
   'kernel/pm/pm_control_syscall_exit.c',
   'kernel/pm/pm_control_syscall_internal.h')),
 ('tests/host/pm_control_test.c',
  ('tests/host/pm_control_test_fixture.c',
   'tests/host/pm_control_abi_test.c',
   'tests/host/pm_control_syscall_test.c',
   'tests/host/pm_control_reservation_test.c',
   'tests/host/pm_control_runtime_test.c',
   'tests/host/pm_control_test_internal.h')),
 ('tests/host/pm_control_execution_test.c',
  ('tests/host/pm_control_execution_test_fixture.c',
   'tests/host/pm_control_execution_prepare_test.c',
   'tests/host/pm_control_execution_token_test.c',
   'tests/host/pm_control_execution_exit_test.c',
   'tests/host/pm_control_execution_test_internal.h')),
 ('servers/vm/vm_core.c',
  ('servers/vm/vm_validation.c',
   'servers/vm/vm_frames.c',
   'servers/vm/vm_load.c',
   'servers/vm/vm_exit.c',
   'servers/vm/vm_internal.h',
   'servers/vm/vm_service_core.h',
   'servers/vm/vm_service_core.c',
   'servers/vm/vm_service_protocol.c',
   'servers/vm/vm_service_load.c',
   'servers/vm/vm_service_release.c',
   'servers/vm/vm_service_backend.c',
   'servers/vm/vm_service_internal.h')),
 ('servers/vm/vm_image.c',
  ('servers/vm/vm_stack.c', 'servers/vm/vm_elf.c', 'servers/vm/vm_image_internal.h')),
 ('tests/host/vm_core_test.c',
  ('tests/host/vm_core_test_fixture.c',
   'tests/host/vm_core_boot_test.c',
   'tests/host/vm_core_load_test.c',
   'tests/host/vm_core_abort_test.c',
   'tests/host/vm_core_validation_test.c',
   'tests/host/vm_core_test_internal.h',
   'tests/host/vm_service_test_fixture.c',
   'tests/host/vm_service_test.c',
   'tests/host/vm_service_protocol_test.c',
   'tests/host/vm_service_backend_test.c',
   'tests/host/vm_service_test_internal.h')),
 ('tests/qemu/vm_server.c',
  ('tests/qemu/vm_server_fixture.c',
   'tests/qemu/vm_server_handoff.c',
   'tests/qemu/vm_server_probe.c',
   'tests/qemu/vm_server_tty.c',
   'tests/qemu/vm_server_scratch.c',
   'tests/qemu/vm_server_pm.c',
   'tests/qemu/vm_server_control.c',
   'tests/qemu/vm_server_internal.h')),
 ('kernel/vm_scratch_preemption_test.c',
  ('kernel/vm_scratch_preemption_test_fixture.c',
   'kernel/vm_scratch_preemption_test_runtime.c',
   'kernel/vm_scratch_preemption_test_report.c',
   'kernel/vm_scratch_preemption_test_trap.c',
   'kernel/vm_scratch_preemption_test_internal.h',
   'kernel/tests/vm/vm_scratch_preemption_test.c',
   'kernel/tests/vm/vm_scratch_preemption_test_fixture.c',
   'kernel/tests/vm/vm_scratch_preemption_test_runtime.c',
   'kernel/tests/vm/vm_scratch_preemption_test_report.c',
   'kernel/tests/vm/vm_scratch_preemption_test_trap.c',
   'kernel/tests/vm/vm_scratch_preemption_test_internal.h')),
 ('servers/pm/pm_core.c',
  ('servers/pm/pm_common.c',
   'servers/pm/pm_protocol.c',
   'servers/pm/pm_validation.c',
   'servers/pm/pm_spawn.c',
   'servers/pm/pm_rollback.c',
   'servers/pm/pm_exit.c',
   'servers/pm/pm_wait.c',
   'servers/pm/pm_internal.h',
   'servers/pm/pm_coordinator.c',
   'servers/pm/pm_coordinator_protocol.c',
   'servers/pm/pm_coordinator_peers.c',
   'servers/pm/pm_coordinator_spawn.c',
   'servers/pm/pm_coordinator_rollback.c',
   'servers/pm/pm_coordinator_requests.c',
   'servers/pm/pm_coordinator_frontend.c',
   'servers/pm/pm_coordinator_exit.c',
   'servers/pm/pm_coordinator_wait.c',
   'servers/pm/pm_coordinator_internal.h',
   'servers/pm/pm_coordinator.h')),
 ('servers/pm/pm_service.c',
  ('servers/pm/pm_service_fixture.c',
   'servers/pm/pm_service_bootstrap.c',
   'servers/pm/pm_service_requests.c',
   'servers/pm/pm_service_self_test.c',
   'servers/pm/pm_service_scratch.c',
   'servers/pm/pm_service_internal.h',
   'servers/pm/pm_service_backend.c')),
 ('lib/spawn/spawn_request.c', ('lib/spawn/spawn_stack_layout.c',)),
 ('tests/host/pm_test.c',
  ('tests/host/pm_test_fixture.c',
   'tests/host/pm_protocol_test.c',
   'tests/host/pm_spawn_test.c',
   'tests/host/pm_validation_test.c',
   'tests/host/pm_exit_wait_test.c',
   'tests/host/pm_model_test.c',
   'tests/host/pm_model_reference.c',
   'tests/host/pm_model_operations.c',
   'tests/host/pm_test_internal.h',
   'tests/host/pm_model_test_internal.h',
   'tests/host/pm_coordinator_test.c',
   'tests/host/pm_coordinator_configuration_test.c',
   'tests/host/pm_coordinator_protocol_test.c',
   'tests/host/pm_coordinator_spawn_fixture.c',
   'tests/host/pm_coordinator_spawn_fixture.h',
   'tests/host/pm_coordinator_spawn_test.c',
   'tests/host/pm_coordinator_request_test.c',
   'tests/host/pm_coordinator_exit_fixture.c',
   'tests/host/pm_coordinator_exit_wait_test.c',
   'tests/host/pm_coordinator_backend_test.c',
   'tests/host/pm_coordinator_test_internal.h')),
 ('tests/host/spawn_abi_test.c',
  ('tests/host/spawn_abi_test_fixture.c',
   'tests/host/spawn_request_test.c',
   'tests/host/application_stack_test.c',
   'tests/host/vm_elf_test.c',
   'tests/host/spawn_abi_test_internal.h')),
 ('kernel/pm_service_test.c',
  ('kernel/pm_service_test_fixture.c',
   'kernel/pm_service_test_report.c',
   'kernel/pm_service_test_internal.h',
   'kernel/tests/pm/pm_service_test.c',
   'kernel/tests/pm/pm_service_test_fixture.c',
   'kernel/tests/pm/pm_service_test_report.c',
   'kernel/tests/pm/pm_service_test_internal.h')),
 ('kernel/ipc.c',
  ('kernel/ipc_common.c',
   'kernel/ipc_queue.c',
   'kernel/ipc_completion.c',
   'kernel/ipc_tokens.c',
   'kernel/ipc_notifications.c',
   'kernel/ipc_deadlock.c',
   'kernel/ipc_rendezvous.c',
   'kernel/ipc_close.c',
   'kernel/ipc_reply.c',
   'kernel/ipc_internal.h',
   'kernel/ipc/ipc.c',
   'kernel/ipc/ipc_common.c',
   'kernel/ipc/ipc_queue.c',
   'kernel/ipc/ipc_completion.c',
   'kernel/ipc/ipc_tokens.c',
   'kernel/ipc/ipc_notifications.c',
   'kernel/ipc/ipc_deadlock.c',
   'kernel/ipc/ipc_rendezvous.c',
   'kernel/ipc/ipc_close.c',
   'kernel/ipc/ipc_reply.c',
   'kernel/ipc/ipc_internal.h')),
 ('kernel/ipc_syscall.c',
  ('kernel/ipc_syscall_result.c',
   'kernel/ipc_syscall_request.c',
   'kernel/ipc_syscall_internal.h',
   'kernel/ipc/ipc_syscall.c',
   'kernel/ipc/ipc_syscall_result.c',
   'kernel/ipc/ipc_syscall_request.c',
   'kernel/ipc/ipc_syscall_internal.h')),
 ('tests/host/ipc_operation_test.c',
  ('tests/host/ipc_operation_test_fixture.c',
   'tests/host/ipc_send_operation_test.c',
   'tests/host/ipc_receive_operation_test.c',
   'tests/host/ipc_operation_test_internal.h')),
 ('tests/host/ipc_call_test.c',
  ('tests/host/ipc_call_test_fixture.c',
   'tests/host/ipc_call_delivery_test.c',
   'tests/host/ipc_call_validation_test.c',
   'tests/host/ipc_call_test_internal.h')),
 ('tests/host/ipc_reply_test.c',
  ('tests/host/ipc_reply_test_fixture.c',
   'tests/host/ipc_reply_authority_test.c',
   'tests/host/ipc_reply_validation_test.c',
   'tests/host/ipc_reply_receive_test.c',
   'tests/host/ipc_reply_test_internal.h')),
 ('tests/host/ipc_notify_test.c',
  ('tests/host/ipc_notify_test_fixture.c',
   'tests/host/ipc_notify_delivery_test.c',
   'tests/host/ipc_kernel_notify_test.c',
   'tests/host/ipc_notify_validation_test.c',
   'tests/host/ipc_notify_model_test.c',
   'tests/host/ipc_notify_test_internal.h')),
 ('tests/host/ipc_close_test.c',
  ('tests/host/ipc_close_test_fixture.c',
   'tests/host/ipc_close_cancellation_test.c',
   'tests/host/ipc_close_validation_test.c',
   'tests/host/ipc_close_model_test.c',
   'tests/host/ipc_close_test_internal.h')),
 ('tests/host/ipc_model_test.c',
  ('tests/host/ipc_model_test_common.c',
   'tests/host/ipc_model_test_fixture.c',
   'tests/host/ipc_model_test_trace.c',
   'tests/host/ipc_model_reference_authority.c',
   'tests/host/ipc_model_reference_scheduler.c',
   'tests/host/ipc_model_reference_ipc_state.c',
   'tests/host/ipc_model_reference_rendezvous.c',
   'tests/host/ipc_model_reference_notifications.c',
   'tests/host/ipc_model_reference_lifecycle.c',
   'tests/host/ipc_model_reference.c',
   'tests/host/ipc_model_test_production.c',
   'tests/host/ipc_model_test_step.c',
   'tests/host/ipc_model_test_actions.c',
   'tests/host/ipc_model_test_prelude.c',
   'tests/host/ipc_model_test_internal.h')),
 ('kernel/ipc_test.c',
  ('kernel/ipc_test_fixture.c',
   'kernel/ipc_test_setup.c',
   'kernel/ipc_test_scenarios.c',
   'kernel/ipc_test_report.c',
   'kernel/ipc_test_internal.h',
   'kernel/tests/ipc/ipc_test.c',
   'kernel/tests/ipc/ipc_test_fixture.c',
   'kernel/tests/ipc/ipc_test_setup.c',
   'kernel/tests/ipc/ipc_test_scenarios.c',
   'kernel/tests/ipc/ipc_test_report.c',
   'kernel/tests/ipc/ipc_test_internal.h')),
 ('kernel/ipc_ecall_test.c',
  ('kernel/ipc_ecall_test_fixture.c',
   'kernel/ipc_ecall_test_setup.c',
   'kernel/ipc_ecall_test_report.c',
   'kernel/ipc_ecall_test_internal.h',
   'kernel/tests/ipc/ipc_ecall_test.c',
   'kernel/tests/ipc/ipc_ecall_test_fixture.c',
   'kernel/tests/ipc/ipc_ecall_test_setup.c',
   'kernel/tests/ipc/ipc_ecall_test_report.c',
   'kernel/tests/ipc/ipc_ecall_test_internal.h')),
 ('kernel/ipc_syscall_test.c',
  ('kernel/ipc_syscall_test_fixture.c',
   'kernel/ipc_syscall_test_setup.c',
   'kernel/ipc_syscall_test_commands.c',
   'kernel/ipc_syscall_test_scenarios.c',
   'kernel/ipc_syscall_test_hooks.c',
   'kernel/ipc_syscall_test_report.c',
   'kernel/ipc_syscall_test_internal.h',
   'kernel/tests/ipc/ipc_syscall_test.c',
   'kernel/tests/ipc/ipc_syscall_test_fixture.c',
   'kernel/tests/ipc/ipc_syscall_test_setup.c',
   'kernel/tests/ipc/ipc_syscall_test_commands.c',
   'kernel/tests/ipc/ipc_syscall_test_scenarios.c',
   'kernel/tests/ipc/ipc_syscall_test_hooks.c',
   'kernel/tests/ipc/ipc_syscall_test_report.c',
   'kernel/tests/ipc/ipc_syscall_test_internal.h')),
 ('kernel/bootstrap_manifest.c',
  ('kernel/bootstrap_manifest_common.c',
   'kernel/bootstrap_manifest_profiles.c',
   'kernel/bootstrap_transitions.c',
   'kernel/bootstrap_manifest_internal.h',
   'kernel/boot/bootstrap_manifest.c',
   'kernel/boot/bootstrap_manifest_common.c',
   'kernel/boot/bootstrap_manifest_profiles.c',
   'kernel/boot/bootstrap_transitions.c',
   'kernel/boot/bootstrap_manifest_internal.h')),
 ('kernel/bootstrap_runtime.c',
  ('kernel/bootstrap_runtime_state.c',
   'kernel/bootstrap_preparation.c',
   'kernel/bootstrap_preparation_image.c',
   'kernel/bootstrap_preparation_rollback.c',
   'kernel/bootstrap_runtime_validation.c',
   'kernel/bootstrap_diagnostics.c',
   'kernel/bootstrap_runtime_internal.h',
   'kernel/boot/bootstrap_runtime.c',
   'kernel/boot/bootstrap_runtime_state.c',
   'kernel/boot/bootstrap_preparation.c',
   'kernel/boot/bootstrap_preparation_image.c',
   'kernel/boot/bootstrap_preparation_rollback.c',
   'kernel/boot/bootstrap_runtime_validation.c',
   'kernel/boot/bootstrap_diagnostics.c',
   'kernel/boot/bootstrap_runtime_internal.h')),
 ('kernel/bootstrap_syscall.c',
  ('kernel/bootstrap_syscall_common.c',
   'kernel/bootstrap_syscall_release.c',
   'kernel/bootstrap_syscall_ready.c',
   'kernel/bootstrap_syscall_complete.c',
   'kernel/bootstrap_syscall_failure.c',
   'kernel/bootstrap_syscall_internal.h',
   'kernel/boot/bootstrap_syscall.c',
   'kernel/boot/bootstrap_syscall_common.c',
   'kernel/boot/bootstrap_syscall_release.c',
   'kernel/boot/bootstrap_syscall_ready.c',
   'kernel/boot/bootstrap_syscall_complete.c',
   'kernel/boot/bootstrap_syscall_failure.c',
   'kernel/boot/bootstrap_syscall_internal.h')),
 ('kernel/main.c',
  ('kernel/boot_foundation.c',
   'kernel/boot_component_tests.c',
   'kernel/boot_internal.h',
   'kernel/boot/main.c',
   'kernel/boot/boot_foundation.c',
   'kernel/boot/boot_component_tests.c',
   'kernel/boot/boot_internal.h')),
 ('tests/qemu/bootstrap_launcher.c',
  ('tests/qemu/bootstrap_launcher_common.c',
   'tests/qemu/bootstrap_launcher_manifest.c',
   'tests/qemu/bootstrap_launcher_protocol.c',
   'tests/qemu/bootstrap_launcher_control.c',
   'tests/qemu/bootstrap_launcher_internal.h')),
 ('tests/host/bootstrap_manifest_test.c',
  ('tests/host/bootstrap_manifest_fixture.c',
   'tests/host/bootstrap_manifest_acceptance_test.c',
   'tests/host/bootstrap_manifest_topology_test.c',
   'tests/host/bootstrap_manifest_tty_test.c',
   'tests/host/bootstrap_runtime_test.c',
   'tests/host/bootstrap_runtime_model.c',
   'tests/host/bootstrap_runtime_reference.c',
   'tests/host/bootstrap_manifest_test_internal.h',
   'tests/host/bootstrap_runtime_model_internal.h')),
 ('tests/host/bootstrap_control_test.c',
  ('tests/host/bootstrap_control_fixture.c',
   'tests/host/bootstrap_control_abi_test.c',
   'tests/host/bootstrap_control_lifecycle_test.c',
   'tests/host/bootstrap_control_completion_test.c',
   'tests/host/bootstrap_control_image_catalog_test.c',
   'tests/host/bootstrap_control_test_internal.h')),
 ('tests/host/fdt_test.c',
  ('tests/host/fdt_test_fixture.c',
   'tests/host/fdt_header_test.c',
   'tests/host/fdt_memory_test.c',
   'tests/host/fdt_test_internal.h')),
 ('kernel/trap.c',
  ('kernel/trap_context.c',
   'kernel/trap_user.c',
   'kernel/trap_interrupt.c',
   'kernel/trap_syscall.c',
   'kernel/trap_fault.c',
   'kernel/trap_test.c',
   'kernel/trap_test_hooks.c',
   'kernel/trap_internal.h',
   'kernel/trap/trap.c',
   'kernel/trap/trap_context.c',
   'kernel/trap/trap_user.c',
   'kernel/trap/trap_interrupt.c',
   'kernel/trap/trap_syscall.c',
   'kernel/trap/trap_fault.c',
   'kernel/tests/trap/trap_test.c',
   'kernel/trap/trap_test_hooks.c',
   'kernel/trap/trap_internal.h')),
 ('kernel/syscall.c',
  ('kernel/syscall_decode.c',
   'kernel/syscall_context.c',
   'kernel/syscall_internal.h',
   'kernel/trap/syscall.c',
   'kernel/trap/syscall_decode.c',
   'kernel/trap/syscall_context.c',
   'kernel/trap/syscall_internal.h')),
 ('kernel/user_runtime_test.c',
  ('kernel/user_runtime_test_state.c',
   'kernel/user_runtime_test_image.c',
   'kernel/user_runtime_test_setup.c',
   'kernel/user_runtime_test_trap.c',
   'kernel/user_runtime_test_cleanup.c',
   'kernel/user_runtime_test_report.c',
   'kernel/user_runtime_test_internal.h',
   'kernel/tests/runtime/user_runtime_test.c',
   'kernel/tests/runtime/user_runtime_test_state.c',
   'kernel/tests/runtime/user_runtime_test_image.c',
   'kernel/tests/runtime/user_runtime_test_setup.c',
   'kernel/tests/runtime/user_runtime_test_trap.c',
   'kernel/tests/runtime/user_runtime_test_cleanup.c',
   'kernel/tests/runtime/user_runtime_test_report.c',
   'kernel/tests/runtime/user_runtime_test_internal.h')),
 ('tests/qemu/user_runtime_service.c',
  ('tests/qemu/user_runtime_service_common.c',
   'tests/qemu/user_runtime_client.c',
   'tests/qemu/user_runtime_responders.c',
   'tests/qemu/user_runtime_service_internal.h')),
 ('tests/host/runtime_test.c',
  ('tests/host/runtime_test_fixture.c',
   'tests/host/runtime_wrapper_test.c',
   'tests/host/runtime_test_internal.h')),
 ('kernel/scheduler_core.c',
  ('kernel/scheduler_core_queue.c',
   'kernel/scheduler_core_validate.c',
   'kernel/scheduler_core_policy.c',
   'kernel/scheduler_core_ipc.c',
   'kernel/scheduler_core_ipc_transition.c',
   'kernel/scheduler_core_return_plan.c',
   'kernel/scheduler_core_return.c',
   'kernel/scheduler_core_accounting.c',
   'kernel/scheduler_core_private.h',
   'kernel/scheduler/scheduler_core.c',
   'kernel/scheduler/scheduler_core_queue.c',
   'kernel/scheduler/scheduler_core_validate.c',
   'kernel/scheduler/scheduler_core_policy.c',
   'kernel/scheduler/scheduler_core_ipc.c',
   'kernel/scheduler/scheduler_core_ipc_transition.c',
   'kernel/scheduler/scheduler_core_return_plan.c',
   'kernel/scheduler/scheduler_core_return.c',
   'kernel/scheduler/scheduler_core_accounting.c',
   'kernel/scheduler/scheduler_core_private.h')),
 ('kernel/scheduler.c',
  ('kernel/scheduler_context.c',
   'kernel/scheduler_completion.c',
   'kernel/scheduler_return.c',
   'kernel/scheduler_interrupt.c',
   'kernel/scheduler_idle.c',
   'kernel/scheduler_test_support.c',
   'kernel/scheduler_runtime_internal.h',
   'kernel/scheduler/scheduler.c',
   'kernel/scheduler/scheduler_context.c',
   'kernel/scheduler/scheduler_completion.c',
   'kernel/scheduler/scheduler_return.c',
   'kernel/scheduler/scheduler_interrupt.c',
   'kernel/scheduler/scheduler_idle.c',
   'kernel/tests/scheduler/scheduler_test_support.c',
   'kernel/scheduler/scheduler_runtime_internal.h')),
 ('kernel/scheduler_test.c',
  ('kernel/scheduler_test_state.c',
   'kernel/scheduler_test_fixture.c',
   'kernel/scheduler_test_completion.c',
   'kernel/scheduler_test_interrupt.c',
   'kernel/scheduler_test_trap.c',
   'kernel/scheduler_test_finish.c',
   'kernel/scheduler_test_internal.h',
   'kernel/tests/scheduler/scheduler_test.c',
   'kernel/tests/scheduler/scheduler_test_state.c',
   'kernel/tests/scheduler/scheduler_test_fixture.c',
   'kernel/tests/scheduler/scheduler_test_completion.c',
   'kernel/tests/scheduler/scheduler_test_interrupt.c',
   'kernel/tests/scheduler/scheduler_test_trap.c',
   'kernel/tests/scheduler/scheduler_test_finish.c',
   'kernel/tests/scheduler/scheduler_test_internal.h')),
 ('kernel/scheduler_invalid_test.c',
  ('kernel/scheduler_invalid_test_state.c',
   'kernel/scheduler_invalid_test_fixture.c',
   'kernel/scheduler_invalid_test_observation.c',
   'kernel/scheduler_invalid_test_internal.h',
   'kernel/tests/scheduler/scheduler_invalid_test.c',
   'kernel/tests/scheduler/scheduler_invalid_test_state.c',
   'kernel/tests/scheduler/scheduler_invalid_test_fixture.c',
   'kernel/tests/scheduler/scheduler_invalid_test_observation.c',
   'kernel/tests/scheduler/scheduler_invalid_test_internal.h')),
 ('tests/host/scheduler_test.c',
  ('tests/host/scheduler_test_fixture.c',
   'tests/host/scheduler_queue_test.c',
   'tests/host/scheduler_return_test.c',
   'tests/host/scheduler_accounting_test.c',
   'tests/host/scheduler_test_internal.h')),
 ('tests/host/scheduler_model_test.c',
  ('tests/host/scheduler_model_fixture.c',
   'tests/host/scheduler_model_reference.c',
   'tests/host/scheduler_model_projection.c',
   'tests/host/scheduler_model_prelude.c',
   'tests/host/scheduler_model_operations.c',
   'tests/host/scheduler_model_internal.h')),
 ('kernel/grant_copy.c',
  ('kernel/grant_copy_range.c',
   'kernel/grant_copy_internal.h',
   'kernel/grant/grant_copy.c',
   'kernel/grant/grant_copy_range.c',
   'kernel/grant/grant_copy_internal.h')),
 ('tests/host/grant_test.c',
  ('tests/host/grant_test_fixture.c',
   'tests/host/grant_lifecycle_test.c',
   'tests/host/grant_validation_test.c',
   'tests/host/grant_model_test.c',
   'tests/host/grant_model_reference.c',
   'tests/host/grant_model_operations.c',
   'tests/host/grant_model_copy.c',
   'tests/host/grant_test_internal.h')),
 ('kernel/grant_syscall_test.c',
  ('kernel/grant_syscall_test_fixture.c',
   'kernel/grant_syscall_test_commands.c',
   'kernel/grant_syscall_test_copy_commands.c',
   'kernel/grant_syscall_test_transitions.c',
   'kernel/grant_syscall_test_effects.c',
   'kernel/grant_syscall_test_hooks.c',
   'kernel/grant_syscall_test_lifecycle.c',
   'kernel/grant_syscall_test_internal.h',
   'kernel/tests/grant/grant_syscall_test.c',
   'kernel/tests/grant/grant_syscall_test_fixture.c',
   'kernel/tests/grant/grant_syscall_test_commands.c',
   'kernel/tests/grant/grant_syscall_test_copy_commands.c',
   'kernel/tests/grant/grant_syscall_test_transitions.c',
   'kernel/tests/grant/grant_syscall_test_effects.c',
   'kernel/tests/grant/grant_syscall_test_hooks.c',
   'kernel/tests/grant/grant_syscall_test_lifecycle.c',
   'kernel/tests/grant/grant_syscall_test_internal.h')),
 ('kernel/address_space_handoff_test.h', ('kernel/tests/memory/address_space_handoff_test.h',)),
 ('kernel/address_space_handoff_test_fixture.c',
  ('kernel/tests/memory/address_space_handoff_test_fixture.c',)),
 ('kernel/address_space_handoff_test_handoff.c',
  ('kernel/tests/memory/address_space_handoff_test_handoff.c',)),
 ('kernel/address_space_handoff_test_internal.h',
  ('kernel/tests/memory/address_space_handoff_test_internal.h',)),
 ('kernel/address_space_handoff_test_runtime.c',
  ('kernel/tests/memory/address_space_handoff_test_runtime.c',)),
 ('kernel/address_space_handoff_test_setup.c',
  ('kernel/tests/memory/address_space_handoff_test_setup.c',)),
 ('kernel/address_space_handoff_test_snapshot.c',
  ('kernel/tests/memory/address_space_handoff_test_snapshot.c',)),
 ('kernel/address_space_handoff_test_syscall.c',
  ('kernel/tests/memory/address_space_handoff_test_syscall.c',)),
 ('kernel/address_space_handoff_test_trap.c',
  ('kernel/tests/memory/address_space_handoff_test_trap.c',)),
 ('kernel/address_space_handoff_test_tty.c',
  ('kernel/tests/memory/address_space_handoff_test_tty.c',)),
 ('kernel/address_space_internal.h', ('kernel/memory/address_space_internal.h',)),
 ('kernel/address_space_validation.c', ('kernel/memory/address_space_validation.c',)),
 ('kernel/boot_component_tests.c', ('kernel/boot/boot_component_tests.c',)),
 ('kernel/boot_foundation.c', ('kernel/boot/boot_foundation.c',)),
 ('kernel/boot_internal.h', ('kernel/boot/boot_internal.h',)),
 ('kernel/bootstrap_control.c', ('kernel/boot/bootstrap_control.c',)),
 ('kernel/bootstrap_control_core.c', ('kernel/boot/bootstrap_control_core.c',)),
 ('kernel/bootstrap_control_internal.h', ('kernel/boot/bootstrap_control_internal.h',)),
 ('kernel/bootstrap_diagnostics.c', ('kernel/boot/bootstrap_diagnostics.c',)),
 ('kernel/bootstrap_image.c', ('kernel/boot/bootstrap_image.c',)),
 ('kernel/bootstrap_image.h', ('kernel/boot/bootstrap_image.h',)),
 ('kernel/bootstrap_manifest_common.c', ('kernel/boot/bootstrap_manifest_common.c',)),
 ('kernel/bootstrap_manifest_internal.h', ('kernel/boot/bootstrap_manifest_internal.h',)),
 ('kernel/bootstrap_manifest_profiles.c', ('kernel/boot/bootstrap_manifest_profiles.c',)),
 ('kernel/bootstrap_memory.c', ('kernel/memory/bootstrap_memory.c',)),
 ('kernel/bootstrap_preparation.c', ('kernel/boot/bootstrap_preparation.c',)),
 ('kernel/bootstrap_preparation_image.c', ('kernel/boot/bootstrap_preparation_image.c',)),
 ('kernel/bootstrap_preparation_rollback.c', ('kernel/boot/bootstrap_preparation_rollback.c',)),
 ('kernel/bootstrap_runtime.h', ('kernel/boot/bootstrap_runtime.h',)),
 ('kernel/bootstrap_runtime_internal.h', ('kernel/boot/bootstrap_runtime_internal.h',)),
 ('kernel/bootstrap_runtime_state.c', ('kernel/boot/bootstrap_runtime_state.c',)),
 ('kernel/bootstrap_runtime_validation.c', ('kernel/boot/bootstrap_runtime_validation.c',)),
 ('kernel/bootstrap_syscall.h', ('kernel/boot/bootstrap_syscall.h',)),
 ('kernel/bootstrap_syscall_common.c', ('kernel/boot/bootstrap_syscall_common.c',)),
 ('kernel/bootstrap_syscall_complete.c', ('kernel/boot/bootstrap_syscall_complete.c',)),
 ('kernel/bootstrap_syscall_failure.c', ('kernel/boot/bootstrap_syscall_failure.c',)),
 ('kernel/bootstrap_syscall_internal.h', ('kernel/boot/bootstrap_syscall_internal.h',)),
 ('kernel/bootstrap_syscall_ready.c', ('kernel/boot/bootstrap_syscall_ready.c',)),
 ('kernel/bootstrap_syscall_release.c', ('kernel/boot/bootstrap_syscall_release.c',)),
 ('kernel/bootstrap_test.c', ('kernel/tests/boot/bootstrap_test.c',)),
 ('kernel/bootstrap_test.h', ('kernel/tests/boot/bootstrap_test.h',)),
 ('kernel/bootstrap_test_fixture.h', ('kernel/tests/boot/bootstrap_test_fixture.h',)),
 ('kernel/bootstrap_transitions.c', ('kernel/boot/bootstrap_transitions.c',)),
 ('kernel/elf_spawn_test.c', ('kernel/tests/spawn/elf_spawn_test.c',)),
 ('kernel/elf_spawn_test.h', ('kernel/tests/spawn/elf_spawn_test.h',)),
 ('kernel/elf_spawn_test_capture.c', ('kernel/tests/spawn/elf_spawn_test_capture.c',)),
 ('kernel/elf_spawn_test_compare.c', ('kernel/tests/spawn/elf_spawn_test_compare.c',)),
 ('kernel/elf_spawn_test_fixture.c', ('kernel/tests/spawn/elf_spawn_test_fixture.c',)),
 ('kernel/elf_spawn_test_fixture.h', ('kernel/tests/spawn/elf_spawn_test_fixture.h',)),
 ('kernel/elf_spawn_test_internal.h', ('kernel/tests/spawn/elf_spawn_test_internal.h',)),
 ('kernel/elf_spawn_test_report.c', ('kernel/tests/spawn/elf_spawn_test_report.c',)),
 ('kernel/elf_spawn_test_runtime.c', ('kernel/tests/spawn/elf_spawn_test_runtime.c',)),
 ('kernel/elf_spawn_test_service_checks.c',
  ('kernel/tests/spawn/elf_spawn_test_service_checks.c',)),
 ('kernel/elf_spawn_test_service_compare.c',
  ('kernel/tests/spawn/elf_spawn_test_service_compare.c',)),
 ('kernel/elf_spawn_test_services.c', ('kernel/tests/spawn/elf_spawn_test_services.c',)),
 ('kernel/endpoint_close.c', ('kernel/endpoint/endpoint_close.c',)),
 ('kernel/endpoint_codec.c', ('kernel/endpoint/endpoint_codec.c',)),
 ('kernel/endpoint_completion_validation.c', ('kernel/endpoint/endpoint_completion_validation.c',)),
 ('kernel/endpoint_identity.c', ('kernel/endpoint/endpoint_identity.c',)),
 ('kernel/endpoint_internal.h', ('kernel/endpoint/endpoint_internal.h',)),
 ('kernel/endpoint_object_validation.c', ('kernel/endpoint/endpoint_object_validation.c',)),
 ('kernel/endpoint_private.h', ('kernel/endpoint/endpoint_private.h',)),
 ('kernel/endpoint_privilege.c', ('kernel/endpoint/endpoint_privilege.c',)),
 ('kernel/endpoint_publication.c', ('kernel/endpoint/endpoint_publication.c',)),
 ('kernel/endpoint_queue_validation.c', ('kernel/endpoint/endpoint_queue_validation.c',)),
 ('kernel/endpoint_registry.c', ('kernel/endpoint/endpoint_registry.c',)),
 ('kernel/endpoint_test_fixture.c', ('kernel/tests/endpoint/endpoint_test_fixture.c',)),
 ('kernel/endpoint_test_internal.h', ('kernel/tests/endpoint/endpoint_test_internal.h',)),
 ('kernel/endpoint_test_scenarios.c', ('kernel/tests/endpoint/endpoint_test_scenarios.c',)),
 ('kernel/fdt.c', ('kernel/platform/fdt.c',)),
 ('kernel/frame_allocator.c', ('kernel/memory/frame_allocator.c',)),
 ('kernel/frame_ownership_bootstrap.c', ('kernel/memory/frame_ownership_bootstrap.c',)),
 ('kernel/frame_ownership_handoff.c', ('kernel/memory/frame_ownership_handoff.c',)),
 ('kernel/frame_ownership_internal.h', ('kernel/memory/frame_ownership_internal.h',)),
 ('kernel/frame_ownership_test_fixture.c', ('kernel/tests/memory/frame_ownership_test_fixture.c',)),
 ('kernel/frame_ownership_test_handoff.c', ('kernel/tests/memory/frame_ownership_test_handoff.c',)),
 ('kernel/frame_ownership_test_internal.h',
  ('kernel/tests/memory/frame_ownership_test_internal.h',)),
 ('kernel/frame_ownership_test_lifecycle.c',
  ('kernel/tests/memory/frame_ownership_test_lifecycle.c',)),
 ('kernel/frame_ownership_validation.c', ('kernel/memory/frame_ownership_validation.c',)),
 ('kernel/frame_ownership_vm_claim.c', ('kernel/memory/frame_ownership_vm_claim.c',)),
 ('kernel/frame_ownership_vm_freeze.c', ('kernel/memory/frame_ownership_vm_freeze.c',)),
 ('kernel/frame_ownership_vm_release.c', ('kernel/memory/frame_ownership_vm_release.c',)),
 ('kernel/grant.c', ('kernel/grant/grant.c',)),
 ('kernel/grant_abi.c', ('kernel/grant/grant_abi.c',)),
 ('kernel/grant_copy_core.c', ('kernel/grant/grant_copy_core.c',)),
 ('kernel/grant_copy_internal.h', ('kernel/grant/grant_copy_internal.h',)),
 ('kernel/grant_copy_range.c', ('kernel/grant/grant_copy_range.c',)),
 ('kernel/grant_internal.h', ('kernel/grant/grant_internal.h',)),
 ('kernel/grant_runtime.c', ('kernel/grant/grant_runtime.c',)),
 ('kernel/grant_runtime_internal.h', ('kernel/grant/grant_runtime_internal.h',)),
 ('kernel/grant_syscall.c', ('kernel/grant/grant_syscall.c',)),
 ('kernel/grant_syscall.h', ('kernel/grant/grant_syscall.h',)),
 ('kernel/grant_syscall_core.c', ('kernel/grant/grant_syscall_core.c',)),
 ('kernel/grant_syscall_test.h', ('kernel/tests/grant/grant_syscall_test.h',)),
 ('kernel/grant_syscall_test_commands.c', ('kernel/tests/grant/grant_syscall_test_commands.c',)),
 ('kernel/grant_syscall_test_copy_commands.c',
  ('kernel/tests/grant/grant_syscall_test_copy_commands.c',)),
 ('kernel/grant_syscall_test_effects.c', ('kernel/tests/grant/grant_syscall_test_effects.c',)),
 ('kernel/grant_syscall_test_fixture.c', ('kernel/tests/grant/grant_syscall_test_fixture.c',)),
 ('kernel/grant_syscall_test_hooks.c', ('kernel/tests/grant/grant_syscall_test_hooks.c',)),
 ('kernel/grant_syscall_test_internal.h', ('kernel/tests/grant/grant_syscall_test_internal.h',)),
 ('kernel/grant_syscall_test_lifecycle.c', ('kernel/tests/grant/grant_syscall_test_lifecycle.c',)),
 ('kernel/grant_syscall_test_transitions.c',
  ('kernel/tests/grant/grant_syscall_test_transitions.c',)),
 ('kernel/grant_test.c', ('kernel/tests/grant/grant_test.c',)),
 ('kernel/grant_test.h', ('kernel/tests/grant/grant_test.h',)),
 ('kernel/ipc_abi.c', ('kernel/ipc/ipc_abi.c',)),
 ('kernel/ipc_buffer.c', ('kernel/ipc/ipc_buffer.c',)),
 ('kernel/ipc_buffer_internal.h', ('kernel/ipc/ipc_buffer_internal.h',)),
 ('kernel/ipc_close.c', ('kernel/ipc/ipc_close.c',)),
 ('kernel/ipc_common.c', ('kernel/ipc/ipc_common.c',)),
 ('kernel/ipc_completion.c', ('kernel/ipc/ipc_completion.c',)),
 ('kernel/ipc_deadlock.c', ('kernel/ipc/ipc_deadlock.c',)),
 ('kernel/ipc_ecall_test.h', ('kernel/tests/ipc/ipc_ecall_test.h',)),
 ('kernel/ipc_ecall_test_fixture.c', ('kernel/tests/ipc/ipc_ecall_test_fixture.c',)),
 ('kernel/ipc_ecall_test_internal.h', ('kernel/tests/ipc/ipc_ecall_test_internal.h',)),
 ('kernel/ipc_ecall_test_report.c', ('kernel/tests/ipc/ipc_ecall_test_report.c',)),
 ('kernel/ipc_ecall_test_setup.c', ('kernel/tests/ipc/ipc_ecall_test_setup.c',)),
 ('kernel/ipc_internal.h', ('kernel/ipc/ipc_internal.h',)),
 ('kernel/ipc_notifications.c', ('kernel/ipc/ipc_notifications.c',)),
 ('kernel/ipc_queue.c', ('kernel/ipc/ipc_queue.c',)),
 ('kernel/ipc_rendezvous.c', ('kernel/ipc/ipc_rendezvous.c',)),
 ('kernel/ipc_reply.c', ('kernel/ipc/ipc_reply.c',)),
 ('kernel/ipc_runtime.c', ('kernel/ipc/ipc_runtime.c',)),
 ('kernel/ipc_runtime_internal.h', ('kernel/ipc/ipc_runtime_internal.h',)),
 ('kernel/ipc_syscall.h', ('kernel/ipc/ipc_syscall.h',)),
 ('kernel/ipc_syscall_internal.h', ('kernel/ipc/ipc_syscall_internal.h',)),
 ('kernel/ipc_syscall_panic_test.c', ('kernel/tests/ipc/ipc_syscall_panic_test.c',)),
 ('kernel/ipc_syscall_panic_test.h', ('kernel/tests/ipc/ipc_syscall_panic_test.h',)),
 ('kernel/ipc_syscall_request.c', ('kernel/ipc/ipc_syscall_request.c',)),
 ('kernel/ipc_syscall_result.c', ('kernel/ipc/ipc_syscall_result.c',)),
 ('kernel/ipc_syscall_test.h', ('kernel/tests/ipc/ipc_syscall_test.h',)),
 ('kernel/ipc_syscall_test_commands.c', ('kernel/tests/ipc/ipc_syscall_test_commands.c',)),
 ('kernel/ipc_syscall_test_fixture.c', ('kernel/tests/ipc/ipc_syscall_test_fixture.c',)),
 ('kernel/ipc_syscall_test_hooks.c', ('kernel/tests/ipc/ipc_syscall_test_hooks.c',)),
 ('kernel/ipc_syscall_test_internal.h', ('kernel/tests/ipc/ipc_syscall_test_internal.h',)),
 ('kernel/ipc_syscall_test_report.c', ('kernel/tests/ipc/ipc_syscall_test_report.c',)),
 ('kernel/ipc_syscall_test_scenarios.c', ('kernel/tests/ipc/ipc_syscall_test_scenarios.c',)),
 ('kernel/ipc_syscall_test_setup.c', ('kernel/tests/ipc/ipc_syscall_test_setup.c',)),
 ('kernel/ipc_test_fixture.c', ('kernel/tests/ipc/ipc_test_fixture.c',)),
 ('kernel/ipc_test_internal.h', ('kernel/tests/ipc/ipc_test_internal.h',)),
 ('kernel/ipc_test_report.c', ('kernel/tests/ipc/ipc_test_report.c',)),
 ('kernel/ipc_test_scenarios.c', ('kernel/tests/ipc/ipc_test_scenarios.c',)),
 ('kernel/ipc_test_setup.c', ('kernel/tests/ipc/ipc_test_setup.c',)),
 ('kernel/ipc_tokens.c', ('kernel/ipc/ipc_tokens.c',)),
 ('kernel/kernel_object_context.c', ('kernel/objects/kernel_object_context.c',)),
 ('kernel/kernel_object_hart.c', ('kernel/objects/kernel_object_hart.c',)),
 ('kernel/kernel_object_identity.c', ('kernel/objects/kernel_object_identity.c',)),
 ('kernel/kernel_object_private.h', ('kernel/objects/kernel_object_private.h',)),
 ('kernel/kernel_object_process.c', ('kernel/objects/kernel_object_process.c',)),
 ('kernel/kernel_object_runtime.c', ('kernel/objects/kernel_object_runtime.c',)),
 ('kernel/kernel_object_runtime_internal.h', ('kernel/objects/kernel_object_runtime_internal.h',)),
 ('kernel/kernel_object_stack.c', ('kernel/objects/kernel_object_stack.c',)),
 ('kernel/kernel_object_state.c', ('kernel/objects/kernel_object_state.c',)),
 ('kernel/kernel_object_thread.c', ('kernel/objects/kernel_object_thread.c',)),
 ('kernel/kernel_object_validation.c', ('kernel/objects/kernel_object_validation.c',)),
 ('kernel/mmu_test.c', ('kernel/tests/memory/mmu_test.c',)),
 ('kernel/object_model_test_internal.h', ('kernel/tests/objects/object_model_test_internal.h',)),
 ('kernel/object_model_test_scenarios.c', ('kernel/tests/objects/object_model_test_scenarios.c',)),
 ('kernel/panic.c', ('kernel/diagnostic/panic.c',)),
 ('kernel/plic.c', ('kernel/platform/plic.c',)),
 ('kernel/plic.h', ('kernel/platform/plic.h',)),
 ('kernel/plic_core.c', ('kernel/platform/plic_core.c',)),
 ('kernel/plic_core.h', ('kernel/platform/plic_core.h',)),
 ('kernel/pm_control_abi.c', ('kernel/pm/pm_control_abi.c',)),
 ('kernel/pm_control_activation.c', ('kernel/pm/pm_control_activation.c',)),
 ('kernel/pm_control_core.h', ('kernel/pm/pm_control_core.h',)),
 ('kernel/pm_control_execution.c', ('kernel/pm/pm_control_execution.c',)),
 ('kernel/pm_control_exit.c', ('kernel/pm/pm_control_exit.c',)),
 ('kernel/pm_control_internal.h', ('kernel/pm/pm_control_internal.h',)),
 ('kernel/pm_control_reservation.c', ('kernel/pm/pm_control_reservation.c',)),
 ('kernel/pm_control_runtime.c', ('kernel/pm/pm_control_runtime.c',)),
 ('kernel/pm_control_runtime.h', ('kernel/pm/pm_control_runtime.h',)),
 ('kernel/pm_control_runtime_core.c', ('kernel/pm/pm_control_runtime_core.c',)),
 ('kernel/pm_control_runtime_core.h', ('kernel/pm/pm_control_runtime_core.h',)),
 ('kernel/pm_control_syscall.h', ('kernel/pm/pm_control_syscall.h',)),
 ('kernel/pm_control_syscall_activation.c', ('kernel/pm/pm_control_syscall_activation.c',)),
 ('kernel/pm_control_syscall_core.c', ('kernel/pm/pm_control_syscall_core.c',)),
 ('kernel/pm_control_syscall_core.h', ('kernel/pm/pm_control_syscall_core.h',)),
 ('kernel/pm_control_syscall_execution.c', ('kernel/pm/pm_control_syscall_execution.c',)),
 ('kernel/pm_control_syscall_exit.c', ('kernel/pm/pm_control_syscall_exit.c',)),
 ('kernel/pm_control_syscall_internal.h', ('kernel/pm/pm_control_syscall_internal.h',)),
 ('kernel/pm_control_syscall_reservation.c', ('kernel/pm/pm_control_syscall_reservation.c',)),
 ('kernel/pm_control_syscall_runtime.c', ('kernel/pm/pm_control_syscall_runtime.c',)),
 ('kernel/pm_control_validation.c', ('kernel/pm/pm_control_validation.c',)),
 ('kernel/pm_service_test.h', ('kernel/tests/pm/pm_service_test.h',)),
 ('kernel/pm_service_test_fixture.c', ('kernel/tests/pm/pm_service_test_fixture.c',)),
 ('kernel/pm_service_test_fixture.h', ('kernel/tests/pm/pm_service_test_fixture.h',)),
 ('kernel/pm_service_test_internal.h', ('kernel/tests/pm/pm_service_test_internal.h',)),
 ('kernel/pm_service_test_report.c', ('kernel/tests/pm/pm_service_test_report.c',)),
 ('kernel/ramfs_service_test.h', ('kernel/tests/ramfs/ramfs_service_test.h',)),
 ('kernel/ramfs_service_test_fixture.c', ('kernel/tests/ramfs/ramfs_service_test_fixture.c',)),
 ('kernel/ramfs_service_test_fixture.h', ('kernel/tests/ramfs/ramfs_service_test_fixture.h',)),
 ('kernel/ramfs_service_test_internal.h', ('kernel/tests/ramfs/ramfs_service_test_internal.h',)),
 ('kernel/ramfs_service_test_report.c', ('kernel/tests/ramfs/ramfs_service_test_report.c',)),
 ('kernel/scheduler_completion.c', ('kernel/scheduler/scheduler_completion.c',)),
 ('kernel/scheduler_context.c', ('kernel/scheduler/scheduler_context.c',)),
 ('kernel/scheduler_core_accounting.c', ('kernel/scheduler/scheduler_core_accounting.c',)),
 ('kernel/scheduler_core_internal.h', ('kernel/scheduler/scheduler_core_internal.h',)),
 ('kernel/scheduler_core_ipc.c', ('kernel/scheduler/scheduler_core_ipc.c',)),
 ('kernel/scheduler_core_ipc_transition.c', ('kernel/scheduler/scheduler_core_ipc_transition.c',)),
 ('kernel/scheduler_core_policy.c', ('kernel/scheduler/scheduler_core_policy.c',)),
 ('kernel/scheduler_core_private.h', ('kernel/scheduler/scheduler_core_private.h',)),
 ('kernel/scheduler_core_queue.c', ('kernel/scheduler/scheduler_core_queue.c',)),
 ('kernel/scheduler_core_return.c', ('kernel/scheduler/scheduler_core_return.c',)),
 ('kernel/scheduler_core_return_plan.c', ('kernel/scheduler/scheduler_core_return_plan.c',)),
 ('kernel/scheduler_core_validate.c', ('kernel/scheduler/scheduler_core_validate.c',)),
 ('kernel/scheduler_idle.c', ('kernel/scheduler/scheduler_idle.c',)),
 ('kernel/scheduler_interrupt.c', ('kernel/scheduler/scheduler_interrupt.c',)),
 ('kernel/scheduler_invalid_test_fixture.c',
  ('kernel/tests/scheduler/scheduler_invalid_test_fixture.c',)),
 ('kernel/scheduler_invalid_test_internal.h',
  ('kernel/tests/scheduler/scheduler_invalid_test_internal.h',)),
 ('kernel/scheduler_invalid_test_observation.c',
  ('kernel/tests/scheduler/scheduler_invalid_test_observation.c',)),
 ('kernel/scheduler_invalid_test_state.c',
  ('kernel/tests/scheduler/scheduler_invalid_test_state.c',)),
 ('kernel/scheduler_return.c', ('kernel/scheduler/scheduler_return.c',)),
 ('kernel/scheduler_runtime_internal.h', ('kernel/scheduler/scheduler_runtime_internal.h',)),
 ('kernel/scheduler_test_completion.c', ('kernel/tests/scheduler/scheduler_test_completion.c',)),
 ('kernel/scheduler_test_finish.c', ('kernel/tests/scheduler/scheduler_test_finish.c',)),
 ('kernel/scheduler_test_fixture.c', ('kernel/tests/scheduler/scheduler_test_fixture.c',)),
 ('kernel/scheduler_test_internal.h', ('kernel/tests/scheduler/scheduler_test_internal.h',)),
 ('kernel/scheduler_test_interrupt.c', ('kernel/tests/scheduler/scheduler_test_interrupt.c',)),
 ('kernel/scheduler_test_state.c', ('kernel/tests/scheduler/scheduler_test_state.c',)),
 ('kernel/scheduler_test_support.c', ('kernel/tests/scheduler/scheduler_test_support.c',)),
 ('kernel/scheduler_test_trap.c', ('kernel/tests/scheduler/scheduler_test_trap.c',)),
 ('kernel/sv39.c', ('kernel/memory/sv39.c',)),
 ('kernel/syscall.h', ('kernel/trap/syscall.h',)),
 ('kernel/syscall_context.c', ('kernel/trap/syscall_context.c',)),
 ('kernel/syscall_decode.c', ('kernel/trap/syscall_decode.c',)),
 ('kernel/syscall_internal.h', ('kernel/trap/syscall_internal.h',)),
 ('kernel/timer.c', ('kernel/platform/timer.c',)),
 ('kernel/trap_context.c', ('kernel/trap/trap_context.c',)),
 ('kernel/trap_fault.c', ('kernel/trap/trap_fault.c',)),
 ('kernel/trap_internal.h', ('kernel/trap/trap_internal.h',)),
 ('kernel/trap_interrupt.c', ('kernel/trap/trap_interrupt.c',)),
 ('kernel/trap_route_core.c', ('kernel/trap/trap_route_core.c',)),
 ('kernel/trap_route_core.h', ('kernel/trap/trap_route_core.h',)),
 ('kernel/trap_syscall.c', ('kernel/trap/trap_syscall.c',)),
 ('kernel/trap_test.c', ('kernel/tests/trap/trap_test.c',)),
 ('kernel/trap_test_hooks.c', ('kernel/trap/trap_test_hooks.c',)),
 ('kernel/trap_user.c', ('kernel/trap/trap_user.c',)),
 ('kernel/tty_control_core.c', ('kernel/tty/tty_control_core.c',)),
 ('kernel/tty_control_core.h', ('kernel/tty/tty_control_core.h',)),
 ('kernel/tty_control_syscall.c', ('kernel/tty/tty_control_syscall.c',)),
 ('kernel/tty_control_syscall.h', ('kernel/tty/tty_control_syscall.h',)),
 ('kernel/tty_control_syscall_core.c', ('kernel/tty/tty_control_syscall_core.c',)),
 ('kernel/tty_control_syscall_core.h', ('kernel/tty/tty_control_syscall_core.h',)),
 ('kernel/tty_fault.c', ('kernel/tty/tty_fault.c',)),
 ('kernel/tty_fault.h', ('kernel/tty/tty_fault.h',)),
 ('kernel/tty_handoff_core.c', ('kernel/tty/tty_handoff_core.c',)),
 ('kernel/tty_handoff_core.h', ('kernel/tty/tty_handoff_core.h',)),
 ('kernel/tty_handoff_runtime.h', ('kernel/tty/tty_handoff_runtime.h',)),
 ('kernel/tty_handoff_runtime_internal.h', ('kernel/tty/tty_handoff_runtime_internal.h',)),
 ('kernel/tty_handoff_runtime_state.c', ('kernel/tty/tty_handoff_runtime_state.c',)),
 ('kernel/tty_handoff_runtime_transitions.c', ('kernel/tty/tty_handoff_runtime_transitions.c',)),
 ('kernel/tty_handoff_runtime_validate.c', ('kernel/tty/tty_handoff_runtime_validate.c',)),
 ('kernel/tty_interrupt.c', ('kernel/tty/tty_interrupt.c',)),
 ('kernel/tty_interrupt.h', ('kernel/tty/tty_interrupt.h',)),
 ('kernel/tty_service_test.c', ('kernel/tests/tty/tty_service_test.c',)),
 ('kernel/tty_service_test.h', ('kernel/tests/tty/tty_service_test.h',)),
 ('kernel/tty_service_test_fixture.h', ('kernel/tests/tty/tty_service_test_fixture.h',)),
 ('kernel/uart_console_core.c', ('kernel/tty/uart_console_core.c',)),
 ('kernel/uart_console_core.h', ('kernel/tty/uart_console_core.h',)),
 ('kernel/uart_console_test.c', ('kernel/tests/tty/uart_console_test.c',)),
 ('kernel/uart_console_test.h', ('kernel/tests/tty/uart_console_test.h',)),
 ('kernel/user_address_space_activation.c', ('kernel/memory/user_address_space_activation.c',)),
 ('kernel/user_address_space_handoff.c', ('kernel/memory/user_address_space_handoff.c',)),
 ('kernel/user_address_space_internal.h', ('kernel/memory/user_address_space_internal.h',)),
 ('kernel/user_address_space_mapping.c', ('kernel/memory/user_address_space_mapping.c',)),
 ('kernel/user_address_space_mutation.c', ('kernel/memory/user_address_space_mutation.c',)),
 ('kernel/user_address_space_runtime.c', ('kernel/memory/user_address_space_runtime.c',)),
 ('kernel/user_address_space_table.c', ('kernel/memory/user_address_space_table.c',)),
 ('kernel/user_address_space_test_fixture.c',
  ('kernel/tests/memory/user_address_space_test_fixture.c',)),
 ('kernel/user_address_space_test_internal.h',
  ('kernel/tests/memory/user_address_space_test_internal.h',)),
 ('kernel/user_address_space_test_lifecycle.c',
  ('kernel/tests/memory/user_address_space_test_lifecycle.c',)),
 ('kernel/user_address_space_test_mapping.c',
  ('kernel/tests/memory/user_address_space_test_mapping.c',)),
 ('kernel/user_address_space_test_validation.c',
  ('kernel/tests/memory/user_address_space_test_validation.c',)),
 ('kernel/user_address_space_tty.c', ('kernel/memory/user_address_space_tty.c',)),
 ('kernel/user_address_space_validation.c', ('kernel/memory/user_address_space_validation.c',)),
 ('kernel/user_address_space_vm_freeze.c', ('kernel/memory/user_address_space_vm_freeze.c',)),
 ('kernel/user_address_space_vm_plan.c', ('kernel/memory/user_address_space_vm_plan.c',)),
 ('kernel/user_address_space_vm_scratch.c', ('kernel/memory/user_address_space_vm_scratch.c',)),
 ('kernel/user_address_space_vm_teardown.c', ('kernel/memory/user_address_space_vm_teardown.c',)),
 ('kernel/user_execution_internal.h', ('kernel/execution/user_execution_internal.h',)),
 ('kernel/user_execution_test_fixture.c',
  ('kernel/tests/execution/user_execution_test_fixture.c',)),
 ('kernel/user_execution_test_internal.h',
  ('kernel/tests/execution/user_execution_test_internal.h',)),
 ('kernel/user_execution_test_lifecycle.c',
  ('kernel/tests/execution/user_execution_test_lifecycle.c',)),
 ('kernel/user_execution_test_prepare.c',
  ('kernel/tests/execution/user_execution_test_prepare.c',)),
 ('kernel/user_execution_test_trap.c', ('kernel/tests/execution/user_execution_test_trap.c',)),
 ('kernel/user_execution_trap.c', ('kernel/execution/user_execution_trap.c',)),
 ('kernel/user_execution_validation.c', ('kernel/execution/user_execution_validation.c',)),
 ('kernel/user_runtime_image.h', ('kernel/tests/runtime/user_runtime_image.h',)),
 ('kernel/user_runtime_test.h', ('kernel/tests/runtime/user_runtime_test.h',)),
 ('kernel/user_runtime_test_cleanup.c', ('kernel/tests/runtime/user_runtime_test_cleanup.c',)),
 ('kernel/user_runtime_test_image.c', ('kernel/tests/runtime/user_runtime_test_image.c',)),
 ('kernel/user_runtime_test_internal.h', ('kernel/tests/runtime/user_runtime_test_internal.h',)),
 ('kernel/user_runtime_test_report.c', ('kernel/tests/runtime/user_runtime_test_report.c',)),
 ('kernel/user_runtime_test_setup.c', ('kernel/tests/runtime/user_runtime_test_setup.c',)),
 ('kernel/user_runtime_test_state.c', ('kernel/tests/runtime/user_runtime_test_state.c',)),
 ('kernel/user_runtime_test_trap.c', ('kernel/tests/runtime/user_runtime_test_trap.c',)),
 ('kernel/vfs_service_test.c', ('kernel/tests/vfs/vfs_service_test.c',)),
 ('kernel/vfs_service_test.h', ('kernel/tests/vfs/vfs_service_test.h',)),
 ('kernel/vfs_service_test_fixture.h', ('kernel/tests/vfs/vfs_service_test_fixture.h',)),
 ('kernel/vm_bootstrap.c', ('kernel/vm/vm_bootstrap.c',)),
 ('kernel/vm_handoff_abi.c', ('kernel/vm/vm_handoff_abi.c',)),
 ('kernel/vm_handoff_boot.c', ('kernel/vm/vm_handoff_boot.c',)),
 ('kernel/vm_handoff_commit.c', ('kernel/vm/vm_handoff_commit.c',)),
 ('kernel/vm_handoff_core.h', ('kernel/vm/vm_handoff_core.h',)),
 ('kernel/vm_handoff_freeze.c', ('kernel/vm/vm_handoff_freeze.c',)),
 ('kernel/vm_handoff_internal.h', ('kernel/vm/vm_handoff_internal.h',)),
 ('kernel/vm_handoff_load.c', ('kernel/vm/vm_handoff_load.c',)),
 ('kernel/vm_handoff_release.c', ('kernel/vm/vm_handoff_release.c',)),
 ('kernel/vm_handoff_runtime.c', ('kernel/vm/vm_handoff_runtime.c',)),
 ('kernel/vm_handoff_runtime.h', ('kernel/vm/vm_handoff_runtime.h',)),
 ('kernel/vm_handoff_scratch.c', ('kernel/vm/vm_handoff_scratch.c',)),
 ('kernel/vm_handoff_syscall.h', ('kernel/vm/vm_handoff_syscall.h',)),
 ('kernel/vm_handoff_syscall_boot.c', ('kernel/vm/vm_handoff_syscall_boot.c',)),
 ('kernel/vm_handoff_syscall_freeze.c', ('kernel/vm/vm_handoff_syscall_freeze.c',)),
 ('kernel/vm_handoff_syscall_internal.h', ('kernel/vm/vm_handoff_syscall_internal.h',)),
 ('kernel/vm_handoff_syscall_load.c', ('kernel/vm/vm_handoff_syscall_load.c',)),
 ('kernel/vm_handoff_syscall_memory.c', ('kernel/vm/vm_handoff_syscall_memory.c',)),
 ('kernel/vm_handoff_syscall_release.c', ('kernel/vm/vm_handoff_syscall_release.c',)),
 ('kernel/vm_handoff_syscall_runtime.c', ('kernel/vm/vm_handoff_syscall_runtime.c',)),
 ('kernel/vm_handoff_syscall_scratch.c', ('kernel/vm/vm_handoff_syscall_scratch.c',)),
 ('kernel/vm_handoff_test.c', ('kernel/tests/vm/vm_handoff_test.c',)),
 ('kernel/vm_handoff_test.h', ('kernel/tests/vm/vm_handoff_test.h',)),
 ('kernel/vm_handoff_test_fixture.h', ('kernel/tests/vm/vm_handoff_test_fixture.h',)),
 ('kernel/vm_scratch_preemption_test.h', ('kernel/tests/vm/vm_scratch_preemption_test.h',)),
 ('kernel/vm_scratch_preemption_test_fixture.c',
  ('kernel/tests/vm/vm_scratch_preemption_test_fixture.c',)),
 ('kernel/vm_scratch_preemption_test_fixture.h',
  ('kernel/tests/vm/vm_scratch_preemption_test_fixture.h',)),
 ('kernel/vm_scratch_preemption_test_internal.h',
  ('kernel/tests/vm/vm_scratch_preemption_test_internal.h',)),
 ('kernel/vm_scratch_preemption_test_report.c',
  ('kernel/tests/vm/vm_scratch_preemption_test_report.c',)),
 ('kernel/vm_scratch_preemption_test_runtime.c',
  ('kernel/tests/vm/vm_scratch_preemption_test_runtime.c',)),
 ('kernel/vm_scratch_preemption_test_trap.c',
  ('kernel/tests/vm/vm_scratch_preemption_test_trap.c',)),
 ('kernel/vm_snapshot.h', ('kernel/vm/vm_snapshot.h',)),
 ('kernel/vm_snapshot_internal.h', ('kernel/vm/vm_snapshot_internal.h',)),
 ('kernel/vm_snapshot_validation.c', ('kernel/vm/vm_snapshot_validation.c',)))

HOST_UNIT_ONLY_INPUTS = ('tests/host/test_qemu_runner_source_layout.py',
 'tests/host/test_tty_source_layout.py',
 'tests/host/elf_spawn_client_test.c',
 'tests/host/elf_spawn_resource_test.c',
 'tests/host/elf_spawn_fixture_test_internal.h',
 'tests/host/test_generate_elf_spawn_fixture.py',
 'tests/host/elf_spawn_fixture_test.c',
 'tests/host/test_generate_elf_spawn_seed.py',
 'tests/host/application_runtime_test.c',
 'tests/host/application_runtime_test_fixture.c',
 'tests/host/application_runtime_frontend_test.c',
 'tests/host/application_runtime_test_internal.h',
 'tests/host/application_elf_test_fixture.py',
 'tests/host/test_check_application_elf.py',
 'tests/host/test_generate_application_runtime_fixture.py',
 'tests/host/test_generate_application_seed.py',
 'tests/host/test_user_elf_source_layout.py',
 'tests/host/test_scheduler_source_layout.py',
 'tests/host/test_trap_syscall_source_layout.py',
 'tests/host/test_ramfs_source_layout.py',
 'tests/host/test_endpoint_source_layout.py',
 'tests/host/test_kernel_object_source_layout.py',
 'tests/host/test_grant_data_plane_source_layout.py',
 'tests/host/c_source_records.py',
 'tests/host/test_kernel_bootstrap_source_layout.py',
 'tests/host/test_elf_spawn_model_layout.py',
 'tests/host/test_kernel_ipc_source_layout.py',
 'tests/host/test_kernel_address_space_source_layout.py',
 'tests/host/test_frame_ownership_source_layout.py',
 'tests/host/test_kernel_vm_handoff_source_layout.py',
 'tests/host/test_kernel_pm_control_source_layout.py',
 'tests/host/test_vm_service_source_layout.py',
 'tests/host/test_pm_service_source_layout.py',
 'tests/host/kernel_layout_source.py',
 'tests/host/test_kernel_directory_layout.py')


def with_path_classification_equivalents(inputs):
    expanded = list(inputs)
    selected = set(inputs)

    for baseline, equivalents in PATH_CLASSIFICATION_EQUIVALENTS:
        if baseline not in selected:
            continue
        for equivalent in equivalents:
            if equivalent not in expanded:
                expanded.append(equivalent)

    return tuple(expanded)


BOOTSTRAP_CROSS_GATE_INPUTS = (
    "kernel/bootstrap_control.c",
    "kernel/bootstrap_control_core.c",
    "kernel/bootstrap_control_internal.h",
    "kernel/bootstrap_image.c",
    "kernel/bootstrap_manifest.c",
    "kernel/bootstrap_runtime.c",
    "kernel/bootstrap_syscall.c",
    "kernel/vm_bootstrap.c",
    "kernel/vm_handoff_core.c",
    "kernel/vm_handoff_runtime.c",
    "kernel/vm_handoff_syscall.c",
    "kernel/vm_snapshot.c",
)

BOOTSTRAP_CROSS_GATE_WORKFLOWS = (
    "test-qemu-object-model",
    "test-qemu-endpoint",
    "test-qemu-grant",
    "test-qemu-grant-syscall",
    "test-qemu-user-runtime",
    "test-qemu-bootstrap-launcher",
    "test-qemu-vm-handoff",
    "test-qemu-pm-service",
    "test-qemu-vm-scratch-preemption",
    "test-qemu-tty",
    "test-qemu-ramfs",
    "test-qemu-vfs",
    "test-qemu-elf-spawn",
    "test-qemu-vm-ready-early",
    "test-qemu-vm-self-fault",
    "test-qemu-vm-self-fault-sealed",
    "test-qemu-bootstrap-ready-timeout",
    "test-qemu-bootstrap-manifest-panic",
    "test-qemu-ipc",
    "test-qemu-ipc-ecall-core",
    "test-qemu-ipc-syscall",
    "test-qemu-ipc-syscall-panic",
    "test-qemu-frame-ownership",
    "test-qemu-user-address-space",
    "test-qemu-address-space-handoff",
    "test-qemu-user-execution",
    "test-qemu-scheduler",
    "test-qemu-uart-console",
)

SHARED_QEMU_PATHS = (
    "CMakeLists.txt",
    "CMakePresets.json",
    "cmake/",
    "arch/riscv64/kernel.ld",
    "kernel/main.c",
    "kernel/kernel_object_runtime.c",
    "include/micros/",
    "kernel/bootstrap_memory.c",
    "kernel/frame_ownership.c",
    "kernel/trap.c",
    "arch/riscv64/trap.S",
    "arch/riscv64/trap_context.h",
    "kernel/scheduler.c",
    "kernel/scheduler_core.c",
    "include/micros/scheduler_core",
    "kernel/timer.c",
    "kernel/user_execution.c",
    "arch/riscv64/boot.S",
    "tools/check_elf_sections.py",
    "tools/run_qemu_smoke.py",
    "tests/host/test_run_qemu_smoke.py",
)

WIRED_HANDOFF_INPUTS = (
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
)

GRANT_SYSCALL_INPUTS = (
    "arch/riscv64/grant_syscall_test.S",
    "kernel/grant_syscall_test.h",
    "kernel/grant_syscall_test.c",
    "kernel/syscall.c",
    "kernel/ipc_abi.c",
    "kernel/grant_abi.c",
    "kernel/grant_syscall_core.c",
    "kernel/grant_syscall.c",
    "kernel/ipc_syscall.c",
    "kernel/grant.c",
    "kernel/grant_copy.c",
    "kernel/grant_copy_core.c",
    "kernel/grant_runtime.c",
    "kernel/ipc_runtime.c",
    "kernel/endpoint.c",
    "kernel/ipc.c",
    "kernel/kernel_objects.c",
    "kernel/scheduler.c",
    "kernel/scheduler_core.c",
    "kernel/user_address_space.c",
    "kernel/user_execution.c",
    "tests/host/grant_syscall_core_test.c",
)

UNIFIED_SYSCALL_PRODUCTION_INPUTS = (
    "kernel/syscall.c",
    "kernel/ipc_syscall.c",
    "kernel/grant_abi.c",
    "kernel/grant_syscall_core.c",
    "kernel/grant_syscall.c",
    "kernel/grant.c",
    "kernel/grant_copy.c",
    "kernel/grant_copy_core.c",
    "kernel/grant_runtime.c",
)

USER_RUNTIME_INPUTS = (
    "include/micros/runtime.h",
    "lib/runtime/memory.c",
    "lib/runtime/memory.h",
    "lib/runtime/memory_symbols.c",
    "lib/runtime/raw_syscall.h",
    "lib/runtime/raw_syscall.S",
    "lib/runtime/runtime.c",
    "lib/runtime/start.S",
    "lib/runtime/user.ld",
    "kernel/user_runtime_image.h",
    "kernel/user_runtime_test.h",
    "kernel/user_runtime_test.c",
    "arch/riscv64/user_runtime_test.S",
    "tests/host/runtime_test.c",
    "tests/host/test_check_user_elf.py",
    "tests/host/test_generate_user_runtime_fixture.py",
    "tests/qemu/user_runtime_protocol.h",
    "tests/qemu/user_runtime_probe.S",
    "tests/qemu/user_runtime_service.c",
    "tools/check_user_elf.py",
    "tools/generate_user_runtime_fixture.py",
)

TTY_CONTROL_INPUTS = (
    "kernel/panic.c",
    "kernel/plic.c",
    "kernel/plic.h",
    "kernel/plic_core.c",
    "kernel/plic_core.h",
    "kernel/syscall.c",
    "kernel/trap_route_core.c",
    "kernel/trap_route_core.h",
    "kernel/tty_control_core.c",
    "kernel/tty_control_core.h",
    "kernel/tty_control_syscall.c",
    "kernel/tty_control_syscall.h",
    "kernel/tty_control_syscall_core.c",
    "kernel/tty_control_syscall_core.h",
    "kernel/tty_fault.c",
    "kernel/tty_fault.h",
    "kernel/tty_handoff_core.c",
    "kernel/tty_handoff_core.h",
    "kernel/tty_handoff_runtime.c",
    "kernel/tty_handoff_runtime.h",
    "kernel/tty_interrupt.c",
    "kernel/tty_interrupt.h",
    "tests/host/plic_test.c",
    "tests/host/stubs/arch/riscv64/interrupt.h",
    "tests/host/tty_control_test.c",
    "tests/host/tty_control_syscall_test.c",
    "tests/host/tty_fault_test.c",
    "tests/host/tty_handoff_test.c",
    "tests/host/tty_interrupt_test.c",
    "tests/host/tty_test.c",
    "tests/host/trap_route_test.c",
)

TTY_TRAP_INPUTS = (
    "kernel/trap_route_core.c",
    "kernel/trap_route_core.h",
    "kernel/tty_interrupt.c",
    "kernel/tty_interrupt.h",
    "tests/host/trap_route_test.c",
)

RAMFS_STARTUP_CROSS_GATE_INPUTS = (
    "servers/ramfs/ramfs_embedded_seed.h",
    "servers/ramfs/ramfs_service.c",
    "servers/ramfs/seed.json",
    "servers/ramfs/seed/",
    "tools/embed_ramfs_seed.py",
    "tools/generate_ramfs_seed.py",
    "tools/generate_ramfs_service_fixture.py",
)

RAMFS_RETAINED_GATE_WORKFLOWS = (
    "test-qemu-user-runtime",
    "test-qemu-bootstrap-launcher",
    "test-qemu-vm-handoff",
    "test-qemu-pm-service",
    "test-qemu-tty",
)

VFS_STRUCTURE_TEST_INPUTS = (
    "tests/host/test_vfs_test_source_layout.py",
)

VFS_SHARED_FIXTURE_INPUTS = (
    "tests/host/vfs_test_fixture.c",
    "tests/host/vfs_test_fixture.h",
    "tests/host/vfs_test_fixture_grant.c",
    "tests/host/vfs_test_fixture_internal.h",
    "tests/host/vfs_test_fixture_ramfs.c",
    "tests/host/vfs_test_fixture_tty.c",
)

VFS_CORE_TEST_INPUTS = (
    "tests/host/vfs_test.c",
    "tests/host/vfs_test_suites.h",
    "tests/host/vfs_core_test_fixture.c",
    "tests/host/vfs_core_test_fixture.h",
    "tests/host/vfs_core_state_test.c",
    "tests/host/vfs_core_process_test.c",
    "tests/host/vfs_core_namespace_test.c",
    "tests/host/vfs_core_io_test.c",
    "tests/host/vfs_core_directory_test.c",
    "tests/host/vfs_core_console_test.c",
)

VFS_SPAWN_TEST_INPUTS = (
    "tests/host/vfs_spawn_test.c",
    "tests/host/vfs_spawn_test_fixture.c",
    "tests/host/vfs_spawn_test_fixture.h",
    "tests/host/vfs_spawn_image_test.c",
    "tests/host/vfs_spawn_descriptor_test.c",
    "tests/host/vfs_spawn_publication_test.c",
)

VFS_SERVICE_TEST_INPUTS = (
    "tests/host/vfs_service_test.c",
    "tests/host/vfs_service_test_fixture.c",
    "tests/host/vfs_service_test_fixture.h",
    "tests/host/vfs_service_test_suites.h",
    "tests/host/vfs_service_application_protocol_test.c",
    "tests/host/vfs_service_backend_protocol_test.c",
    "tests/host/vfs_service_pm_control_protocol_test.c",
    "tests/host/vfs_service_configuration_test.c",
    "tests/host/vfs_service_dispatch_test.c",
)

VFS_MODEL_TEST_INPUTS = (
    "tests/host/vfs_model_test.c",
    "tests/host/vfs_model_internal.h",
    "tests/host/vfs_model_context.c",
    "tests/host/vfs_model_reference.c",
    "tests/host/vfs_model_compare.c",
    "tests/host/vfs_model_process_operations.c",
    "tests/host/vfs_model_file_operations.c",
    "tests/host/vfs_model_console_operations.c",
    "tests/host/vfs_model_pressure.c",
    "tests/host/vfs_model_trace.c",
)

VFS_PROBE_INPUTS = (
    "tests/qemu/vfs_service_probe.c",
    "tests/qemu/vfs_service_probe_internal.h",
    "tests/qemu/vfs_service_probe_client.c",
    "tests/qemu/vfs_service_probe_filesystem.c",
    "tests/qemu/vfs_service_probe_console.c",
)

VFS_QEMU_TEST_INPUTS = (
    VFS_STRUCTURE_TEST_INPUTS
    + VFS_SHARED_FIXTURE_INPUTS
    + VFS_CORE_TEST_INPUTS
    + VFS_SPAWN_TEST_INPUTS
    + VFS_SERVICE_TEST_INPUTS
    + VFS_MODEL_TEST_INPUTS
    + VFS_PROBE_INPUTS
)

GATE_INPUTS = {
    "test-qemu-smoke": (
        "kernel/fdt.c",
        "arch/riscv64/uart.c",
        "arch/riscv64/sbi.c",
    ),
    "test-qemu-panic": ("kernel/panic.c", "arch/riscv64/panic.S"),
    "test-qemu-trap": ("arch/riscv64/trap_test.S",),
    "test-qemu-timer": ("arch/riscv64/sbi.c",),
    "test-qemu-uart-console": (
        "arch/riscv64/platform.h",
        "arch/riscv64/uart.c",
        "kernel/uart_console_core.c",
        "kernel/uart_console_core.h",
        "kernel/uart_console_test.c",
        "kernel/uart_console_test.h",
    ) + TTY_CONTROL_INPUTS,
    "test-qemu-frame-allocator": (
        "kernel/frame_allocator.c",
        "kernel/frame_allocator_test.c",
    ),
    "test-qemu-trap-panic": (
        "kernel/panic.c",
        "arch/riscv64/trap_test.S",
    ),
    "test-qemu-mmu": (
        "kernel/sv39.c",
        "kernel/mmu_test.c",
        "arch/riscv64/mmu.S",
        "arch/riscv64/mmu_test.S",
    ),
    "test-qemu-object-model": (
        "kernel/object_model_test.c",
        "kernel/kernel_objects.c",
    ),
    "test-qemu-endpoint": (
        "kernel/endpoint.c",
        "kernel/grant.c",
        "kernel/grant_runtime.c",
        "kernel/ipc.c",
        "kernel/kernel_objects.c",
        "kernel/endpoint_test.c",
    ),
    "test-qemu-grant": (
        "kernel/grant.c",
        "kernel/grant_copy.c",
        "kernel/grant_copy_core.c",
        "kernel/grant_runtime.c",
        "kernel/grant_test.c",
        "kernel/user_address_space.c",
        "kernel/user_address_space_core.c",
        "kernel/address_space.c",
        "kernel/sv39.c",
        "kernel/frame_ownership.c",
        "arch/riscv64/mmu.S",
    ) + WIRED_HANDOFF_INPUTS,
    "test-qemu-grant-syscall": (
        GRANT_SYSCALL_INPUTS + WIRED_HANDOFF_INPUTS
    ),
    "test-qemu-user-runtime": (
        USER_RUNTIME_INPUTS
        + UNIFIED_SYSCALL_PRODUCTION_INPUTS
        + WIRED_HANDOFF_INPUTS
        + (
            "kernel/ipc_abi.c",
            "kernel/ipc.c",
            "kernel/ipc_runtime.c",
            "kernel/ipc_buffer.c",
            "kernel/endpoint.c",
            "kernel/kernel_objects.c",
            "kernel/scheduler.c",
            "kernel/scheduler_core.c",
            "kernel/user_address_space.c",
            "kernel/user_execution.c",
        )
    ),
    "test-qemu-bootstrap-launcher": (
        "arch/riscv64/sbi.c",
        "arch/riscv64/mmu.S",
        "arch/riscv64/trap.S",
        "arch/riscv64/uart.c",
        "arch/riscv64/user_entry.S",
        "include/micros/runtime.h",
        "kernel/address_space.c",
        "kernel/bootstrap_memory.c",
        "kernel/bootstrap_control.c",
        "kernel/bootstrap_control_core.c",
        "kernel/bootstrap_image.c",
        "kernel/bootstrap_manifest.c",
        "kernel/bootstrap_runtime.c",
        "kernel/bootstrap_syscall.c",
        "kernel/bootstrap_test.c",
        "kernel/bootstrap_test.h",
        "kernel/bootstrap_test_fixture.h",
        "kernel/endpoint.c",
        "kernel/fdt.c",
        "kernel/frame_allocator.c",
        "kernel/frame_ownership.c",
        "kernel/grant.c",
        "kernel/grant_runtime.c",
        "kernel/ipc_abi.c",
        "kernel/ipc.c",
        "kernel/ipc_buffer.c",
        "kernel/ipc_runtime.c",
        "kernel/ipc_syscall.c",
        "kernel/kernel_object_runtime.c",
        "kernel/kernel_objects.c",
        "kernel/main.c",
        "kernel/scheduler.c",
        "kernel/scheduler_core.c",
        "kernel/syscall.c",
        "kernel/sv39.c",
        "kernel/timer.c",
        "kernel/trap.c",
        "kernel/user_address_space.c",
        "kernel/user_address_space_core.c",
        "kernel/user_execution.c",
        "lib/runtime/memory.c",
        "lib/runtime/memory.h",
        "lib/runtime/memory_symbols.c",
        "lib/runtime/raw_syscall.h",
        "lib/runtime/raw_syscall.S",
        "lib/runtime/runtime.c",
        "lib/runtime/start.S",
        "lib/runtime/user.ld",
        "tests/host/test_generate_bootstrap_fixture.py",
        "tests/qemu/bootstrap_launcher.c",
        "tests/qemu/bootstrap_launcher_control.c",
        "tests/qemu/bootstrap_launcher_control.h",
        "tests/qemu/bootstrap_probe.c",
        "tests/qemu/bootstrap_probe.S",
        "tests/qemu/bootstrap_protocol.h",
        "tools/check_user_elf.py",
        "tools/generate_bootstrap_fixture.py",
    ),
    "test-qemu-ipc": (
        "kernel/ipc.c",
        "kernel/endpoint.c",
        "kernel/kernel_objects.c",
        "kernel/ipc_test.c",
    ),
    "test-qemu-ipc-ecall-core": (
        "kernel/ipc_abi.c",
        "kernel/ipc_ecall_test.c",
        "arch/riscv64/ipc_ecall_test.S",
    ) + UNIFIED_SYSCALL_PRODUCTION_INPUTS,
    "test-qemu-ipc-syscall": (
        "kernel/ipc_abi.c",
        "kernel/ipc.c",
        "kernel/endpoint.c",
        "kernel/ipc_runtime.c",
        "kernel/ipc_buffer.c",
        "kernel/kernel_objects.c",
        "kernel/address_space.c",
        "kernel/sv39.c",
        "kernel/scheduler.c",
        "kernel/scheduler_core.c",
        "kernel/user_address_space.c",
        "kernel/user_execution.c",
        "kernel/trap.c",
        "kernel/ipc_syscall_test.c",
        "arch/riscv64/mmu.S",
        "arch/riscv64/ipc_syscall_test.S",
    ) + UNIFIED_SYSCALL_PRODUCTION_INPUTS + WIRED_HANDOFF_INPUTS,
    "test-qemu-ipc-syscall-panic": (
        "kernel/ipc_abi.c",
        "kernel/ipc.c",
        "kernel/endpoint.c",
        "kernel/ipc_runtime.c",
        "kernel/ipc_buffer.c",
        "kernel/kernel_objects.c",
        "kernel/address_space.c",
        "kernel/sv39.c",
        "kernel/scheduler.c",
        "kernel/scheduler_core.c",
        "kernel/user_address_space.c",
        "kernel/user_execution.c",
        "kernel/trap.c",
        "kernel/ipc_syscall_panic_test.c",
        "arch/riscv64/mmu.S",
        "arch/riscv64/ipc_syscall_panic_test.S",
    ) + UNIFIED_SYSCALL_PRODUCTION_INPUTS + WIRED_HANDOFF_INPUTS,
    "test-qemu-nested-trap": ("arch/riscv64/nested_trap_test.S",),
    "test-qemu-frame-ownership": (
        "kernel/frame_ownership_test.c",
        "kernel/frame_allocator.c",
        "kernel/kernel_objects.c",
    ) + WIRED_HANDOFF_INPUTS,
    "test-qemu-user-address-space": (
        "kernel/grant.c",
        "kernel/grant_abi.c",
        "kernel/grant_copy.c",
        "kernel/grant_copy_core.c",
        "kernel/grant_runtime.c",
        "kernel/grant_syscall.c",
        "kernel/grant_syscall_core.c",
        "kernel/ipc_abi.c",
        "kernel/ipc_syscall.c",
        "kernel/syscall.c",
        "kernel/user_address_space.c",
        "kernel/address_space.c",
        "kernel/sv39.c",
        "kernel/frame_ownership.c",
        "kernel/user_address_space_test.c",
        "arch/riscv64/mmu.S",
        "arch/riscv64/user_address_space_test.S",
    ) + WIRED_HANDOFF_INPUTS,
    "test-qemu-address-space-handoff": (
        WIRED_HANDOFF_INPUTS + GRANT_SYSCALL_INPUTS
    ),
    "test-qemu-user-execution": (
        "kernel/user_execution_test.c",
        "arch/riscv64/user_execution_test.S",
    ) + WIRED_HANDOFF_INPUTS,
    "test-qemu-scheduler": (
        "kernel/scheduler_test.c",
        "kernel/kernel_objects.c",
        "arch/riscv64/scheduler_test.S",
    ) + WIRED_HANDOFF_INPUTS + TTY_TRAP_INPUTS,
    "test-qemu-scheduler-invalid-outgoing": (
        "kernel/scheduler_invalid_test.c",
        "arch/riscv64/scheduler_test.S",
    ),
    "test-qemu-scheduler-invalid-next": (
        "kernel/scheduler_invalid_test.c",
        "arch/riscv64/scheduler_test.S",
    ),
}

GATE_INPUTS["test-qemu-bootstrap-ready-timeout"] = GATE_INPUTS[
    "test-qemu-bootstrap-launcher"
] + (
    "arch/riscv64/panic.S",
    "kernel/panic.c",
)
GATE_INPUTS["test-qemu-bootstrap-manifest-panic"] = GATE_INPUTS[
    "test-qemu-bootstrap-launcher"
] + (
    "arch/riscv64/panic.S",
    "kernel/panic.c",
)
GATE_INPUTS["test-qemu-vm-handoff"] = GATE_INPUTS[
    "test-qemu-bootstrap-launcher"
] + (
    "include/micros/vm_bootstrap.h",
    "kernel/vm_bootstrap.c",
    "kernel/vm_handoff_core.c",
    "kernel/vm_handoff_core.h",
    "kernel/vm_handoff_runtime.c",
    "kernel/vm_handoff_runtime.h",
    "kernel/vm_handoff_syscall.c",
    "kernel/vm_handoff_syscall.h",
    "kernel/vm_handoff_test.c",
    "kernel/vm_handoff_test.h",
    "kernel/vm_handoff_test_fixture.h",
    "kernel/vm_snapshot.c",
    "kernel/vm_snapshot.h",
    "tests/host/test_generate_vm_handoff_fixture.py",
    "tests/host/vm_handoff_test.c",
    "tests/qemu/vm_handoff_probe.S",
    "tests/qemu/vm_handoff_probe.c",
    "tests/qemu/vm_handoff_protocol.h",
    "tests/qemu/vm_self_fault.S",
    "tests/qemu/vm_server.c",
    "tools/generate_vm_handoff_fixture.py",
)
GATE_INPUTS["test-qemu-pm-service"] = GATE_INPUTS[
    "test-qemu-vm-handoff"
] + (
    "include/micros/pm.h",
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
)
GATE_INPUTS["test-qemu-vm-scratch-preemption"] = GATE_INPUTS[
    "test-qemu-pm-service"
] + (
    "arch/riscv64/mmu.S",
    "kernel/scheduler.c",
    "kernel/timer.c",
    "kernel/trap.c",
    "kernel/user_address_space.c",
    "kernel/vm_scratch_preemption_test.c",
    "kernel/vm_scratch_preemption_test.h",
    "kernel/vm_scratch_preemption_test_fixture.h",
    "tests/host/test_generate_vm_scratch_preemption_fixture.py",
    "tests/qemu/vm_scratch_preemption_peer.S",
    "tests/qemu/vm_scratch_preemption_driver.c",
    "tests/qemu/vm_scratch_preemption_protocol.h",
    "tests/qemu/vm_scratch_preemption_vm.S",
    "tools/generate_vm_scratch_preemption_fixture.py",
)
GATE_INPUTS["test-qemu-tty"] = GATE_INPUTS[
    "test-qemu-pm-service"
] + TTY_CONTROL_INPUTS + (
    "include/micros/tty.h",
    "kernel/tty_service_test.c",
    "kernel/tty_service_test.h",
    "kernel/tty_service_test_fixture.h",
    "servers/tty/",
    "tests/host/test_generate_tty_service_fixture.py",
    "tests/qemu/tty_handoff_protocol.h",
    "tests/qemu/tty_service_claim_wait.S",
    "tests/qemu/tty_service_report.S",
    "tests/qemu/tty_service_vfs.c",
    "tools/generate_tty_service_fixture.py",
)
for ramfs_retained_workflow in RAMFS_RETAINED_GATE_WORKFLOWS:
    GATE_INPUTS[ramfs_retained_workflow] += (
        RAMFS_STARTUP_CROSS_GATE_INPUTS
    )
GATE_INPUTS["test-qemu-ramfs"] = GATE_INPUTS[
    "test-qemu-tty"
] + (
    "include/micros/ramfs.h",
    "kernel/ramfs_service_test.c",
    "kernel/ramfs_service_test.h",
    "kernel/ramfs_service_test_fixture.h",
    "servers/ramfs/",
    "tests/host/ramfs_model_test.c",
    "tests/host/test_generate_ramfs_service_fixture.py",
    "tests/qemu/ramfs_service_protocol.h",
    "tests/qemu/ramfs_service_report.S",
    "tests/qemu/ramfs_service_vfs.c",
    "tools/embed_ramfs_seed.py",
    "tools/generate_ramfs_seed.py",
    "tools/generate_ramfs_service_fixture.py",
)
GATE_INPUTS["test-qemu-vfs"] = GATE_INPUTS[
    "test-qemu-ramfs"
] + VFS_QEMU_TEST_INPUTS + (
    "include/micros/vfs.h",
    "kernel/vfs_service_test.c",
    "kernel/vfs_service_test.h",
    "kernel/vfs_service_test_fixture.h",
    "servers/vfs/",
    "tests/host/test_generate_vfs_service_fixture.py",
    "tests/qemu/vfs_service_protocol.h",
    "tests/qemu/vfs_service_report.S",
    "tools/generate_vfs_service_fixture.py",
)
GATE_INPUTS["test-qemu-vm-ready-early"] = GATE_INPUTS[
    "test-qemu-vm-handoff"
] + (
    "arch/riscv64/panic.S",
    "kernel/panic.c",
)
GATE_INPUTS["test-qemu-vm-self-fault"] = GATE_INPUTS[
    "test-qemu-vm-handoff"
] + (
    "arch/riscv64/panic.S",
    "kernel/panic.c",
)
GATE_INPUTS["test-qemu-vm-self-fault-sealed"] = GATE_INPUTS[
    "test-qemu-vm-self-fault"
]
ELF_SPAWN_INTEGRATION_INPUTS = (
    "kernel/elf_spawn_test.c",
    "kernel/elf_spawn_test.h",
    "kernel/elf_spawn_test_internal.h",
    "kernel/elf_spawn_test_fixture.c",
    "kernel/elf_spawn_test_fixture.h",
    "kernel/elf_spawn_test_runtime.c",
    "kernel/elf_spawn_test_capture.c",
    "kernel/elf_spawn_test_services.c",
    "kernel/elf_spawn_test_service_checks.c",
    "kernel/elf_spawn_test_report.c",
    "kernel/elf_spawn_test_compare.c",
    "kernel/elf_spawn_test_service_compare.c",
    "tests/qemu/elf_spawn_protocol.h",
    "tests/qemu/elf_spawn_resources.h",
    "tests/qemu/elf_spawn_request.c",
    "tests/qemu/elf_spawn_request.h",
    "tests/qemu/elf_spawn_init.c",
    "tests/qemu/elf_spawn_child.c",
    "tests/qemu/elf_spawn_client.c",
    "tests/qemu/elf_spawn_client_vfs.c",
    "tests/qemu/elf_spawn_client.h",
    "tests/qemu/elf_spawn_pm.c",
    "tests/qemu/elf_spawn_report.S",
    "tools/generate_elf_spawn_fixture.py",
    "tools/generate_elf_spawn_seed.py",
    "tools/check_application_elf.py",
    "tools/application_elf_policy.py",
    "lib/runtime/application.c",
    "lib/runtime/application_stack.c",
    "lib/runtime/application_start.S",
    "lib/runtime/application.ld",
    "lib/runtime/application_internal.h",
    "include/micros/application_runtime.h",
)
GATE_INPUTS["test-qemu-elf-spawn"] = (
    GATE_INPUTS["test-qemu-vfs"] + ELF_SPAWN_INTEGRATION_INPUTS
)

for bootstrap_workflow in BOOTSTRAP_CROSS_GATE_WORKFLOWS:
    GATE_INPUTS[bootstrap_workflow] += BOOTSTRAP_CROSS_GATE_INPUTS

APPLICATION_RUNTIME_COMPONENT_INPUTS = (
    "lib/runtime/application.c",
    "lib/runtime/application_stack.c",
    "lib/runtime/application_start.S",
    "lib/runtime/application.ld",
    "lib/runtime/application_internal.h",
    "include/micros/application_runtime.h",
    "tests/qemu/application_runtime_probe.c",
    "tests/qemu/application_runtime_vm.c",
    "tests/qemu/application_runtime_fixture.h",
    "tools/check_application_elf.py",
    "tools/application_elf_policy.py",
    "tools/generate_application_runtime_fixture.py",
    "tools/generate_user_runtime_fixture.py",
)
GATE_INPUTS["test-qemu-pm-service"] += APPLICATION_RUNTIME_COMPONENT_INPUTS

SLOW_MODEL_INPUTS = {
    "test-ipc-model": (
        "CMakeLists.txt",
        "CMakePresets.json",
        "kernel/endpoint.c",
        "kernel/grant.c",
        "kernel/grant_abi.c",
        "kernel/grant_copy.c",
        "kernel/grant_copy_core.c",
        "kernel/grant_runtime.c",
        "kernel/grant_syscall.c",
        "kernel/grant_syscall_core.c",
        "kernel/ipc_abi.c",
        "kernel/ipc_syscall.c",
        "kernel/syscall.c",
        "kernel/user_address_space.c",
        "kernel/address_space.c",
        "kernel/sv39.c",
        "kernel/frame_ownership.c",
        "arch/riscv64/mmu.S",
        "kernel/ipc.c",
        "kernel/kernel_objects.c",
        "kernel/scheduler_core.c",
        "include/micros/",
        "tests/host/endpoint_model_test.c",
        "tests/host/grant_test.c",
        "tests/host/grant_copy_test.c",
        "tests/host/grant_syscall_core_test.c",
        "tests/host/user_address_space_core_test.c",
        "tests/host/ipc_model_test.c",
        "tests/host/ipc_model_main.c",
        "tests/host/ipc_notify_test.c",
        "tests/host/ipc_close_test.c",
    ) + WIRED_HANDOFF_INPUTS,
    "test-tty-model": (
        "CMakeLists.txt",
        "CMakePresets.json",
        "include/micros/grant.h",
        "include/micros/ipc.h",
        "include/micros/tty.h",
        "servers/tty/tty_core.c",
        "servers/tty/tty_core.h",
        "servers/tty/tty_service_core.c",
        "servers/tty/tty_service_core.h",
        "servers/tty/tty_uart.c",
        "servers/tty/tty_uart.h",
        "tests/host/tty_model_test.c",
    ),
    "test-ramfs-model": (
        "CMakeLists.txt",
        "CMakePresets.json",
        "include/micros/grant.h",
        "include/micros/ipc.h",
        "include/micros/ramfs.h",
        "servers/ramfs/",
        "tests/host/ramfs_model_test.c",
        "tools/generate_ramfs_seed.py",
    ),
    "test-vfs-model": (
        "CMakeLists.txt",
        "CMakePresets.json",
        "include/micros/ramfs.h",
        "include/micros/tty.h",
        "include/micros/vfs.h",
        "servers/vfs/",
    ) + VFS_SHARED_FIXTURE_INPUTS + VFS_MODEL_TEST_INPUTS,
    "test-elf-spawn-model": (
        "CMakeLists.txt",
        "CMakePresets.json",
        "include/micros/",
        "lib/spawn/",
        "lib/vm/",
        "servers/pm/",
        "servers/vm/",
        "servers/vfs/",
        "kernel/frame_allocator.c",
        "kernel/frame_ownership.c",
        "kernel/vm_bootstrap.c",
        "tests/host/vm_core_test.c",
        "tests/host/elf_spawn_model_test.c",
        "tests/host/elf_spawn_model_context.c",
        "tests/host/elf_spawn_model_kernel.c",
        "tests/host/elf_spawn_model_vm.c",
        "tests/host/elf_spawn_model_transport.c",
        "tests/host/elf_spawn_model_operations.c",
        "tests/host/elf_spawn_model_reference.c",
        "tests/host/elf_spawn_model_compare.c",
        "tests/host/elf_spawn_model_internal.h",
    ) + VFS_SHARED_FIXTURE_INPUTS,
}

SLOW_MODEL_CTESTS = {
    "test-ipc-model": ("ipc-model", "ipc_model_test"),
    "test-tty-model": ("tty-model", "tty_model_test"),
    "test-ramfs-model": ("ramfs-model", "ramfs_model_test"),
    "test-vfs-model": ("vfs-model", "vfs_model_test"),
    "test-elf-spawn-model": ("elf-spawn-model", "elf_spawn_model_test"),
}

# Model registration must not narrow the VM family's previous full-tier ownership.
SHARED_QEMU_PATHS = with_path_classification_equivalents(
    SHARED_QEMU_PATHS + ("servers/vm/", "tests/host/vm_core_test.c")
)
GATE_INPUTS = {
    name: with_path_classification_equivalents(inputs)
    for name, inputs in GATE_INPUTS.items()
}
SLOW_MODEL_INPUTS = {
    name: with_path_classification_equivalents(inputs)
    for name, inputs in SLOW_MODEL_INPUTS.items()
}
IMAGE_INPUTS = {
    name: with_path_classification_equivalents(inputs)
    for name, inputs in IMAGE_INPUTS.items()
}


def workflow(name):
    return ("cmake", "--workflow", "--preset", name)


def append_unique(commands, command):
    if command not in commands:
        commands.append(command)


def is_document(path):
    return path.endswith(".md") or path.startswith(".github/instructions/")


def normalize_path(path, root=None):
    if root is not None and os.path.isabs(path):
        path = os.path.relpath(path, root)
    while path.startswith("./"):
        path = path[2:]
    return path


def planner_root():
    return os.path.realpath(
        os.path.join(os.path.dirname(__file__), os.pardir)
    )


def normalize_explicit_path(path, root, caller_directory):
    candidate = (
        path
        if os.path.isabs(path)
        else os.path.join(caller_directory, path)
    )
    candidate = os.path.realpath(candidate)
    if os.path.commonpath((root, candidate)) != root:
        raise ValueError(f"path is outside planner repository: {path}")
    return os.path.relpath(candidate, root)


def path_matches(path, patterns):
    return any(
        path.startswith(pattern) if pattern.endswith("/") else path == pattern
        for pattern in patterns
    )


def target_path(path):
    return path.startswith(("kernel/", "arch/", "include/"))


def affected_workflows(paths):
    if any(path_matches(path, SHARED_QEMU_PATHS) for path in paths):
        return list(QEMU_WORKFLOWS)

    workflows = []
    unmatched_target = False
    for path in paths:
        matched = [
            name
            for name in QEMU_WORKFLOWS
            if path_matches(path, GATE_INPUTS.get(name, ()))
        ]
        if target_path(path) and not matched:
            unmatched_target = True
        for name in matched:
            if name not in workflows:
                workflows.append(name)
    if unmatched_target:
        return list(QEMU_WORKFLOWS)
    return workflows


def affected_slow_models(paths):
    return [
        name
        for name, inputs in SLOW_MODEL_INPUTS.items()
        if any(path_matches(path, inputs) for path in paths)
    ]


def affected_image_workflows(paths):
    return [
        name
        for name, inputs in IMAGE_INPUTS.items()
        if any(path_matches(path, inputs) for path in paths)
    ]


def requires_full_tier(paths):
    if any(path_matches(path, SHARED_QEMU_PATHS) for path in paths):
        return True
    for path in paths:
        if is_document(path):
            continue
        known = (
            any(
                path_matches(path, GATE_INPUTS.get(name, ()))
                for name in QEMU_WORKFLOWS
            )
            or bool(affected_slow_models([path]))
            or bool(affected_image_workflows([path]))
            or path_matches(path, PLANNER_INPUTS)
            or path_matches(path, HOST_UNIT_ONLY_INPUTS)
        )
        if not known:
            return True
    return False


def diff_commands(base):
    return (
        ("git", "diff", "--cached", "--check"),
        ("git", "diff", "--check"),
        ("git", "diff", "--check", f"{base}...HEAD"),
    )


def plan(paths, tier, base="origin/main"):
    paths = sorted({normalize_path(path) for path in paths})
    if tier == "full":
        commands = [UNIT_FULL]
        commands.extend(workflow(name) for name in QEMU_WORKFLOWS)
        commands.extend(workflow(name) for name in IMAGE_WORKFLOWS)
        commands.append(DOCS)
        commands.extend(diff_commands(base))
        return commands

    commands = []
    code_paths = [path for path in paths if not is_document(path)]
    if code_paths:
        append_unique(commands, UNIT_FAST)
    if any(is_document(path) for path in paths):
        append_unique(commands, DOCS)
    if not code_paths:
        commands.extend(diff_commands(base))
        return commands

    if requires_full_tier(code_paths):
        return plan(paths, "full", base)

    workflows = affected_workflows(code_paths)
    image_workflows = affected_image_workflows(code_paths)
    if tier == "fast":
        for name in affected_slow_models(code_paths):
            append_unique(commands, workflow(name))
        for name in workflows:
            append_unique(commands, workflow(name))
        for name in image_workflows:
            append_unique(commands, workflow(name))
    else:
        commands[0] = UNIT_FULL
        append_unique(commands, SMOKE)
        for name in workflows:
            append_unique(commands, workflow(name))
        for name in image_workflows:
            append_unique(commands, workflow(name))
    commands.extend(diff_commands(base))
    return commands


def changed_paths(base, root):
    commands = (
        ("git", "diff", "--no-renames", "--name-only", f"{base}...HEAD"),
        ("git", "diff", "--no-renames", "--name-only"),
        ("git", "diff", "--cached", "--no-renames", "--name-only"),
        ("git", "ls-files", "--others", "--exclude-standard"),
    )
    paths = set()
    for command in commands:
        completed = subprocess.run(
            command,
            check=True,
            stdout=subprocess.PIPE,
            text=True,
            cwd=root,
        )
        paths.update(line for line in completed.stdout.splitlines() if line)
    return sorted(paths)


def repository_plan(root, paths, tier, base="origin/main"):
    if tier == "full" or "CMakeLists.txt" not in paths:
        return plan(paths, tier, base)
    owners = validation_build_impact.discover(root, base)
    if owners is None:
        return plan(paths, tier, base)
    remaining = [path for path in paths if path != "CMakeLists.txt"]
    commands = plan(remaining, tier, base)[:-3]
    if not any(command in (UNIT_FAST, UNIT_FULL) for command in commands):
        commands.insert(0, UNIT_FAST if tier == "fast" else UNIT_FULL)
    if tier == "pr":
        append_unique(commands, SMOKE)
    for name in QEMU_WORKFLOWS:
        if name in owners:
            append_unique(commands, workflow(name))
    commands.extend(diff_commands(base))
    return commands


def untracked_paths(root):
    completed = subprocess.run(
        ("git", "ls-files", "--others", "--exclude-standard"),
        check=True,
        stdout=subprocess.PIPE,
        text=True,
        cwd=root,
    )
    return sorted(
        line for line in completed.stdout.splitlines() if line
    )


def combine_paths(discovered, explicit):
    return sorted(set(discovered) | set(explicit))


def ensure_execution_ready(root):
    remaining = untracked_paths(root)
    if remaining:
        raise ValueError(
            "stage untracked files before execution: "
            + ", ".join(remaining)
        )


def preset_workflows(root):
    with open(
        os.path.join(root, "CMakePresets.json"),
        encoding="utf-8",
    ) as source:
        presets = json.load(source)
    return {
        preset["name"]
        for preset in presets["workflowPresets"]
    }


def documented_qemu_workflows(root):
    with open(
        os.path.join(root, "docs/testing-strategy.md"),
        encoding="utf-8",
    ) as source:
        content = source.read()
    return set(
        re.findall(
            r"cmake --workflow --preset (test-qemu-[a-z0-9-]+)",
            content,
        )
    )


def documented_slow_workflows(root):
    with open(
        os.path.join(root, "docs/testing-strategy.md"),
        encoding="utf-8",
    ) as source:
        content = source.read()
    workflows = set(
        re.findall(
            r"cmake --workflow --preset (test-[a-z0-9-]*model)",
            content,
        )
    )
    return {
        name for name in workflows if not name.startswith("test-qemu-")
    }


def documented_image_workflows(root):
    with open(
        os.path.join(root, "docs/development/building.md"),
        encoding="utf-8",
    ) as source:
        content = source.read()
    return set(
        re.findall(
            r"cmake --workflow --preset (build-[a-z0-9-]+-images?)(?![a-z0-9-])",
            content,
        )
    )


def custom_target_block(cmake, target):
    marker = f"add_custom_target(\n        {target}\n"
    start = cmake.find(marker)
    if start < 0:
        return ""
    next_target = cmake.find("add_custom_target(", start + len(marker))
    return cmake[start:] if next_target < 0 else cmake[start:next_target]


def validate_inventory(root):
    workflows = preset_workflows(root)
    qemu_inventory = {
        name for name in workflows if name.startswith("test-qemu-")
    }
    if qemu_inventory != set(QEMU_WORKFLOWS):
        raise ValueError("QEMU workflow inventory is out of sync")
    if qemu_inventory != documented_qemu_workflows(root):
        raise ValueError("documented QEMU matrix is out of sync")
    slow_inventory = {
        name
        for name in workflows
        if name.startswith("test-")
        and name.endswith("-model")
        and not name.startswith("test-qemu-")
    }
    if slow_inventory != set(SLOW_MODEL_INPUTS):
        raise ValueError("slow-model workflow inventory is out of sync")
    if slow_inventory != documented_slow_workflows(root):
        raise ValueError("documented slow-model matrix is out of sync")
    if slow_inventory != set(SLOW_MODEL_CTESTS):
        raise ValueError("slow-model CTest map is out of sync")
    if set(GATE_INPUTS) != qemu_inventory:
        raise ValueError("QEMU ownership map is out of sync")
    image_inventory = {
        name
        for name in workflows
        if name.startswith("build-") and name.endswith(("-image", "-images"))
    }
    if image_inventory != set(IMAGE_WORKFLOWS):
        raise ValueError("image workflow inventory is out of sync")
    if image_inventory != documented_image_workflows(root):
        raise ValueError("documented image matrix is out of sync")
    if set(IMAGE_INPUTS) != image_inventory:
        raise ValueError("image ownership map is out of sync")

    with open(
        os.path.join(root, "CMakeLists.txt"),
        encoding="utf-8",
    ) as source:
        cmake = source.read()
    fast_block = custom_target_block(cmake, "test-unit-fast")
    full_block = custom_target_block(cmake, "test-unit")
    for workflow_name, (ctest_name, executable) in SLOW_MODEL_CTESTS.items():
        required = (
            f"add_test(NAME {ctest_name} COMMAND {executable})",
            f"set_tests_properties({ctest_name} PROPERTIES LABELS slow)",
        )
        if any(fragment not in cmake for fragment in required):
            raise ValueError("slow-model CTest wiring is out of sync")
        model_block = custom_target_block(cmake, workflow_name)
        if (
            executable in fast_block
            or executable not in model_block
            or executable not in full_block
        ):
            raise ValueError("slow-model tier membership is out of sync")


def canonical_workflows():
    return (
        UNIT_FAST[-1], UNIT_FULL[-1],
        *SLOW_MODEL_INPUTS, *QEMU_WORKFLOWS, *IMAGE_WORKFLOWS,
    )


def parse_arguments(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--tier", choices=("fast", "pr", "full"), default="fast")
    parser.add_argument("--base", default="origin/main")
    parser.add_argument("--path", action="append", default=[])
    parser.add_argument("--execute", action="store_true")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--reuse", action="store_true")
    mode.add_argument("--fresh", action="store_true")
    mode.add_argument("--producer", action="store_true")
    parser.add_argument("--record-workflow", choices=canonical_workflows())
    arguments = parser.parse_args(argv)
    if (
        arguments.reuse or arguments.fresh or arguments.producer
    ) and not arguments.execute:
        parser.error("--reuse/--fresh/--producer requires authoritative --execute")
    if arguments.reuse and arguments.tier == "full":
        parser.error("explicit full validation always executes fresh")
    if arguments.record_workflow and (
        arguments.execute or arguments.path or arguments.tier != "fast"
        or arguments.base != "origin/main" or arguments.producer
    ):
        parser.error("gate-only recording cannot select a plan, paths, base or nondefault tier")
    if arguments.base.startswith("-"):
        parser.error("--base must identify a Git revision, not an option")
    return arguments


def main(argv=None):
    arguments = parse_arguments(argv)
    root = planner_root()
    try:
        expected_source = (
            validation_context.capture_candidate(root)
            if arguments.execute or arguments.record_workflow else None
        )
        validate_inventory(root)
        if arguments.record_workflow:
            ensure_execution_ready(root)
            if validation_context.capture_candidate(root) != expected_source:
                raise ValueError("source identity changed while selecting the canonical workflow")
            return validation_execution.execute_plan(
                root, [workflow(arguments.record_workflow)],
                canonical_workflows(), gate_only=True, expected_source=expected_source,
            )
        discovered = changed_paths(arguments.base, root)
        explicit = [
            normalize_explicit_path(path, root, os.getcwd())
            for path in arguments.path
        ]
        paths = combine_paths(discovered, explicit)
    except ValueError as error:
        raise SystemExit(str(error)) from error
    commands = repository_plan(root, paths, arguments.tier, arguments.base)

    if arguments.execute:
        try:
            ensure_execution_ready(root)
            if validation_context.capture_candidate(root) != expected_source:
                raise ValueError("source identity changed while selecting required validation coverage")
            if arguments.producer and not expected_source.clean:
                raise ValueError(
                    "producer requires the exact clean staged candidate"
                )
        except ValueError as error:
            raise SystemExit(str(error)) from error
        return validation_execution.execute_plan(
            root, commands, canonical_workflows(), reuse=arguments.reuse,
            producer=arguments.producer,
            expected_source=expected_source,
        )
    for command in commands:
        print(shlex.join(command), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

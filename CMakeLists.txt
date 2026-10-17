cmake_minimum_required(VERSION 3.25)

project(micros VERSION 0.1.0 LANGUAGES C)

option(MICROS_BUILD_HOST_TESTS "Build native host tests" OFF)
option(MICROS_BUILD_PANIC_TEST "Build the intentional panic image" OFF)
option(MICROS_BUILD_TRAP_TEST "Build the trap recovery test image" OFF)
option(MICROS_BUILD_TIMER_TEST "Build the timer interrupt test image" OFF)
option(
    MICROS_BUILD_UART_CONSOLE_TEST
    "Build the UART console ownership handoff test image"
    OFF
)
option(
    MICROS_BUILD_FRAME_ALLOCATOR_TEST
    "Build the bootstrap frame allocator test image"
    OFF
)
option(
    MICROS_BUILD_TRAP_PANIC_TEST
    "Build the unexpected-trap panic test image"
    OFF
)
option(MICROS_BUILD_MMU_TEST "Build the Sv39 MMU test image" OFF)
option(
    MICROS_BUILD_OBJECT_MODEL_TEST
    "Build the kernel object model test image"
    OFF
)
option(
    MICROS_BUILD_ENDPOINT_TEST
    "Build the endpoint and privilege-profile test image"
    OFF
)
option(
    MICROS_BUILD_GRANT_TEST
    "Build the checked direct-grant copy test image"
    OFF
)
option(
    MICROS_BUILD_IPC_TEST
    "Build the blocking IPC acceptance test image"
    OFF
)
option(
    MICROS_BUILD_IPC_ECALL_CORE_TEST
    "Build the production IPC ecall core test image"
    OFF
)
option(
    MICROS_BUILD_IPC_SYSCALL_TEST
    "Build the complete IPC syscall acceptance test image"
    OFF
)
option(
    MICROS_BUILD_IPC_SYSCALL_PANIC_TEST
    "Build the invalid IPC return-buffer panic image"
    OFF
)
option(
    MICROS_BUILD_GRANT_SYSCALL_TEST
    "Build the unified grant syscall acceptance image"
    OFF
)
option(
    MICROS_BUILD_USER_RUNTIME_TEST
    "Build the freestanding user-service runtime acceptance image"
    OFF
)
option(
    MICROS_BUILD_BOOTSTRAP_LAUNCHER_TEST
    "Build the static bootstrap launcher acceptance image"
    OFF
)
option(
    MICROS_BUILD_BOOTSTRAP_READY_TIMEOUT_TEST
    "Build the bootstrap readiness-timeout panic image"
    OFF
)
option(
    MICROS_BUILD_BOOTSTRAP_MANIFEST_PANIC_TEST
    "Build the malformed bootstrap manifest panic image"
    OFF
)
option(
    MICROS_BUILD_VM_HANDOFF_TEST
    "Build the static VM handoff integration image"
    OFF
)
option(
    MICROS_BUILD_PM_SERVICE_TEST
    "Build the real PM service integration image"
    OFF
)
option(
    MICROS_BUILD_VM_SCRATCH_PREEMPTION_TEST
    "Build the VM scratch preemption and stale-translation image"
    OFF
)
option(
    MICROS_BUILD_TTY_SERVICE_TEST
    "Build the real TTY service integration image"
    OFF
)
option(
    MICROS_BUILD_TTY_SERVICE_IMAGE
    "Build the standalone real TTY service ELF"
    OFF
)
option(
    MICROS_BUILD_RAMFS_SERVICE_IMAGE
    "Build the standalone real RAMFS service ELF"
    OFF
)
option(
    MICROS_BUILD_VFS_SERVICE_IMAGE
    "Build the standalone real VFS service ELF"
    OFF
)
option(
    MICROS_BUILD_RAMFS_SERVICE_TEST
    "Build the real RAMFS service integration image"
    OFF
)
option(
    MICROS_BUILD_VFS_SERVICE_TEST
    "Build the real VFS service integration image"
    OFF
)
option(
    MICROS_BUILD_ELF_SPAWN_TEST
    "Build the production ELF spawn and resource-baseline integration image"
    OFF
)
option(
    MICROS_BUILD_VM_READY_EARLY_TEST
    "Build the early VM readiness role-gate panic image"
    OFF
)
option(
    MICROS_BUILD_VM_SELF_FAULT_TEST
    "Build the running-bootstrap VM self-fault image"
    OFF
)
option(
    MICROS_BUILD_VM_SELF_FAULT_SEALED_TEST
    "Build the sealed-bootstrap VM self-fault image"
    OFF
)
option(
    MICROS_BUILD_NESTED_TRAP_TEST
    "Build the per-hart nested trap test image"
    OFF
)
option(
    MICROS_BUILD_FRAME_OWNERSHIP_TEST
    "Build the typed frame ownership test image"
    OFF
)
option(
    MICROS_BUILD_USER_ADDRESS_SPACE_TEST
    "Build the generation-safe user address-space test image"
    OFF
)
option(
    MICROS_BUILD_ADDRESS_SPACE_HANDOFF_TEST
    "Build the post-handoff wired address-space test image"
    OFF
)
option(
    MICROS_BUILD_USER_EXECUTION_TEST
    "Build the user execution-context and U-mode entry test image"
    OFF
)
option(
    MICROS_BUILD_SCHEDULER_TEST
    "Build the MINIX-baseline scheduler test image"
    OFF
)
option(
    MICROS_BUILD_SCHEDULER_INVALID_OUTGOING_TEST
    "Build the invalid outgoing scheduler context panic image"
    OFF
)
option(
    MICROS_BUILD_SCHEDULER_INVALID_NEXT_TEST
    "Build the invalid selected scheduler context panic image"
    OFF
)
option(MICROS_BUILD_APPLICATION_IMAGES "Build checked standalone application images and seed" OFF)
option(MICROS_ENABLE_SANITIZERS "Enable host-test sanitizers" ON)

set(
    MICROS_TARGET_TEST_OPTIONS
    MICROS_BUILD_PANIC_TEST
    MICROS_BUILD_TRAP_TEST
    MICROS_BUILD_TIMER_TEST
    MICROS_BUILD_UART_CONSOLE_TEST
    MICROS_BUILD_FRAME_ALLOCATOR_TEST
    MICROS_BUILD_TRAP_PANIC_TEST
    MICROS_BUILD_MMU_TEST
    MICROS_BUILD_OBJECT_MODEL_TEST
    MICROS_BUILD_ENDPOINT_TEST
    MICROS_BUILD_GRANT_TEST
    MICROS_BUILD_IPC_TEST
    MICROS_BUILD_IPC_ECALL_CORE_TEST
    MICROS_BUILD_IPC_SYSCALL_TEST
    MICROS_BUILD_IPC_SYSCALL_PANIC_TEST
    MICROS_BUILD_GRANT_SYSCALL_TEST
    MICROS_BUILD_USER_RUNTIME_TEST
    MICROS_BUILD_BOOTSTRAP_LAUNCHER_TEST
    MICROS_BUILD_BOOTSTRAP_READY_TIMEOUT_TEST
    MICROS_BUILD_BOOTSTRAP_MANIFEST_PANIC_TEST
    MICROS_BUILD_VM_HANDOFF_TEST
    MICROS_BUILD_PM_SERVICE_TEST
    MICROS_BUILD_VM_SCRATCH_PREEMPTION_TEST
    MICROS_BUILD_TTY_SERVICE_TEST
    MICROS_BUILD_TTY_SERVICE_IMAGE
    MICROS_BUILD_RAMFS_SERVICE_IMAGE
    MICROS_BUILD_VFS_SERVICE_IMAGE
    MICROS_BUILD_RAMFS_SERVICE_TEST
    MICROS_BUILD_VFS_SERVICE_TEST
    MICROS_BUILD_ELF_SPAWN_TEST
    MICROS_BUILD_VM_READY_EARLY_TEST
    MICROS_BUILD_VM_SELF_FAULT_TEST
    MICROS_BUILD_VM_SELF_FAULT_SEALED_TEST
    MICROS_BUILD_NESTED_TRAP_TEST
    MICROS_BUILD_FRAME_OWNERSHIP_TEST
    MICROS_BUILD_USER_ADDRESS_SPACE_TEST
    MICROS_BUILD_ADDRESS_SPACE_HANDOFF_TEST
    MICROS_BUILD_USER_EXECUTION_TEST
    MICROS_BUILD_SCHEDULER_TEST
    MICROS_BUILD_SCHEDULER_INVALID_OUTGOING_TEST
    MICROS_BUILD_SCHEDULER_INVALID_NEXT_TEST
)
set(MICROS_TARGET_TEST_COUNT 0)
foreach(option_name IN LISTS MICROS_TARGET_TEST_OPTIONS)
    if(${option_name})
        math(EXPR MICROS_TARGET_TEST_COUNT "${MICROS_TARGET_TEST_COUNT} + 1")
    endif()
endforeach()
if(MICROS_TARGET_TEST_COUNT GREATER 1)
    message(FATAL_ERROR "Select at most one target test image")
endif()

find_program(MICROS_PYTHON NAMES python3 REQUIRED)

set(
    MICROS_USER_ELF_CHECKER_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_user_elf.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/user_elf_format.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/user_elf_parse.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/user_elf_structure.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/user_elf_instructions.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/user_elf_policy.py"
)
set(
    MICROS_APPLICATION_ELF_CHECKER_SOURCES
    ${MICROS_USER_ELF_CHECKER_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_application_elf.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/application_elf_policy.py"
)

set(
    MICROS_TTY_HANDOFF_RUNTIME_SOURCES
    kernel/tty_handoff_runtime.c
    kernel/tty_handoff_runtime_state.c
    kernel/tty_handoff_runtime_validate.c
    kernel/tty_handoff_runtime_transitions.c
)

set(
    MICROS_TTY_SERVICE_CORE_SOURCES
    servers/tty/tty_service_core.c
    servers/tty/tty_service_validate.c
    servers/tty/tty_service_read.c
    servers/tty/tty_service_write.c
    servers/tty/tty_service_request.c
)

set(
    MICROS_TTY_MODEL_TEST_SOURCES
    tests/host/tty_model_test.c
    tests/host/tty_model_common.c
    tests/host/tty_model_reference.c
    tests/host/tty_model_io.c
    tests/host/tty_model_protocol.c
    tests/host/tty_model_operations.c
    tests/host/tty_model_validate.c
)

set(
    MICROS_TTY_HANDOFF_HOST_TEST_SOURCES
    tests/host/tty_handoff_test.c
    tests/host/tty_handoff_test_fixture.c
    tests/host/tty_handoff_device_test.c
    tests/host/tty_handoff_runtime_test.c
)

set(
    MICROS_TTY_CONTROL_SYSCALL_HOST_TEST_SOURCES
    tests/host/tty_control_syscall_test.c
    tests/host/tty_control_syscall_test_fixture.c
    tests/host/tty_control_syscall_test_observe.c
    tests/host/tty_control_syscall_test_controller.c
)

set(
    MICROS_GRANT_COPY_TARGET_SOURCES
    kernel/grant_copy.c
    kernel/grant_copy_range.c
)

set(
    MICROS_GRANT_HOST_TEST_SOURCES
    tests/host/grant_test.c
    tests/host/grant_test_fixture.c
    tests/host/grant_lifecycle_test.c
    tests/host/grant_validation_test.c
    tests/host/grant_model_test.c
    tests/host/grant_model_reference.c
    tests/host/grant_model_operations.c
    tests/host/grant_model_copy.c
)

set(
    MICROS_GRANT_SYSCALL_QEMU_TEST_SOURCES
    kernel/grant_syscall_test.c
    kernel/grant_syscall_test_fixture.c
    kernel/grant_syscall_test_commands.c
    kernel/grant_syscall_test_copy_commands.c
    kernel/grant_syscall_test_transitions.c
    kernel/grant_syscall_test_effects.c
    kernel/grant_syscall_test_hooks.c
    kernel/grant_syscall_test_lifecycle.c
)

set(
    MICROS_KERNEL_OBJECT_CORE_SOURCES
    kernel/kernel_objects.c
    kernel/kernel_object_state.c
    kernel/kernel_object_identity.c
    kernel/kernel_object_stack.c
    kernel/kernel_object_process.c
    kernel/kernel_object_thread.c
    kernel/kernel_object_context.c
    kernel/kernel_object_hart.c
    kernel/kernel_object_validation.c
)

set(
    MICROS_KERNEL_OBJECT_HOST_TEST_SOURCES
    tests/host/kernel_objects_test.c
    tests/host/kernel_objects_test_fixture.c
    tests/host/kernel_objects_initialization_test.c
    tests/host/kernel_objects_process_test.c
    tests/host/kernel_objects_thread_test.c
    tests/host/kernel_objects_hart_test.c
    tests/host/kernel_objects_identity_test.c
    tests/host/kernel_objects_validation_test.c
    tests/host/kernel_objects_model_test.c
    tests/host/kernel_objects_model_reference.c
    tests/host/kernel_objects_model_projection.c
    tests/host/kernel_objects_model_operations.c
)

set(
    MICROS_KERNEL_OBJECT_QEMU_TEST_SOURCES
    kernel/object_model_test.c
    kernel/object_model_test_scenarios.c
)

set(
    MICROS_ENDPOINT_CORE_SOURCES
    kernel/endpoint.c
    kernel/endpoint_codec.c
    kernel/endpoint_identity.c
    kernel/endpoint_registry.c
    kernel/endpoint_privilege.c
    kernel/endpoint_completion_validation.c
    kernel/endpoint_queue_validation.c
    kernel/endpoint_object_validation.c
    kernel/endpoint_publication.c
    kernel/endpoint_close.c
)

set(
    MICROS_ENDPOINT_HOST_TEST_SOURCES
    tests/host/endpoint_test.c
    tests/host/endpoint_test_fixture.c
    tests/host/endpoint_identity_test.c
    tests/host/endpoint_profile_test.c
    tests/host/endpoint_validation_test.c
    tests/host/endpoint_lifecycle_test.c
    tests/host/endpoint_source_only_test.c
)

set(
    MICROS_ENDPOINT_MODEL_HOST_TEST_SOURCES
    tests/host/endpoint_model_test.c
    tests/host/endpoint_model_fixture.c
    tests/host/endpoint_model_reference.c
    tests/host/endpoint_model_projection.c
    tests/host/endpoint_model_operations.c
)

set(
    MICROS_ENDPOINT_QEMU_TEST_SOURCES
    kernel/endpoint_test.c
    kernel/endpoint_test_fixture.c
    kernel/endpoint_test_scenarios.c
)

set(
    MICROS_RAMFS_CORE_SOURCES
    servers/ramfs/ramfs_core.c
    servers/ramfs/ramfs_codec.c
    servers/ramfs/ramfs_ownership.c
    servers/ramfs/ramfs_state.c
    servers/ramfs/ramfs_transfer.c
    servers/ramfs/ramfs_namespace.c
    servers/ramfs/ramfs_io.c
    servers/ramfs/ramfs_directory.c
)

set(
    MICROS_RAMFS_HOST_TEST_SOURCES
    tests/host/ramfs_test.c
    tests/host/ramfs_test_fixture.c
    tests/host/ramfs_protocol_test.c
    tests/host/ramfs_namespace_test.c
    tests/host/ramfs_io_test.c
    tests/host/ramfs_directory_test.c
)

set(
    MICROS_RAMFS_SEED_HOST_TEST_SOURCES
    tests/host/ramfs_seed_test.c
    tests/host/ramfs_seed_test_fixture.c
    tests/host/ramfs_seed_boundary_test.c
    tests/host/ramfs_seed_corruption_test.c
)

set(
    MICROS_RAMFS_MODEL_HOST_TEST_SOURCES
    tests/host/ramfs_model_test.c
    tests/host/ramfs_model_fixture.c
    tests/host/ramfs_model_reference.c
    tests/host/ramfs_model_projection.c
    tests/host/ramfs_model_protocol.c
    tests/host/ramfs_model_operations.c
)

set(
    MICROS_RAMFS_QEMU_PEER_SOURCES
    tests/qemu/ramfs_service_vfs.c
    tests/qemu/ramfs_service_vfs_fixture.c
    tests/qemu/ramfs_service_vfs_ramfs.c
    tests/qemu/ramfs_service_vfs_scenario.c
    tests/qemu/ramfs_service_vfs_tty.c
)

set(
    MICROS_RAMFS_QEMU_REPORT_SOURCES
    kernel/ramfs_service_test.c
    kernel/ramfs_service_test_fixture.c
    kernel/ramfs_service_test_report.c
)

set(
    MICROS_RAMFS_SEED_IMAGE
    "${CMAKE_CURRENT_BINARY_DIR}/generated/ramfs_seed.bin"
)
add_custom_command(
    OUTPUT "${MICROS_RAMFS_SEED_IMAGE}"
    COMMAND
        "${MICROS_PYTHON}"
        -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_ramfs_seed.py"
        --manifest
        "${CMAKE_CURRENT_SOURCE_DIR}/servers/ramfs/seed.json"
        --repository-root
        "${CMAKE_CURRENT_SOURCE_DIR}"
        --output
        "${MICROS_RAMFS_SEED_IMAGE}"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/servers/ramfs/seed.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/servers/ramfs/seed/etc/motd"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_ramfs_seed.py"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBATIM
)
add_custom_target(
    ramfs_seed_image
    DEPENDS "${MICROS_RAMFS_SEED_IMAGE}"
)

set(
    MICROS_VFS_PORTABLE_SOURCES
    servers/vfs/vfs_core.c
    servers/vfs/vfs_state.c
    servers/vfs/vfs_ownership.c
    servers/vfs/vfs_transfer.c
    servers/vfs/vfs_namespace.c
    servers/vfs/vfs_io.c
    servers/vfs/vfs_console.c
    servers/vfs/vfs_process.c
    servers/vfs/vfs_spawn.c
)

set(
    MICROS_VFS_TEST_FIXTURE_SOURCES
    tests/host/vfs_test_fixture.c
    tests/host/vfs_test_fixture_grant.c
    tests/host/vfs_test_fixture_ramfs.c
    tests/host/vfs_test_fixture_tty.c
)

set(
    MICROS_VFS_CORE_TEST_SOURCES
    tests/host/vfs_test.c
    tests/host/vfs_core_test_fixture.c
    tests/host/vfs_core_state_test.c
    tests/host/vfs_core_process_test.c
    tests/host/vfs_core_namespace_test.c
    tests/host/vfs_core_io_test.c
    tests/host/vfs_core_directory_test.c
    tests/host/vfs_core_console_test.c
)

set(
    MICROS_VFS_SPAWN_TEST_SOURCES
    tests/host/vfs_spawn_test.c
    tests/host/vfs_spawn_test_fixture.c
    tests/host/vfs_spawn_image_test.c
    tests/host/vfs_spawn_descriptor_test.c
    tests/host/vfs_spawn_publication_test.c
)

set(
    MICROS_VFS_MODEL_TEST_SOURCES
    tests/host/vfs_model_test.c
    tests/host/vfs_model_context.c
    tests/host/vfs_model_reference.c
    tests/host/vfs_model_compare.c
    tests/host/vfs_model_process_operations.c
    tests/host/vfs_model_file_operations.c
    tests/host/vfs_model_console_operations.c
    tests/host/vfs_model_pressure.c
    tests/host/vfs_model_trace.c
)

set(
    MICROS_VFS_SERVICE_TEST_SOURCES
    tests/host/vfs_service_test.c
    tests/host/vfs_service_test_fixture.c
    tests/host/vfs_service_application_protocol_test.c
    tests/host/vfs_service_backend_protocol_test.c
    tests/host/vfs_service_pm_control_protocol_test.c
    tests/host/vfs_service_configuration_test.c
    tests/host/vfs_service_dispatch_test.c
)

set(
    MICROS_VFS_PROBE_SOURCES
    tests/qemu/vfs_service_probe.c
    tests/qemu/vfs_service_probe_client.c
    tests/qemu/vfs_service_probe_filesystem.c
    tests/qemu/vfs_service_probe_console.c
)

set(
    MICROS_IPC_CORE_SOURCES
    kernel/ipc_common.c
    kernel/ipc_queue.c
    kernel/ipc_completion.c
    kernel/ipc_tokens.c
    kernel/ipc_notifications.c
    kernel/ipc_deadlock.c
    kernel/ipc_rendezvous.c
    kernel/ipc_close.c
    kernel/ipc_reply.c
    kernel/ipc.c
)

set(
    MICROS_IPC_SYSCALL_SOURCES
    kernel/ipc_syscall_result.c
    kernel/ipc_syscall_request.c
    kernel/ipc_syscall.c
)

set(
    MICROS_IPC_OPERATION_HOST_TEST_SOURCES
    tests/host/ipc_operation_test.c
    tests/host/ipc_operation_test_fixture.c
    tests/host/ipc_send_operation_test.c
    tests/host/ipc_receive_operation_test.c
)

set(
    MICROS_IPC_CALL_HOST_TEST_SOURCES
    tests/host/ipc_call_test.c
    tests/host/ipc_call_test_fixture.c
    tests/host/ipc_call_delivery_test.c
    tests/host/ipc_call_validation_test.c
)

set(
    MICROS_IPC_REPLY_HOST_TEST_SOURCES
    tests/host/ipc_reply_test.c
    tests/host/ipc_reply_test_fixture.c
    tests/host/ipc_reply_authority_test.c
    tests/host/ipc_reply_validation_test.c
    tests/host/ipc_reply_receive_test.c
)

set(
    MICROS_IPC_NOTIFY_HOST_TEST_SOURCES
    tests/host/ipc_notify_test.c
    tests/host/ipc_notify_test_fixture.c
    tests/host/ipc_notify_delivery_test.c
    tests/host/ipc_kernel_notify_test.c
    tests/host/ipc_notify_validation_test.c
    tests/host/ipc_notify_model_test.c
)

set(
    MICROS_IPC_CLOSE_HOST_TEST_SOURCES
    tests/host/ipc_close_test.c
    tests/host/ipc_close_test_fixture.c
    tests/host/ipc_close_cancellation_test.c
    tests/host/ipc_close_validation_test.c
    tests/host/ipc_close_model_test.c
)

set(
    MICROS_IPC_MODEL_HOST_TEST_SOURCES
    tests/host/ipc_model_test.c
    tests/host/ipc_model_test_common.c
    tests/host/ipc_model_test_fixture.c
    tests/host/ipc_model_test_trace.c
    tests/host/ipc_model_reference_authority.c
    tests/host/ipc_model_reference_scheduler.c
    tests/host/ipc_model_reference_ipc_state.c
    tests/host/ipc_model_reference_rendezvous.c
    tests/host/ipc_model_reference_notifications.c
    tests/host/ipc_model_reference_lifecycle.c
    tests/host/ipc_model_reference.c
    tests/host/ipc_model_test_production.c
    tests/host/ipc_model_test_step.c
    tests/host/ipc_model_test_actions.c
    tests/host/ipc_model_test_prelude.c
)

set(
    MICROS_IPC_QEMU_TEST_SOURCES
    kernel/ipc_test.c
    kernel/ipc_test_fixture.c
    kernel/ipc_test_setup.c
    kernel/ipc_test_scenarios.c
    kernel/ipc_test_report.c
)

set(
    MICROS_IPC_ECALL_QEMU_TEST_SOURCES
    kernel/ipc_ecall_test.c
    kernel/ipc_ecall_test_fixture.c
    kernel/ipc_ecall_test_setup.c
    kernel/ipc_ecall_test_report.c
)

set(
    MICROS_IPC_SYSCALL_QEMU_TEST_SOURCES
    kernel/ipc_syscall_test.c
    kernel/ipc_syscall_test_fixture.c
    kernel/ipc_syscall_test_setup.c
    kernel/ipc_syscall_test_commands.c
    kernel/ipc_syscall_test_scenarios.c
    kernel/ipc_syscall_test_hooks.c
    kernel/ipc_syscall_test_report.c
)

set(
    MICROS_KERNEL_ADDRESS_SPACE_SOURCES
    kernel/address_space.c
    kernel/address_space_validation.c
)

set(
    MICROS_USER_ADDRESS_SPACE_CORE_SOURCES
    kernel/user_address_space_core.c
    kernel/user_address_space_vm_plan.c
)

set(
    MICROS_USER_ADDRESS_SPACE_SOURCES
    kernel/user_address_space.c
    kernel/user_address_space_runtime.c
    kernel/user_address_space_table.c
    kernel/user_address_space_validation.c
    kernel/user_address_space_mapping.c
    kernel/user_address_space_mutation.c
    kernel/user_address_space_handoff.c
    kernel/user_address_space_tty.c
    kernel/user_address_space_vm_scratch.c
    kernel/user_address_space_vm_freeze.c
    kernel/user_address_space_vm_teardown.c
    kernel/user_address_space_activation.c
)

set(
    MICROS_USER_EXECUTION_SOURCES
    kernel/user_execution.c
    kernel/user_execution_validation.c
    kernel/user_execution_trap.c
)

set(
    MICROS_USER_ADDRESS_SPACE_HOST_TEST_SOURCES
    tests/host/user_address_space_core_test.c
    tests/host/user_address_space_owner_test.c
    tests/host/user_address_space_vm_plan_test.c
)

set(
    MICROS_USER_ADDRESS_SPACE_QEMU_TEST_SOURCES
    kernel/user_address_space_test.c
    kernel/user_address_space_test_fixture.c
    kernel/user_address_space_test_lifecycle.c
    kernel/user_address_space_test_mapping.c
    kernel/user_address_space_test_validation.c
)

set(
    MICROS_USER_EXECUTION_QEMU_TEST_SOURCES
    kernel/user_execution_test.c
    kernel/user_execution_test_fixture.c
    kernel/user_execution_test_prepare.c
    kernel/user_execution_test_trap.c
    kernel/user_execution_test_lifecycle.c
)

set(
    MICROS_FRAME_OWNERSHIP_SOURCES
    kernel/frame_ownership.c
    kernel/frame_ownership_validation.c
    kernel/frame_ownership_bootstrap.c
    kernel/frame_ownership_handoff.c
    kernel/frame_ownership_vm_claim.c
    kernel/frame_ownership_vm_freeze.c
    kernel/frame_ownership_vm_release.c
)

set(
    MICROS_FRAME_ALLOCATOR_HOST_TEST_SOURCES
    tests/host/frame_allocator_test.c
    tests/host/frame_allocator_test_cases.c
    tests/host/frame_allocator_model_test.c
)

set(
    MICROS_FRAME_OWNERSHIP_HOST_TEST_SOURCES
    tests/host/frame_ownership_test.c
    tests/host/frame_ownership_test_fixture.c
    tests/host/frame_ownership_test_bootstrap.c
    tests/host/frame_ownership_test_validation.c
    tests/host/frame_ownership_test_handoff.c
    tests/host/frame_ownership_test_model.c
    tests/host/frame_ownership_test_vm.c
)

set(
    MICROS_FRAME_OWNERSHIP_QEMU_TEST_SOURCES
    kernel/frame_ownership_test.c
    kernel/frame_ownership_test_fixture.c
    kernel/frame_ownership_test_lifecycle.c
    kernel/frame_ownership_test_handoff.c
)

set(
    MICROS_VM_HANDOFF_CORE_SOURCES
    kernel/vm_handoff_core.c
    kernel/vm_handoff_abi.c
    kernel/vm_handoff_boot.c
    kernel/vm_handoff_load.c
    kernel/vm_handoff_scratch.c
    kernel/vm_handoff_freeze.c
    kernel/vm_handoff_commit.c
    kernel/vm_handoff_release.c
)

set(
    MICROS_VM_HANDOFF_SYSCALL_SOURCES
    kernel/vm_handoff_syscall.c
    kernel/vm_handoff_syscall_runtime.c
    kernel/vm_handoff_syscall_memory.c
    kernel/vm_handoff_syscall_boot.c
    kernel/vm_handoff_syscall_load.c
    kernel/vm_handoff_syscall_scratch.c
    kernel/vm_handoff_syscall_freeze.c
    kernel/vm_handoff_syscall_release.c
)

set(
    MICROS_VM_SNAPSHOT_SOURCES
    kernel/vm_snapshot.c
    kernel/vm_snapshot_validation.c
)

set(
    MICROS_VM_HANDOFF_CORE_HOST_TEST_SOURCES
    tests/host/vm_handoff_test.c
    tests/host/vm_handoff_test_fixture.c
    tests/host/vm_handoff_abi_test.c
    tests/host/vm_handoff_boot_test.c
    tests/host/vm_handoff_load_test.c
)

set(
    MICROS_VM_BOOTSTRAP_HOST_TEST_SOURCES
    tests/host/vm_bootstrap_test.c
    tests/host/vm_bootstrap_test_fixture.c
    tests/host/vm_bootstrap_abi_test.c
    tests/host/vm_bootstrap_build_test.c
)

set(
    MICROS_VM_HANDOFF_SYSCALL_HOST_TEST_SOURCES
    tests/host/vm_handoff_syscall_test.c
    tests/host/vm_handoff_syscall_test_fixture.c
    tests/host/vm_handoff_syscall_test_boot.c
    tests/host/vm_handoff_syscall_test_load.c
    tests/host/vm_handoff_syscall_test_runtime_backend.c
    tests/host/vm_handoff_syscall_test_boot_backend.c
    tests/host/vm_handoff_syscall_test_load_backend.c
)

set(
    MICROS_ADDRESS_SPACE_HANDOFF_QEMU_TEST_SOURCES
    kernel/address_space_handoff_test.c
    kernel/address_space_handoff_test_fixture.c
    kernel/address_space_handoff_test_setup.c
    kernel/address_space_handoff_test_snapshot.c
    kernel/address_space_handoff_test_handoff.c
    kernel/address_space_handoff_test_runtime.c
    kernel/address_space_handoff_test_syscall.c
    kernel/address_space_handoff_test_tty.c
    kernel/address_space_handoff_test_trap.c
)

set(
    MICROS_PM_CONTROL_CORE_SOURCES
    kernel/pm_control_core.c
    kernel/pm_control_abi.c
    kernel/pm_control_validation.c
    kernel/pm_control_reservation.c
    kernel/pm_control_execution.c
    kernel/pm_control_activation.c
    kernel/pm_control_exit.c
    kernel/pm_control_runtime_core.c
    kernel/pm_control_syscall_core.c
)

set(
    MICROS_PM_CONTROL_TARGET_SOURCES
    kernel/pm_control_runtime.c
    kernel/pm_control_syscall.c
    kernel/pm_control_syscall_runtime.c
    kernel/pm_control_syscall_reservation.c
    kernel/pm_control_syscall_execution.c
    kernel/pm_control_syscall_activation.c
    kernel/pm_control_syscall_exit.c
)

set(
    MICROS_PM_CONTROL_HOST_TEST_SOURCES
    tests/host/pm_control_test.c
    tests/host/pm_control_test_fixture.c
    tests/host/pm_control_abi_test.c
    tests/host/pm_control_syscall_test.c
    tests/host/pm_control_reservation_test.c
    tests/host/pm_control_runtime_test.c
)

set(
    MICROS_PM_CONTROL_EXECUTION_HOST_TEST_SOURCES
    tests/host/pm_control_execution_test.c
    tests/host/pm_control_execution_test_fixture.c
    tests/host/pm_control_execution_prepare_test.c
    tests/host/pm_control_execution_token_test.c
    tests/host/pm_control_execution_exit_test.c
)

set(
    MICROS_PM_CORE_SOURCES
    servers/pm/pm_common.c
    servers/pm/pm_core.c
    servers/pm/pm_protocol.c
    servers/pm/pm_validation.c
    servers/pm/pm_spawn.c
    servers/pm/pm_rollback.c
    servers/pm/pm_exit.c
    servers/pm/pm_wait.c
)

set(
    MICROS_PM_COORDINATOR_SOURCES
    servers/pm/pm_coordinator.c
    servers/pm/pm_coordinator_protocol.c
    servers/pm/pm_coordinator_peers.c
    servers/pm/pm_coordinator_spawn.c
    servers/pm/pm_coordinator_rollback.c
    servers/pm/pm_coordinator_requests.c
    servers/pm/pm_coordinator_frontend.c
    servers/pm/pm_coordinator_exit.c
    servers/pm/pm_coordinator_wait.c
)

set(
    MICROS_PM_RUNTIME_BACKEND_SOURCES
    servers/pm/pm_service_backend.c
)

set(
    MICROS_SPAWN_REQUEST_SOURCES
    lib/spawn/spawn_request.c
    lib/spawn/spawn_stack_layout.c
)

set(
    MICROS_ELF_SPAWN_RESOURCE_SOURCES
    kernel/elf_spawn_test_compare.c
    kernel/elf_spawn_test_service_compare.c
)

set(
    MICROS_PM_SERVICE_SOURCES
    servers/pm/pm_service.c
    servers/pm/pm_service_fixture.c
    servers/pm/pm_service_bootstrap.c
    servers/pm/pm_service_requests.c
    servers/pm/pm_service_self_test.c
    servers/pm/pm_service_scratch.c
)

set(
    MICROS_PM_CORE_HOST_TEST_SOURCES
    tests/host/pm_test.c
    tests/host/pm_test_fixture.c
    tests/host/pm_protocol_test.c
    tests/host/pm_spawn_test.c
    tests/host/pm_validation_test.c
    tests/host/pm_exit_wait_test.c
    tests/host/pm_model_test.c
    tests/host/pm_model_reference.c
    tests/host/pm_model_operations.c
)

set(
    MICROS_PM_COORDINATOR_HOST_TEST_SOURCES
    tests/host/pm_coordinator_test.c
    tests/host/pm_coordinator_configuration_test.c
    tests/host/pm_coordinator_protocol_test.c
    tests/host/pm_coordinator_spawn_fixture.c
    tests/host/pm_coordinator_spawn_test.c
    tests/host/pm_coordinator_request_test.c
    tests/host/pm_coordinator_exit_fixture.c
    tests/host/pm_coordinator_exit_wait_test.c
    tests/host/pm_coordinator_backend_test.c
)

set(
    MICROS_SPAWN_ABI_HOST_TEST_SOURCES
    tests/host/spawn_abi_test.c
    tests/host/spawn_abi_test_fixture.c
    tests/host/spawn_request_test.c
    tests/host/application_stack_test.c
    tests/host/vm_elf_test.c
)

set(
    MICROS_PM_SERVICE_QEMU_TEST_SOURCES
    kernel/pm_service_test.c
    kernel/pm_service_test_fixture.c
    kernel/pm_service_test_report.c
)

set(
    MICROS_VM_BOOT_VALIDATION_SOURCES
    lib/vm/vm_boot_validate.c
)

set(
    MICROS_VM_IMAGE_SOURCES
    servers/vm/vm_image.c
    servers/vm/vm_stack.c
    servers/vm/vm_elf.c
)

set(
    MICROS_VM_CORE_SOURCES
    servers/vm/vm_core.c
    servers/vm/vm_validation.c
    servers/vm/vm_frames.c
    servers/vm/vm_load.c
    servers/vm/vm_exit.c
)

set(
    MICROS_VM_CORE_TEST_FIXTURE_SOURCES
    tests/host/vm_core_test_fixture.c
)

set(
    MICROS_VM_CORE_HOST_TEST_SOURCES
    tests/host/vm_core_test.c
    tests/host/vm_core_boot_test.c
    tests/host/vm_core_load_test.c
    tests/host/vm_core_abort_test.c
    tests/host/vm_core_validation_test.c
)

set(
    MICROS_VM_SERVICE_CORE_SOURCES
    servers/vm/vm_service_core.c
    servers/vm/vm_service_protocol.c
    servers/vm/vm_service_load.c
    servers/vm/vm_service_release.c
)

set(
    MICROS_VM_SERVICE_BACKEND_SOURCES
    servers/vm/vm_service_backend.c
)

set(
    MICROS_VM_SERVICE_HOST_TEST_SOURCES
    tests/host/vm_service_test_fixture.c
    tests/host/vm_service_test.c
    tests/host/vm_service_protocol_test.c
    tests/host/vm_service_backend_test.c
)

set(
    MICROS_ELF_SPAWN_MODEL_SOURCES
    tests/host/elf_spawn_model_test.c
    tests/host/elf_spawn_model_context.c
    tests/host/elf_spawn_model_kernel.c
    tests/host/elf_spawn_model_vm.c
    tests/host/elf_spawn_model_transport.c
    tests/host/elf_spawn_model_operations.c
    tests/host/elf_spawn_model_reference.c
    tests/host/elf_spawn_model_compare.c
)

set(
    MICROS_VM_SERVICE_QEMU_SOURCES
    tests/qemu/vm_server.c
    tests/qemu/vm_server_fixture.c
    tests/qemu/vm_server_handoff.c
    tests/qemu/vm_server_probe.c
    tests/qemu/vm_server_tty.c
    tests/qemu/vm_server_scratch.c
    tests/qemu/vm_server_pm.c
    tests/qemu/vm_server_control.c
)

set(
    MICROS_VM_SCRATCH_PREEMPTION_QEMU_TEST_SOURCES
    kernel/vm_scratch_preemption_test.c
    kernel/vm_scratch_preemption_test_fixture.c
    kernel/vm_scratch_preemption_test_runtime.c
    kernel/vm_scratch_preemption_test_report.c
    kernel/vm_scratch_preemption_test_trap.c
)

set(
    MICROS_BOOTSTRAP_MANIFEST_SOURCES
    kernel/bootstrap_manifest.c
    kernel/bootstrap_manifest_common.c
    kernel/bootstrap_manifest_profiles.c
    kernel/bootstrap_transitions.c
)

set(
    MICROS_BOOTSTRAP_IMAGE_SOURCES
    kernel/bootstrap_image.c
)

set(
    MICROS_BOOTSTRAP_RUNTIME_SOURCES
    kernel/bootstrap_runtime.c
    kernel/bootstrap_runtime_state.c
    kernel/bootstrap_preparation.c
    kernel/bootstrap_preparation_image.c
    kernel/bootstrap_preparation_rollback.c
    kernel/bootstrap_runtime_validation.c
    kernel/bootstrap_diagnostics.c
)

set(
    MICROS_BOOTSTRAP_SYSCALL_SOURCES
    kernel/bootstrap_syscall.c
    kernel/bootstrap_syscall_common.c
    kernel/bootstrap_syscall_release.c
    kernel/bootstrap_syscall_ready.c
    kernel/bootstrap_syscall_complete.c
    kernel/bootstrap_syscall_failure.c
)

set(
    MICROS_BOOT_FOUNDATION_SOURCES
    kernel/main.c
    kernel/boot_foundation.c
    kernel/boot_component_tests.c
)

set(
    MICROS_BOOTSTRAP_LAUNCHER_SOURCES
    tests/qemu/bootstrap_launcher.c
    tests/qemu/bootstrap_launcher_common.c
    tests/qemu/bootstrap_launcher_manifest.c
    tests/qemu/bootstrap_launcher_protocol.c
    tests/qemu/bootstrap_launcher_control.c
)

set(
    MICROS_BOOTSTRAP_MANIFEST_HOST_TEST_SOURCES
    tests/host/bootstrap_manifest_test.c
    tests/host/bootstrap_manifest_fixture.c
    tests/host/bootstrap_manifest_acceptance_test.c
    tests/host/bootstrap_manifest_topology_test.c
    tests/host/bootstrap_manifest_tty_test.c
    tests/host/bootstrap_runtime_test.c
    tests/host/bootstrap_runtime_model.c
    tests/host/bootstrap_runtime_reference.c
)

set(
    MICROS_BOOTSTRAP_CONTROL_HOST_TEST_SOURCES
    tests/host/bootstrap_control_test.c
    tests/host/bootstrap_control_fixture.c
    tests/host/bootstrap_control_abi_test.c
    tests/host/bootstrap_control_lifecycle_test.c
    tests/host/bootstrap_control_completion_test.c
    tests/host/bootstrap_control_image_catalog_test.c
)

set(
    MICROS_FDT_HOST_TEST_SOURCES
    tests/host/fdt_test.c
    tests/host/fdt_test_fixture.c
    tests/host/fdt_header_test.c
    tests/host/fdt_memory_test.c
)

set(
    MICROS_SCHEDULER_CORE_SOURCES
    kernel/scheduler_core.c
    kernel/scheduler_core_queue.c
    kernel/scheduler_core_validate.c
    kernel/scheduler_core_policy.c
    kernel/scheduler_core_ipc.c
    kernel/scheduler_core_ipc_transition.c
    kernel/scheduler_core_return_plan.c
    kernel/scheduler_core_return.c
    kernel/scheduler_core_accounting.c
)

set(
    MICROS_SCHEDULER_RUNTIME_SOURCES
    kernel/scheduler.c
    kernel/scheduler_context.c
    kernel/scheduler_completion.c
    kernel/scheduler_return.c
    kernel/scheduler_interrupt.c
    kernel/scheduler_idle.c
    kernel/scheduler_test_support.c
)

set(
    MICROS_SCHEDULER_QEMU_TEST_SOURCES
    kernel/scheduler_test.c
    kernel/scheduler_test_state.c
    kernel/scheduler_test_fixture.c
    kernel/scheduler_test_completion.c
    kernel/scheduler_test_interrupt.c
    kernel/scheduler_test_trap.c
    kernel/scheduler_test_finish.c
)

set(
    MICROS_SCHEDULER_INVALID_QEMU_TEST_SOURCES
    kernel/scheduler_invalid_test.c
    kernel/scheduler_invalid_test_state.c
    kernel/scheduler_invalid_test_fixture.c
    kernel/scheduler_invalid_test_observation.c
)

set(
    MICROS_SCHEDULER_HOST_TEST_SOURCES
    tests/host/scheduler_test.c
    tests/host/scheduler_test_fixture.c
    tests/host/scheduler_queue_test.c
    tests/host/scheduler_return_test.c
    tests/host/scheduler_accounting_test.c
)

set(
    MICROS_SCHEDULER_MODEL_HOST_TEST_SOURCES
    tests/host/scheduler_model_test.c
    tests/host/scheduler_model_fixture.c
    tests/host/scheduler_model_reference.c
    tests/host/scheduler_model_projection.c
    tests/host/scheduler_model_prelude.c
    tests/host/scheduler_model_operations.c
)

set(
    MICROS_TRAP_SOURCES
    kernel/trap.c
    kernel/trap_context.c
    kernel/trap_user.c
    kernel/trap_interrupt.c
    kernel/trap_syscall.c
    kernel/trap_fault.c
    kernel/trap_test.c
    kernel/trap_test_hooks.c
)

set(
    MICROS_SYSCALL_SOURCES
    kernel/syscall.c
    kernel/syscall_decode.c
    kernel/syscall_context.c
)

set(
    MICROS_USER_RUNTIME_KERNEL_TEST_SOURCES
    kernel/user_runtime_test.c
    kernel/user_runtime_test_state.c
    kernel/user_runtime_test_image.c
    kernel/user_runtime_test_setup.c
    kernel/user_runtime_test_trap.c
    kernel/user_runtime_test_cleanup.c
    kernel/user_runtime_test_report.c
)

set(
    MICROS_USER_RUNTIME_SERVICE_TEST_SOURCES
    tests/qemu/user_runtime_service.c
    tests/qemu/user_runtime_service_common.c
    tests/qemu/user_runtime_client.c
    tests/qemu/user_runtime_responders.c
)

set(
    MICROS_RUNTIME_HOST_TEST_SOURCES
    tests/host/runtime_test.c
    tests/host/runtime_test_fixture.c
    tests/host/runtime_wrapper_test.c
)

if(MICROS_BUILD_HOST_TESTS)
    enable_testing()

    add_executable(
        fdt_test
        kernel/platform/fdt.c
        ${MICROS_FDT_HOST_TEST_SOURCES}
    )
    add_executable(
        frame_allocator_test
        kernel/frame_allocator.c
        ${MICROS_FRAME_ALLOCATOR_HOST_TEST_SOURCES}
    )
    add_executable(
        sv39_test
        kernel/sv39.c
        tests/host/sv39_test.c
    )
    add_executable(
        kernel_objects_test
        ${MICROS_KERNEL_OBJECT_CORE_SOURCES}
        ${MICROS_SCHEDULER_CORE_SOURCES}
        ${MICROS_KERNEL_OBJECT_HOST_TEST_SOURCES}
    )
    add_executable(
        frame_ownership_test
        kernel/frame_allocator.c
        ${MICROS_FRAME_OWNERSHIP_SOURCES}
        ${MICROS_KERNEL_OBJECT_CORE_SOURCES}
        ${MICROS_SCHEDULER_CORE_SOURCES}
        ${MICROS_FRAME_OWNERSHIP_HOST_TEST_SOURCES}
    )
    add_executable(
        scheduler_test
        ${MICROS_KERNEL_OBJECT_CORE_SOURCES}
        ${MICROS_SCHEDULER_CORE_SOURCES}
        tests/host/ipc_current_guard_test.c
        ${MICROS_SCHEDULER_MODEL_HOST_TEST_SOURCES}
        ${MICROS_SCHEDULER_HOST_TEST_SOURCES}
    )
    add_executable(
        endpoint_test
        ${MICROS_KERNEL_OBJECT_CORE_SOURCES}
        ${MICROS_SCHEDULER_CORE_SOURCES}
        ${MICROS_ENDPOINT_CORE_SOURCES}
        kernel/grant.c
        kernel/grant_abi.c
        kernel/grant_copy_core.c
        kernel/grant_syscall_core.c
        kernel/sv39.c
        ${MICROS_USER_ADDRESS_SPACE_CORE_SOURCES}
        kernel/ipc_abi.c
        ${MICROS_IPC_CORE_SOURCES}
        tests/host/ipc_abi_test.c
        ${MICROS_IPC_CALL_HOST_TEST_SOURCES}
        ${MICROS_IPC_CLOSE_HOST_TEST_SOURCES}
        tests/host/ipc_deadlock_test.c
        tests/host/ipc_delivery_test.c
        ${MICROS_GRANT_HOST_TEST_SOURCES}
        tests/host/grant_copy_test.c
        tests/host/grant_syscall_core_test.c
        ${MICROS_USER_ADDRESS_SPACE_HOST_TEST_SOURCES}
        ${MICROS_IPC_NOTIFY_HOST_TEST_SOURCES}
        ${MICROS_IPC_OPERATION_HOST_TEST_SOURCES}
        tests/host/ipc_queue_test.c
        tests/host/ipc_queue_mutation_test.c
        ${MICROS_IPC_REPLY_HOST_TEST_SOURCES}
        tests/host/ipc_state_test.c
        ${MICROS_ENDPOINT_MODEL_HOST_TEST_SOURCES}
        ${MICROS_ENDPOINT_HOST_TEST_SOURCES}
    )
    add_executable(
        ipc_model_test
        ${MICROS_KERNEL_OBJECT_CORE_SOURCES}
        ${MICROS_SCHEDULER_CORE_SOURCES}
        ${MICROS_ENDPOINT_CORE_SOURCES}
        kernel/grant.c
        kernel/grant_copy_core.c
        ${MICROS_IPC_CORE_SOURCES}
        ${MICROS_ENDPOINT_MODEL_HOST_TEST_SOURCES}
        ${MICROS_GRANT_HOST_TEST_SOURCES}
        tests/host/grant_copy_test.c
        ${MICROS_IPC_CLOSE_HOST_TEST_SOURCES}
        tests/host/ipc_model_main.c
        ${MICROS_IPC_MODEL_HOST_TEST_SOURCES}
        ${MICROS_IPC_NOTIFY_HOST_TEST_SOURCES}
    )
    add_executable(
        runtime_test
        lib/runtime/memory.c
        lib/runtime/runtime.c
        tests/qemu/bootstrap_launcher_control.c
        ${MICROS_RUNTIME_HOST_TEST_SOURCES}
    )
    add_executable(
        bootstrap_test
        ${MICROS_BOOTSTRAP_MANIFEST_SOURCES}
        ${MICROS_BOOTSTRAP_MANIFEST_HOST_TEST_SOURCES}
    )
    add_executable(
        bootstrap_control_test
        kernel/bootstrap_control.c
        kernel/bootstrap_control_core.c
        ${MICROS_BOOTSTRAP_IMAGE_SOURCES}
        ${MICROS_BOOTSTRAP_MANIFEST_SOURCES}
        ${MICROS_ENDPOINT_CORE_SOURCES}
        ${MICROS_IPC_CORE_SOURCES}
        ${MICROS_KERNEL_OBJECT_CORE_SOURCES}
        ${MICROS_SCHEDULER_CORE_SOURCES}
        ${MICROS_BOOTSTRAP_CONTROL_HOST_TEST_SOURCES}
    )
    add_executable(
        vm_bootstrap_test
        kernel/frame_allocator.c
        ${MICROS_FRAME_OWNERSHIP_SOURCES}
        kernel/vm_bootstrap.c
        ${MICROS_VM_HANDOFF_CORE_SOURCES}
        ${MICROS_VM_BOOT_VALIDATION_SOURCES}
        ${MICROS_VM_HANDOFF_CORE_HOST_TEST_SOURCES}
        ${MICROS_VM_BOOTSTRAP_HOST_TEST_SOURCES}
    )
    add_executable(
        pm_test
        kernel/frame_allocator.c
        ${MICROS_FRAME_OWNERSHIP_SOURCES}
        ${MICROS_ENDPOINT_CORE_SOURCES}
        ${MICROS_IPC_CORE_SOURCES}
        ${MICROS_KERNEL_OBJECT_CORE_SOURCES}
        ${MICROS_PM_CONTROL_CORE_SOURCES}
        ${MICROS_SCHEDULER_CORE_SOURCES}
        ${MICROS_VM_HANDOFF_CORE_SOURCES}
        ${MICROS_PM_CORE_SOURCES}
        ${MICROS_PM_CONTROL_HOST_TEST_SOURCES}
        ${MICROS_PM_CONTROL_EXECUTION_HOST_TEST_SOURCES}
        ${MICROS_PM_CORE_HOST_TEST_SOURCES}
    )
    add_executable(
        pm_coordinator_test
        ${MICROS_PM_CORE_SOURCES}
        ${MICROS_PM_COORDINATOR_SOURCES}
        ${MICROS_SPAWN_REQUEST_SOURCES}
        ${MICROS_PM_RUNTIME_BACKEND_SOURCES}
        servers/pm/pm_control.c
        lib/runtime/runtime.c
        ${MICROS_PM_COORDINATOR_HOST_TEST_SOURCES}
    )
    target_include_directories(
        pm_coordinator_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_compile_features(pm_coordinator_test PRIVATE c_std_17)
    target_compile_options(
        pm_coordinator_test
        PRIVATE
            -Wall
            -Wextra
            -Werror
            -Wpedantic
            -Wmissing-prototypes
            -Wstrict-prototypes
    )
    if(
        MICROS_ENABLE_SANITIZERS
        AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU"
    )
        target_compile_options(
            pm_coordinator_test
            PRIVATE
                -fno-omit-frame-pointer
                -fsanitize=address,undefined
        )
        target_link_options(
            pm_coordinator_test
            PRIVATE -fsanitize=address,undefined
        )
    endif()
    add_executable(
        spawn_abi_test
        ${MICROS_SPAWN_REQUEST_SOURCES}
        ${MICROS_VM_IMAGE_SOURCES}
        ${MICROS_SPAWN_ABI_HOST_TEST_SOURCES}
    )
    add_executable(
        application_runtime_test
        lib/runtime/application.c
        lib/runtime/application_stack.c
        lib/runtime/runtime.c
        ${MICROS_SPAWN_REQUEST_SOURCES}
        ${MICROS_VM_IMAGE_SOURCES}
        tests/host/spawn_abi_test_fixture.c
        tests/host/application_runtime_test.c
        tests/host/application_runtime_test_fixture.c
        tests/host/application_runtime_frontend_test.c
    )
    target_include_directories(
        application_runtime_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_compile_features(application_runtime_test PRIVATE c_std_17)
    target_compile_options(
        application_runtime_test
        PRIVATE
            -Wall
            -Wextra
            -Werror
            -Wpedantic
            -Wmissing-prototypes
            -Wstrict-prototypes
    )
    if(MICROS_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(
            application_runtime_test
            PRIVATE
                -fno-omit-frame-pointer
                -fsanitize=address,undefined
        )
        target_link_options(
            application_runtime_test
            PRIVATE
                -fsanitize=address,undefined
        )
    endif()
    add_executable(
        elf_spawn_fixture_test
        ${MICROS_SPAWN_REQUEST_SOURCES}
        ${MICROS_ELF_SPAWN_RESOURCE_SOURCES}
        tests/qemu/elf_spawn_request.c
        tests/qemu/elf_spawn_client.c
        tests/qemu/elf_spawn_client_vfs.c
        tests/host/elf_spawn_fixture_test.c
        tests/host/elf_spawn_resource_test.c
        tests/host/elf_spawn_client_test.c
    )
    target_include_directories(
        elf_spawn_fixture_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_compile_features(elf_spawn_fixture_test PRIVATE c_std_17)
    target_compile_definitions(elf_spawn_fixture_test PRIVATE MICROS_BUILD_ELF_SPAWN_TEST=1)
    target_compile_options(
        elf_spawn_fixture_test
        PRIVATE
            -Wall
            -Wextra
            -Werror
            -Wpedantic
            -Wmissing-prototypes
            -Wstrict-prototypes
    )
    if(MICROS_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(
            elf_spawn_fixture_test
            PRIVATE
                -fno-omit-frame-pointer
                -fsanitize=address,undefined
        )
        target_link_options(
            elf_spawn_fixture_test
            PRIVATE
                -fsanitize=address,undefined
        )
    endif()
    add_executable(
        vm_core_test
        kernel/frame_allocator.c
        ${MICROS_FRAME_OWNERSHIP_SOURCES}
        kernel/vm_bootstrap.c
        ${MICROS_VM_BOOT_VALIDATION_SOURCES}
        ${MICROS_SPAWN_REQUEST_SOURCES}
        ${MICROS_VM_IMAGE_SOURCES}
        ${MICROS_VM_CORE_SOURCES}
        ${MICROS_VM_CORE_HOST_TEST_SOURCES}
        ${MICROS_VM_CORE_TEST_FIXTURE_SOURCES}
        ${MICROS_VM_SERVICE_CORE_SOURCES}
        ${MICROS_VM_SERVICE_BACKEND_SOURCES}
        ${MICROS_VM_SERVICE_HOST_TEST_SOURCES}
        lib/runtime/runtime.c
    )
    add_executable(
        elf_spawn_model_test
        kernel/frame_allocator.c
        ${MICROS_FRAME_OWNERSHIP_SOURCES}
        kernel/vm_bootstrap.c
        ${MICROS_VM_BOOT_VALIDATION_SOURCES}
        ${MICROS_VM_IMAGE_SOURCES}
        ${MICROS_VM_CORE_SOURCES}
        ${MICROS_VM_SERVICE_CORE_SOURCES}
        ${MICROS_VM_CORE_TEST_FIXTURE_SOURCES}
        ${MICROS_PM_CORE_SOURCES}
        ${MICROS_PM_COORDINATOR_SOURCES}
        ${MICROS_SPAWN_REQUEST_SOURCES}
        ${MICROS_VFS_PORTABLE_SOURCES}
        ${MICROS_VFS_TEST_FIXTURE_SOURCES}
        servers/vfs/vfs_service_core.c
        ${MICROS_ELF_SPAWN_MODEL_SOURCES}
    )
    target_include_directories(
        elf_spawn_model_test
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_compile_features(elf_spawn_model_test PRIVATE c_std_17)
    target_compile_options(
        elf_spawn_model_test
        PRIVATE -Wall -Wextra -Werror -Wpedantic -Wmissing-prototypes -Wstrict-prototypes
    )
    if(MICROS_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(
            elf_spawn_model_test PRIVATE -fno-omit-frame-pointer -fsanitize=address,undefined
        )
        target_link_options(elf_spawn_model_test PRIVATE -fsanitize=address,undefined)
    endif()
    add_executable(
        ramfs_seed_test
        servers/ramfs/ramfs_seed.c
        ${MICROS_RAMFS_SEED_HOST_TEST_SOURCES}
    )
    add_dependencies(ramfs_seed_test ramfs_seed_image)
    add_executable(
        ramfs_test
        ${MICROS_RAMFS_CORE_SOURCES}
        servers/ramfs/ramfs_seed.c
        ${MICROS_RAMFS_HOST_TEST_SOURCES}
    )
    add_dependencies(ramfs_test ramfs_seed_image)
    add_executable(
        ramfs_model_test
        ${MICROS_RAMFS_CORE_SOURCES}
        servers/ramfs/ramfs_seed.c
        ${MICROS_RAMFS_MODEL_HOST_TEST_SOURCES}
    )
    add_dependencies(ramfs_model_test ramfs_seed_image)
    add_executable(
        vfs_test
        ${MICROS_SPAWN_REQUEST_SOURCES}
        ${MICROS_VFS_PORTABLE_SOURCES}
        ${MICROS_VFS_TEST_FIXTURE_SOURCES}
        ${MICROS_VFS_CORE_TEST_SOURCES}
        ${MICROS_VFS_SPAWN_TEST_SOURCES}
    )
    add_executable(
        vfs_model_test
        ${MICROS_SPAWN_REQUEST_SOURCES}
        ${MICROS_VFS_PORTABLE_SOURCES}
        ${MICROS_VFS_TEST_FIXTURE_SOURCES}
        ${MICROS_VFS_MODEL_TEST_SOURCES}
    )
    add_executable(
        vfs_service_test
        ${MICROS_SPAWN_REQUEST_SOURCES}
        ${MICROS_VFS_PORTABLE_SOURCES}
        servers/vfs/vfs_service_core.c
        ${MICROS_VFS_TEST_FIXTURE_SOURCES}
        ${MICROS_VFS_SERVICE_TEST_SOURCES}
    )
    add_executable(
        tty_test
        kernel/platform/plic_core.c
        kernel/tty_control_core.c
        kernel/tty_control_syscall_core.c
        kernel/tty_handoff_core.c
        ${MICROS_TTY_HANDOFF_RUNTIME_SOURCES}
        kernel/uart_console_core.c
        servers/tty/tty_core.c
        tests/host/plic_test.c
        tests/host/tty_control_test.c
        ${MICROS_TTY_HANDOFF_HOST_TEST_SOURCES}
        tests/host/tty_test.c
    )
    add_executable(
        tty_service_test
        servers/tty/tty_core.c
        ${MICROS_TTY_SERVICE_CORE_SOURCES}
        tests/host/tty_service_test.c
    )
    add_executable(
        tty_uart_test
        servers/tty/tty_core.c
        servers/tty/tty_uart.c
        tests/host/tty_uart_test.c
    )
    add_executable(
        tty_model_test
        servers/tty/tty_core.c
        ${MICROS_TTY_SERVICE_CORE_SOURCES}
        servers/tty/tty_uart.c
        ${MICROS_TTY_MODEL_TEST_SOURCES}
    )
    add_executable(
        tty_service_control_test
        servers/tty/tty_control.c
        tests/host/tty_service_control_test.c
    )
    add_executable(
        vm_handoff_syscall_test
        kernel/tty_handoff_core.c
        ${MICROS_VM_HANDOFF_CORE_SOURCES}
        ${MICROS_VM_HANDOFF_SYSCALL_SOURCES}
        ${MICROS_VM_HANDOFF_SYSCALL_HOST_TEST_SOURCES}
    )
    add_executable(
        tty_interrupt_test
        kernel/tty_handoff_core.c
        kernel/tty_interrupt.c
        tests/host/tty_interrupt_test.c
    )
    add_executable(
        tty_control_syscall_test
        kernel/tty_control_core.c
        kernel/tty_control_syscall.c
        kernel/tty_handoff_core.c
        ${MICROS_TTY_CONTROL_SYSCALL_HOST_TEST_SOURCES}
    )
    add_executable(
        tty_fault_test
        kernel/tty_fault.c
        tests/host/tty_fault_test.c
    )
    add_executable(
        trap_route_test
        kernel/trap_route_core.c
        tests/host/trap_route_test.c
    )
    target_include_directories(
        vm_handoff_syscall_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_include_directories(
        tty_interrupt_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_include_directories(
        trap_route_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_include_directories(
        tty_control_syscall_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/host/stubs"
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_include_directories(
        tty_fault_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_compile_features(
        vm_handoff_syscall_test
        PRIVATE
            c_std_17
    )
    target_compile_features(tty_interrupt_test PRIVATE c_std_17)
    target_compile_features(trap_route_test PRIVATE c_std_17)
    target_compile_features(tty_control_syscall_test PRIVATE c_std_17)
    target_compile_features(tty_fault_test PRIVATE c_std_17)
    target_compile_options(
        vm_handoff_syscall_test
        PRIVATE
            -Wall
            -Wextra
            -Werror
            -Wpedantic
            -Wmissing-prototypes
            -Wstrict-prototypes
    )
    target_compile_options(
        tty_interrupt_test
        PRIVATE
            -Wall
            -Wextra
            -Werror
            -Wpedantic
            -Wmissing-prototypes
            -Wstrict-prototypes
    )
    target_compile_options(
        trap_route_test
        PRIVATE
            -Wall
            -Wextra
            -Werror
            -Wpedantic
            -Wmissing-prototypes
            -Wstrict-prototypes
    )
    target_compile_options(
        tty_control_syscall_test
        PRIVATE
            -Wall
            -Wextra
            -Werror
            -Wpedantic
            -Wmissing-prototypes
            -Wstrict-prototypes
    )
    target_compile_options(
        tty_fault_test
        PRIVATE
            -Wall
            -Wextra
            -Werror
            -Wpedantic
            -Wmissing-prototypes
            -Wstrict-prototypes
    )
    target_include_directories(
        fdt_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_include_directories(
        frame_allocator_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_include_directories(
        sv39_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_include_directories(
        kernel_objects_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_include_directories(
        frame_ownership_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_include_directories(
        scheduler_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_include_directories(
        endpoint_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_include_directories(
        ipc_model_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_include_directories(
        runtime_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_include_directories(
        bootstrap_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_include_directories(
        bootstrap_control_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_include_directories(
        vm_bootstrap_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_include_directories(
        pm_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_include_directories(
        spawn_abi_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_include_directories(
        vm_core_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_include_directories(
        ramfs_seed_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_include_directories(
        ramfs_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_include_directories(
        ramfs_model_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_include_directories(
        vfs_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_include_directories(
        vfs_model_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_include_directories(
        vfs_service_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_include_directories(
        tty_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_include_directories(
        tty_service_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_include_directories(
        tty_uart_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_include_directories(
        tty_model_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_include_directories(
        tty_service_control_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_compile_features(fdt_test PRIVATE c_std_17)
    target_compile_features(frame_allocator_test PRIVATE c_std_17)
    target_compile_features(sv39_test PRIVATE c_std_17)
    target_compile_features(kernel_objects_test PRIVATE c_std_17)
    target_compile_features(frame_ownership_test PRIVATE c_std_17)
    target_compile_features(scheduler_test PRIVATE c_std_17)
    target_compile_features(endpoint_test PRIVATE c_std_17)
    target_compile_features(ipc_model_test PRIVATE c_std_17)
    target_compile_features(runtime_test PRIVATE c_std_17)
    target_compile_features(bootstrap_test PRIVATE c_std_17)
    target_compile_features(bootstrap_control_test PRIVATE c_std_17)
    target_compile_features(vm_bootstrap_test PRIVATE c_std_17)
    target_compile_features(pm_test PRIVATE c_std_17)
    target_compile_features(spawn_abi_test PRIVATE c_std_17)
    target_compile_features(vm_core_test PRIVATE c_std_17)
    target_compile_features(ramfs_seed_test PRIVATE c_std_17)
    target_compile_features(ramfs_test PRIVATE c_std_17)
    target_compile_features(ramfs_model_test PRIVATE c_std_17)
    target_compile_features(vfs_test PRIVATE c_std_17)
    target_compile_features(vfs_model_test PRIVATE c_std_17)
    target_compile_features(vfs_service_test PRIVATE c_std_17)
    target_compile_definitions(
        vfs_test
        PRIVATE
            MICROS_BUILD_VFS_SERVICE_TEST=1
    )
    target_compile_definitions(
        vfs_model_test
        PRIVATE
            MICROS_BUILD_VFS_SERVICE_TEST=1
    )
    target_compile_definitions(
        vfs_service_test
        PRIVATE
            MICROS_BUILD_VFS_SERVICE_TEST=1
    )
    target_compile_features(tty_test PRIVATE c_std_17)
    target_compile_features(tty_service_test PRIVATE c_std_17)
    target_compile_features(tty_uart_test PRIVATE c_std_17)
    target_compile_features(tty_model_test PRIVATE c_std_17)
    target_compile_features(tty_service_control_test PRIVATE c_std_17)
    target_compile_options(
        fdt_test
        PRIVATE
            -Wall
            -Wextra
            -Werror
            -Wpedantic
            -Wmissing-prototypes
            -Wstrict-prototypes
    )
    target_compile_options(
        frame_allocator_test
        PRIVATE
            -Wall
            -Wextra
            -Werror
            -Wpedantic
            -Wmissing-prototypes
            -Wstrict-prototypes
    )
    target_compile_options(
        sv39_test
        PRIVATE
            -Wall
            -Wextra
            -Werror
            -Wpedantic
            -Wmissing-prototypes
            -Wstrict-prototypes
    )
    target_compile_options(
        kernel_objects_test
        PRIVATE
            -Wall
            -Wextra
            -Werror
            -Wpedantic
            -Wmissing-prototypes
            -Wstrict-prototypes
    )
    target_compile_options(
        frame_ownership_test
        PRIVATE
            -Wall
            -Wextra
            -Werror
            -Wpedantic
            -Wmissing-prototypes
            -Wstrict-prototypes
    )
    target_compile_options(
        scheduler_test
        PRIVATE
            -Wall
            -Wextra
            -Werror
            -Wpedantic
            -Wmissing-prototypes
            -Wstrict-prototypes
    )
    target_compile_options(
        endpoint_test
        PRIVATE
            -Wall
            -Wextra
            -Werror
            -Wpedantic
            -Wmissing-prototypes
            -Wstrict-prototypes
    )
    target_compile_options(
        ipc_model_test
        PRIVATE
            -Wall
            -Wextra
            -Werror
            -Wpedantic
            -Wmissing-prototypes
            -Wstrict-prototypes
    )
    target_compile_options(
        runtime_test
        PRIVATE
            -Wall
            -Wextra
            -Werror
            -Wpedantic
            -Wmissing-prototypes
            -Wstrict-prototypes
    )
    target_compile_options(
        bootstrap_test
        PRIVATE
            -Wall
            -Wextra
            -Werror
            -Wpedantic
            -Wmissing-prototypes
            -Wstrict-prototypes
    )
    target_compile_options(
        bootstrap_control_test
        PRIVATE
            -Wall
            -Wextra
            -Werror
            -Wpedantic
            -Wmissing-prototypes
            -Wstrict-prototypes
    )
    target_compile_options(
        vm_bootstrap_test
        PRIVATE
            -Wall
            -Wextra
            -Werror
            -Wpedantic
            -Wmissing-prototypes
            -Wstrict-prototypes
    )
    target_compile_options(
        pm_test
        PRIVATE
            -Wall
            -Wextra
            -Werror
            -Wpedantic
            -Wmissing-prototypes
            -Wstrict-prototypes
    )
    target_compile_options(
        spawn_abi_test
        PRIVATE
            -Wall
            -Wextra
            -Werror
            -Wpedantic
            -Wmissing-prototypes
            -Wstrict-prototypes
    )
    target_compile_options(
        vm_core_test
        PRIVATE
            -Wall
            -Wextra
            -Werror
            -Wpedantic
            -Wmissing-prototypes
            -Wstrict-prototypes
    )
    target_compile_options(
        ramfs_seed_test
        PRIVATE
            -Wall
            -Wextra
            -Werror
            -Wpedantic
            -Wmissing-prototypes
            -Wstrict-prototypes
    )
    target_compile_options(
        ramfs_test
        PRIVATE
            -Wall
            -Wextra
            -Werror
            -Wpedantic
            -Wmissing-prototypes
            -Wstrict-prototypes
    )
    target_compile_options(
        ramfs_model_test
        PRIVATE
            -Wall
            -Wextra
            -Werror
            -Wpedantic
            -Wmissing-prototypes
            -Wstrict-prototypes
    )
    target_compile_options(
        vfs_test
        PRIVATE
            -Wall
            -Wextra
            -Werror
            -Wpedantic
            -Wmissing-prototypes
            -Wstrict-prototypes
    )
    target_compile_options(
        vfs_model_test
        PRIVATE
            -Wall
            -Wextra
            -Werror
            -Wpedantic
            -Wmissing-prototypes
            -Wstrict-prototypes
    )
    target_compile_options(
        vfs_service_test
        PRIVATE
            -Wall
            -Wextra
            -Werror
            -Wpedantic
            -Wmissing-prototypes
            -Wstrict-prototypes
    )
    target_compile_options(
        tty_test
        PRIVATE
            -Wall
            -Wextra
            -Werror
            -Wpedantic
            -Wmissing-prototypes
            -Wstrict-prototypes
    )
    target_compile_options(
        tty_service_test
        PRIVATE
            -Wall
            -Wextra
            -Werror
            -Wpedantic
            -Wmissing-prototypes
            -Wstrict-prototypes
    )
    target_compile_options(
        tty_uart_test
        PRIVATE
            -Wall
            -Wextra
            -Werror
            -Wpedantic
            -Wmissing-prototypes
            -Wstrict-prototypes
    )
    target_compile_options(
        tty_model_test
        PRIVATE
            -Wall
            -Wextra
            -Werror
            -Wpedantic
            -Wmissing-prototypes
            -Wstrict-prototypes
    )
    target_compile_options(
        tty_service_control_test
        PRIVATE
            -Wall
            -Wextra
            -Werror
            -Wpedantic
            -Wmissing-prototypes
            -Wstrict-prototypes
    )

    if(
        MICROS_ENABLE_SANITIZERS
        AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU"
    )
        target_compile_options(
            fdt_test
            PRIVATE
                -fno-omit-frame-pointer
                -fsanitize=address,undefined
        )
        target_link_options(fdt_test PRIVATE -fsanitize=address,undefined)
        target_compile_options(
            frame_allocator_test
            PRIVATE
                -fno-omit-frame-pointer
                -fsanitize=address,undefined
        )
        target_link_options(
            frame_allocator_test
            PRIVATE
                -fsanitize=address,undefined
        )
        target_compile_options(
            sv39_test
            PRIVATE
                -fno-omit-frame-pointer
                -fsanitize=address,undefined
        )
        target_link_options(sv39_test PRIVATE -fsanitize=address,undefined)
        target_compile_options(
            kernel_objects_test
            PRIVATE
                -fno-omit-frame-pointer
                -fsanitize=address,undefined
        )
        target_link_options(
            kernel_objects_test
            PRIVATE
                -fsanitize=address,undefined
        )
        target_compile_options(
            frame_ownership_test
            PRIVATE
                -fno-omit-frame-pointer
                -fsanitize=address,undefined
        )
        target_link_options(
            frame_ownership_test
            PRIVATE
                -fsanitize=address,undefined
        )
        target_compile_options(
            scheduler_test
            PRIVATE
                -fno-omit-frame-pointer
                -fsanitize=address,undefined
        )
        target_link_options(
            scheduler_test
            PRIVATE
                -fsanitize=address,undefined
        )
        target_compile_options(
            endpoint_test
            PRIVATE
                -fno-omit-frame-pointer
                -fsanitize=address,undefined
        )
        target_link_options(
            endpoint_test
            PRIVATE
                -fsanitize=address,undefined
        )
        target_compile_options(
            ipc_model_test
            PRIVATE
                -fno-omit-frame-pointer
                -fsanitize=address,undefined
        )
        target_link_options(
            ipc_model_test
            PRIVATE
                -fsanitize=address,undefined
        )
        target_compile_options(
            runtime_test
            PRIVATE
                -fno-omit-frame-pointer
                -fsanitize=address,undefined
        )
        target_link_options(
            runtime_test
            PRIVATE
                -fsanitize=address,undefined
        )
        target_compile_options(
            bootstrap_test
            PRIVATE
                -fno-omit-frame-pointer
                -fsanitize=address,undefined
        )
        target_link_options(
            bootstrap_test
            PRIVATE
                -fsanitize=address,undefined
        )
        target_compile_options(
            bootstrap_control_test
            PRIVATE
                -fno-omit-frame-pointer
                -fsanitize=address,undefined
        )
        target_link_options(
            bootstrap_control_test
            PRIVATE
                -fsanitize=address,undefined
        )
        target_compile_options(
            vm_bootstrap_test
            PRIVATE
                -fno-omit-frame-pointer
                -fsanitize=address,undefined
        )
        target_link_options(
            vm_bootstrap_test
            PRIVATE
                -fsanitize=address,undefined
        )
        target_compile_options(
            pm_test
            PRIVATE
                -fno-omit-frame-pointer
                -fsanitize=address,undefined
        )
        target_link_options(
            pm_test
            PRIVATE
                -fsanitize=address,undefined
        )
        target_compile_options(
            spawn_abi_test
            PRIVATE
                -fno-omit-frame-pointer
                -fsanitize=address,undefined
        )
        target_link_options(
            spawn_abi_test
            PRIVATE
                -fsanitize=address,undefined
        )
        target_compile_options(
            vm_core_test
            PRIVATE
                -fno-omit-frame-pointer
                -fsanitize=address,undefined
        )
        target_link_options(
            vm_core_test
            PRIVATE
                -fsanitize=address,undefined
        )
        target_compile_options(
            ramfs_seed_test
            PRIVATE
                -fno-omit-frame-pointer
                -fsanitize=address,undefined
        )
        target_link_options(
            ramfs_seed_test
            PRIVATE
                -fsanitize=address,undefined
        )
        target_compile_options(
            ramfs_test
            PRIVATE
                -fno-omit-frame-pointer
                -fsanitize=address,undefined
        )
        target_link_options(
            ramfs_test
            PRIVATE
                -fsanitize=address,undefined
        )
        target_compile_options(
            ramfs_model_test
            PRIVATE
                -fno-omit-frame-pointer
                -fsanitize=address,undefined
        )
        target_link_options(
            ramfs_model_test
            PRIVATE
                -fsanitize=address,undefined
        )
        target_compile_options(
            vfs_test
            PRIVATE
                -fno-omit-frame-pointer
                -fsanitize=address,undefined
        )
        target_link_options(
            vfs_test
            PRIVATE
                -fsanitize=address,undefined
        )
        target_compile_options(
            vfs_model_test
            PRIVATE
                -fno-omit-frame-pointer
                -fsanitize=address,undefined
        )
        target_link_options(
            vfs_model_test
            PRIVATE
                -fsanitize=address,undefined
        )
        target_compile_options(
            vfs_service_test
            PRIVATE
                -fno-omit-frame-pointer
                -fsanitize=address,undefined
        )
        target_link_options(
            vfs_service_test
            PRIVATE
                -fsanitize=address,undefined
        )
        target_compile_options(
            tty_test
            PRIVATE
                -fno-omit-frame-pointer
                -fsanitize=address,undefined
        )
        target_link_options(
            tty_test
            PRIVATE
                -fsanitize=address,undefined
        )
        target_compile_options(
            tty_service_test
            PRIVATE
                -fno-omit-frame-pointer
                -fsanitize=address,undefined
        )
        target_link_options(
            tty_service_test
            PRIVATE
                -fsanitize=address,undefined
        )
        target_compile_options(
            tty_uart_test
            PRIVATE
                -fno-omit-frame-pointer
                -fsanitize=address,undefined
        )
        target_link_options(
            tty_uart_test
            PRIVATE
                -fsanitize=address,undefined
        )
        target_compile_options(
            tty_model_test
            PRIVATE
                -fno-omit-frame-pointer
                -fsanitize=address,undefined
        )
        target_link_options(
            tty_model_test
            PRIVATE
                -fsanitize=address,undefined
        )
        target_compile_options(
            tty_service_control_test
            PRIVATE
                -fno-omit-frame-pointer
                -fsanitize=address,undefined
        )
        target_link_options(
            tty_service_control_test
            PRIVATE
                -fsanitize=address,undefined
        )
        target_compile_options(
            vm_handoff_syscall_test
            PRIVATE
                -fno-omit-frame-pointer
                -fsanitize=address,undefined
        )
        target_link_options(
            vm_handoff_syscall_test
            PRIVATE
                -fsanitize=address,undefined
        )
        target_compile_options(
            tty_interrupt_test
            PRIVATE
                -fno-omit-frame-pointer
                -fsanitize=address,undefined
        )
        target_link_options(
            tty_interrupt_test
            PRIVATE
                -fsanitize=address,undefined
        )
        target_compile_options(
            trap_route_test
            PRIVATE
                -fno-omit-frame-pointer
                -fsanitize=address,undefined
        )
        target_link_options(
            trap_route_test
            PRIVATE
                -fsanitize=address,undefined
        )
        target_compile_options(
            tty_control_syscall_test
            PRIVATE
                -fno-omit-frame-pointer
                -fsanitize=address,undefined
        )
        target_link_options(
            tty_control_syscall_test
            PRIVATE
                -fsanitize=address,undefined
        )
        target_compile_options(
            tty_fault_test
            PRIVATE
                -fno-omit-frame-pointer
                -fsanitize=address,undefined
        )
        target_link_options(
            tty_fault_test
            PRIVATE
                -fsanitize=address,undefined
        )
    endif()

    foreach(
        kernel_consumer
        IN ITEMS
            bootstrap_control_test
            bootstrap_test
            elf_spawn_fixture_test
            elf_spawn_model_test
            endpoint_test
            fdt_test
            frame_allocator_test
            frame_ownership_test
            ipc_model_test
            kernel_objects_test
            pm_test
            scheduler_test
            sv39_test
            trap_route_test
            tty_control_syscall_test
            tty_fault_test
            tty_interrupt_test
            tty_test
            vm_bootstrap_test
            vm_core_test
            vm_handoff_syscall_test
    )
        foreach(
            kernel_directory
            IN ITEMS
                boot
                diagnostic
                endpoint
                execution
                grant
                ipc
                memory
                objects
                platform
                pm
                scheduler
                trap
                tty
                vm
                tests/boot
                tests/endpoint
                tests/execution
                tests/grant
                tests/ipc
                tests/memory
                tests/objects
                tests/pm
                tests/ramfs
                tests/runtime
                tests/scheduler
                tests/spawn
                tests/trap
                tests/tty
                tests/vfs
                tests/vm
        )
            target_compile_options(
                "${kernel_consumer}"
                PRIVATE
                    "$<$<COMPILE_LANGUAGE:C>:-fmacro-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/kernel/${kernel_directory}/=${CMAKE_CURRENT_SOURCE_DIR}/kernel/>"
            )
        endforeach()
    endforeach()

    add_test(NAME fdt-parser COMMAND fdt_test)
    add_test(NAME frame-allocator COMMAND frame_allocator_test)
    add_test(NAME sv39-encoding COMMAND sv39_test)
    add_test(NAME kernel-objects COMMAND kernel_objects_test)
    add_test(NAME frame-ownership COMMAND frame_ownership_test)
    add_test(NAME scheduler-core COMMAND scheduler_test)
    add_test(NAME endpoint-profile COMMAND endpoint_test)
    add_test(NAME ipc-model COMMAND ipc_model_test)
    add_test(NAME user-runtime COMMAND runtime_test)
    add_test(NAME application-runtime COMMAND application_runtime_test)
    add_test(NAME elf-spawn-fixture COMMAND elf_spawn_fixture_test)
    add_test(NAME bootstrap-manifest COMMAND bootstrap_test)
    add_test(NAME bootstrap-control COMMAND bootstrap_control_test)
    add_test(NAME vm-bootstrap COMMAND vm_bootstrap_test)
    add_test(NAME pm-lifecycle COMMAND pm_test)
    add_test(NAME pm-coordinator COMMAND pm_coordinator_test)
    add_test(NAME spawn-abi COMMAND spawn_abi_test)
    add_test(NAME vm-core COMMAND vm_core_test)
    add_test(NAME elf-spawn-model COMMAND elf_spawn_model_test)
    set_tests_properties(elf-spawn-model PROPERTIES LABELS slow)
    add_test(
        NAME ramfs-seed
        COMMAND ramfs_seed_test "${MICROS_RAMFS_SEED_IMAGE}"
    )
    add_test(
        NAME ramfs-core
        COMMAND ramfs_test "${MICROS_RAMFS_SEED_IMAGE}"
    )
    add_test(NAME ramfs-model COMMAND ramfs_model_test)
    add_test(NAME vfs-core COMMAND vfs_test)
    add_test(NAME vfs-model COMMAND vfs_model_test)
    add_test(NAME vfs-service-core COMMAND vfs_service_test)
    add_test(NAME tty-core COMMAND tty_test)
    add_test(NAME tty-service-core COMMAND tty_service_test)
    add_test(NAME tty-uart COMMAND tty_uart_test)
    add_test(NAME tty-model COMMAND tty_model_test)
    add_test(NAME tty-service-control COMMAND tty_service_control_test)
    add_test(
        NAME vm-handoff-syscall
        COMMAND vm_handoff_syscall_test
    )
    add_test(NAME tty-interrupt COMMAND tty_interrupt_test)
    add_test(NAME trap-route COMMAND trap_route_test)
    add_test(
        NAME tty-control-syscall
        COMMAND tty_control_syscall_test
    )
    add_test(NAME tty-owner-fault COMMAND tty_fault_test)
    set_tests_properties(ipc-model PROPERTIES LABELS slow)
    set_tests_properties(tty-model PROPERTIES LABELS slow)
    set_tests_properties(ramfs-model PROPERTIES LABELS slow)
    set_tests_properties(vfs-model PROPERTIES LABELS slow)

    add_custom_target(
        test-unit-fast
        COMMAND
            "${CMAKE_CTEST_COMMAND}"
            --test-dir
            "${CMAKE_BINARY_DIR}"
            --output-on-failure
            -LE
            slow
        COMMAND
            "${MICROS_PYTHON}"
            -B
            -m
            unittest
            discover
            -s
            tests/host
            -p
            "test_*.py"
        DEPENDS
            fdt_test
            frame_allocator_test
            sv39_test
            kernel_objects_test
            frame_ownership_test
            scheduler_test
            endpoint_test
            runtime_test
            application_runtime_test
            elf_spawn_fixture_test
            bootstrap_test
            bootstrap_control_test
            vm_bootstrap_test
            pm_test
            pm_coordinator_test
            spawn_abi_test
            vm_core_test
            ramfs_seed_test
            ramfs_test
            vfs_test
            vfs_service_test
            tty_test
            tty_service_test
            tty_uart_test
            tty_service_control_test
            vm_handoff_syscall_test
            tty_interrupt_test
            trap_route_test
            tty_control_syscall_test
            tty_fault_test
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )

    add_custom_target(
        test-ipc-model
        COMMAND
            "${CMAKE_CTEST_COMMAND}"
            --test-dir
            "${CMAKE_BINARY_DIR}"
            --output-on-failure
            -R
            ipc-model
        DEPENDS ipc_model_test
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )

    add_custom_target(
        test-tty-model
        COMMAND
            "${CMAKE_CTEST_COMMAND}"
            --test-dir
            "${CMAKE_BINARY_DIR}"
            --output-on-failure
            -R
            tty-model
        DEPENDS tty_model_test
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )

    add_custom_target(
        test-ramfs-model
        COMMAND
            "${CMAKE_CTEST_COMMAND}"
            --test-dir
            "${CMAKE_BINARY_DIR}"
            --output-on-failure
            -R
            ramfs-model
        DEPENDS ramfs_model_test
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )

    add_custom_target(
        test-vfs-model
        COMMAND
            "${CMAKE_CTEST_COMMAND}"
            --test-dir
            "${CMAKE_BINARY_DIR}"
            --output-on-failure
            -R
            vfs-model
        DEPENDS vfs_model_test
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )

    add_custom_target(
        test-elf-spawn-model
        COMMAND
            "${CMAKE_CTEST_COMMAND}"
            --test-dir
            "${CMAKE_BINARY_DIR}"
            --output-on-failure
            -R
            elf-spawn-model
        DEPENDS elf_spawn_model_test
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )

    add_custom_target(
        test-unit
        COMMAND
            "${CMAKE_CTEST_COMMAND}"
            --test-dir
            "${CMAKE_BINARY_DIR}"
            --output-on-failure
        COMMAND
            "${MICROS_PYTHON}"
            -B
            -m
            unittest
            discover
            -s
            tests/host
            -p
            "test_*.py"
        DEPENDS
            fdt_test
            frame_allocator_test
            sv39_test
            kernel_objects_test
            frame_ownership_test
            scheduler_test
            endpoint_test
            ipc_model_test
            runtime_test
            application_runtime_test
            elf_spawn_fixture_test
            bootstrap_test
            bootstrap_control_test
            vm_bootstrap_test
            pm_test
            pm_coordinator_test
            elf_spawn_model_test
            spawn_abi_test
            vm_core_test
            ramfs_seed_test
            ramfs_test
            ramfs_model_test
            vfs_test
            vfs_model_test
            vfs_service_test
            tty_test
            tty_service_test
            tty_uart_test
            tty_model_test
            tty_service_control_test
            vm_handoff_syscall_test
            tty_interrupt_test
            trap_route_test
            tty_control_syscall_test
            tty_fault_test
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )
    return()
endif()

if(NOT CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "micros target builds require the RISC-V toolchain file")
endif()

enable_language(ASM)

if(NOT MICROS_LD_LLD)
    message(FATAL_ERROR "MICROS_LD_LLD must name the target LLD executable")
endif()

find_program(MICROS_QEMU NAMES qemu-system-riscv64 REQUIRED)

set(MICROS_LINKER_SCRIPT
    "${CMAKE_CURRENT_SOURCE_DIR}/arch/riscv64/kernel.ld"
)
set(MICROS_USER_LINKER_SCRIPT
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/runtime/user.ld"
)
set(MICROS_APPLICATION_LINKER_SCRIPT
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/runtime/application.ld"
)

if(MICROS_BUILD_PM_SERVICE_TEST OR MICROS_BUILD_APPLICATION_IMAGES OR MICROS_BUILD_ELF_SPAWN_TEST)
    function(micros_add_application target output_name)
        add_executable(
            "${target}"
            lib/runtime/application.c
            lib/runtime/application_stack.c
            lib/runtime/application_start.S
            lib/runtime/memory.c
            lib/runtime/memory_symbols.c
            lib/runtime/raw_syscall.S
            lib/runtime/runtime.c
            ${ARGN}
        )
        set_target_properties(
            "${target}"
            PROPERTIES
                OUTPUT_NAME "${output_name}"
                SUFFIX ".elf"
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/applications"
                LINK_DEPENDS
                    "${MICROS_APPLICATION_LINKER_SCRIPT};${MICROS_APPLICATION_ELF_CHECKER_SOURCES}"
        )
        target_include_directories(
            "${target}"
            PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}"
                "${CMAKE_CURRENT_SOURCE_DIR}/include"
        )
        target_compile_features("${target}" PRIVATE c_std_17)
        target_compile_options(
            "${target}"
            PRIVATE
                -march=rv64imac_zicsr_zifencei
                -mabi=lp64
                -mcmodel=medany
                -msmall-data-limit=0
                -mno-save-restore
                -ffunction-sections
                -fdata-sections
                -g0
                -Os
                "$<$<COMPILE_LANGUAGE:C>:-std=c17;-ffreestanding;-fno-builtin;-fno-common;-fno-pic;-fno-pie;-fno-stack-protector;-fno-unwind-tables;-fno-asynchronous-unwind-tables>"
                "$<$<COMPILE_LANGUAGE:C>:-Wall;-Wextra;-Werror;-Wpedantic;-Wmissing-prototypes;-Wstrict-prototypes>"
        )
        target_link_options(
            "${target}"
            PRIVATE
                -march=rv64imac_zicsr_zifencei
                -mabi=lp64
                -mcmodel=medany
                -nostdlib
                -static
                "--ld-path=${MICROS_LD_LLD}"
                "-Wl,-T,${MICROS_APPLICATION_LINKER_SCRIPT}"
                -Wl,--build-id=none
                -Wl,--fatal-warnings
                -Wl,--gc-sections
                -Wl,--no-relax
                -Wl,-z,max-page-size=4096
                -Wl,-z,common-page-size=4096
        )
        add_custom_command(
            TARGET "${target}"
            POST_BUILD
            COMMAND
                "${MICROS_PYTHON}" -B
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_application_elf.py"
                --elf "$<TARGET_FILE:${target}>"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            VERBATIM
        )
    endfunction()
    micros_add_application(
        micros_application_runtime_probe
        micros-application-runtime-probe
        tests/qemu/application_runtime_probe.c
    )
    if(MICROS_BUILD_APPLICATION_IMAGES)
        micros_add_application(
            micros_application_minimal_probe
            micros-application-minimal-probe
            tests/qemu/application_minimal_probe.c
        )
    endif()
    if(MICROS_BUILD_ELF_SPAWN_TEST OR MICROS_BUILD_APPLICATION_IMAGES)
        micros_add_application(
            micros_elf_spawn_init
            micros-elf-spawn-init
            tests/qemu/elf_spawn_init.c
            tests/qemu/elf_spawn_client.c
            tests/qemu/elf_spawn_client_vfs.c
            tests/qemu/elf_spawn_request.c
            tests/qemu/elf_spawn_report.S
        )
        micros_add_application(
            micros_elf_spawn_child
            micros-elf-spawn-child
            tests/qemu/elf_spawn_child.c
            tests/qemu/elf_spawn_client.c
            tests/qemu/elf_spawn_client_vfs.c
            tests/qemu/elf_spawn_request.c
            tests/qemu/elf_spawn_report.S
        )
        target_compile_definitions(micros_elf_spawn_init PRIVATE MICROS_BUILD_ELF_SPAWN_TEST=1)
        target_compile_definitions(micros_elf_spawn_child PRIVATE MICROS_BUILD_ELF_SPAWN_TEST=1)
        set(
            MICROS_ELF_SPAWN_SEED_IMAGE
            "${CMAKE_BINARY_DIR}/applications/elf-spawn-seed.bin"
        )
        add_custom_command(
            OUTPUT "${MICROS_ELF_SPAWN_SEED_IMAGE}"
            COMMAND
                "${MICROS_PYTHON}" -B
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_elf_spawn_seed.py"
                --init-elf "$<TARGET_FILE:micros_elf_spawn_init>"
                --child-elf "$<TARGET_FILE:micros_elf_spawn_child>"
                --output "${MICROS_ELF_SPAWN_SEED_IMAGE}"
            DEPENDS
                micros_elf_spawn_init
                micros_elf_spawn_child
                ${MICROS_APPLICATION_ELF_CHECKER_SOURCES}
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_elf_spawn_seed.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_ramfs_seed.py"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            VERBATIM
        )
    endif()
    set(
        MICROS_APPLICATION_RUNTIME_FIXTURE_SOURCE
        "${CMAKE_BINARY_DIR}/generated/application_runtime_fixture.c"
    )
    add_custom_command(
        OUTPUT "${MICROS_APPLICATION_RUNTIME_FIXTURE_SOURCE}"
        COMMAND
            "${MICROS_PYTHON}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_application_runtime_fixture.py"
            --elf "$<TARGET_FILE:micros_application_runtime_probe>"
            --output "${MICROS_APPLICATION_RUNTIME_FIXTURE_SOURCE}"
        DEPENDS
            micros_application_runtime_probe
            ${MICROS_APPLICATION_ELF_CHECKER_SOURCES}
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_application_runtime_fixture.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_user_runtime_fixture.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/qemu/application_runtime_fixture.h"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        VERBATIM
    )
    set_source_files_properties(
        "${MICROS_APPLICATION_RUNTIME_FIXTURE_SOURCE}"
        PROPERTIES GENERATED TRUE
    )
    if(MICROS_BUILD_APPLICATION_IMAGES)
        set(
            MICROS_APPLICATION_RUNTIME_ARTIFACT
            "${CMAKE_BINARY_DIR}/applications/micros-application-runtime-probe.runtime.elf"
        )
        set(
            MICROS_APPLICATION_SEED_IMAGE
            "${CMAKE_BINARY_DIR}/applications/application-seed.bin"
        )
        add_custom_command(
            OUTPUT "${MICROS_APPLICATION_RUNTIME_ARTIFACT}"
            COMMAND
                "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:micros_application_runtime_probe>"
                "${MICROS_APPLICATION_RUNTIME_ARTIFACT}"
            COMMAND
                "${MICROS_PYTHON}" -B
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_application_elf.py"
                --elf "$<TARGET_FILE:micros_application_runtime_probe>"
                --runtime-elf "${MICROS_APPLICATION_RUNTIME_ARTIFACT}"
            DEPENDS
                micros_application_runtime_probe
                ${MICROS_APPLICATION_ELF_CHECKER_SOURCES}
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            VERBATIM
        )
        add_custom_command(
            OUTPUT "${MICROS_APPLICATION_SEED_IMAGE}"
            COMMAND
                "${MICROS_PYTHON}" -B
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_application_seed.py"
                --elf "$<TARGET_FILE:micros_application_runtime_probe>"
                --runtime-elf "${MICROS_APPLICATION_RUNTIME_ARTIFACT}"
                --output "${MICROS_APPLICATION_SEED_IMAGE}"
            DEPENDS
                micros_application_runtime_probe
                "${MICROS_APPLICATION_RUNTIME_ARTIFACT}"
                ${MICROS_APPLICATION_ELF_CHECKER_SOURCES}
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_application_seed.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_ramfs_seed.py"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            VERBATIM
        )
        add_custom_target(
            build-application-images
            DEPENDS
                micros_application_runtime_probe
                micros_application_minimal_probe
                micros_elf_spawn_init
                micros_elf_spawn_child
                "${MICROS_ELF_SPAWN_SEED_IMAGE}"
                "${MICROS_APPLICATION_RUNTIME_ARTIFACT}"
                "${MICROS_APPLICATION_SEED_IMAGE}"
        )
    endif()
endif()

if(MICROS_BUILD_USER_RUNTIME_TEST)
    add_executable(
        micros_user_runtime_service
        lib/runtime/memory.c
        lib/runtime/memory_symbols.c
        lib/runtime/raw_syscall.S
        lib/runtime/runtime.c
        lib/runtime/start.S
        tests/qemu/user_runtime_probe.S
        ${MICROS_USER_RUNTIME_SERVICE_TEST_SOURCES}
    )
    set_target_properties(
        micros_user_runtime_service
        PROPERTIES
            OUTPUT_NAME micros-user-runtime-service
            SUFFIX ".elf"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/user-runtime"
            LINK_DEPENDS
                "${MICROS_USER_LINKER_SCRIPT};${MICROS_USER_ELF_CHECKER_SOURCES}"
    )
    target_include_directories(
        micros_user_runtime_service
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_compile_features(
        micros_user_runtime_service
        PRIVATE
            c_std_17
    )
    target_compile_options(
        micros_user_runtime_service
        PRIVATE
            -march=rv64imac_zicsr_zifencei
            -mabi=lp64
            -mcmodel=medany
            -msmall-data-limit=0
            -mno-save-restore
            -ffunction-sections
            -fdata-sections
            -g0
            "$<$<COMPILE_LANGUAGE:C>:-std=c17;-ffreestanding;-fno-builtin;-fno-common;-fno-pic;-fno-pie;-fno-stack-protector;-fno-unwind-tables;-fno-asynchronous-unwind-tables>"
            "$<$<COMPILE_LANGUAGE:C>:-Wall;-Wextra;-Werror;-Wpedantic;-Wmissing-prototypes;-Wstrict-prototypes>"
    )
    target_link_options(
        micros_user_runtime_service
        PRIVATE
            -march=rv64imac_zicsr_zifencei
            -mabi=lp64
            -mcmodel=medany
            -nostdlib
            -static
            "--ld-path=${MICROS_LD_LLD}"
            "-Wl,-T,${MICROS_USER_LINKER_SCRIPT}"
            -Wl,--build-id=none
            -Wl,--fatal-warnings
            -Wl,--gc-sections
            -Wl,--no-relax
            -Wl,-z,max-page-size=4096
            -Wl,-z,common-page-size=4096
            "-Wl,-Map,${CMAKE_BINARY_DIR}/user-runtime/micros-user-runtime-service.map"
    )
    add_custom_command(
        TARGET micros_user_runtime_service
        POST_BUILD
        COMMAND
            "${MICROS_PYTHON}"
            -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_user_elf.py"
            --elf
            "$<TARGET_FILE:micros_user_runtime_service>"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        VERBATIM
    )
    set(
        MICROS_USER_RUNTIME_FIXTURE_SOURCE
        "${CMAKE_BINARY_DIR}/generated/user_runtime_fixture.c"
    )
    add_custom_command(
        OUTPUT "${MICROS_USER_RUNTIME_FIXTURE_SOURCE}"
        COMMAND
            "${MICROS_PYTHON}"
            -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_user_runtime_fixture.py"
            --elf
            "$<TARGET_FILE:micros_user_runtime_service>"
            --output
            "${MICROS_USER_RUNTIME_FIXTURE_SOURCE}"
        DEPENDS
            micros_user_runtime_service
            ${MICROS_USER_ELF_CHECKER_SOURCES}
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_user_runtime_fixture.py"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        VERBATIM
    )
    set_source_files_properties(
        "${MICROS_USER_RUNTIME_FIXTURE_SOURCE}"
        PROPERTIES GENERATED TRUE
    )
endif()

if(
    MICROS_BUILD_BOOTSTRAP_LAUNCHER_TEST
    OR MICROS_BUILD_BOOTSTRAP_READY_TIMEOUT_TEST
    OR MICROS_BUILD_BOOTSTRAP_MANIFEST_PANIC_TEST
    OR MICROS_BUILD_VM_HANDOFF_TEST
    OR MICROS_BUILD_PM_SERVICE_TEST
    OR MICROS_BUILD_VM_SCRATCH_PREEMPTION_TEST
    OR MICROS_BUILD_TTY_SERVICE_TEST
    OR MICROS_BUILD_TTY_SERVICE_IMAGE
    OR MICROS_BUILD_RAMFS_SERVICE_IMAGE
    OR MICROS_BUILD_VFS_SERVICE_IMAGE
    OR MICROS_BUILD_RAMFS_SERVICE_TEST
    OR MICROS_BUILD_VFS_SERVICE_TEST
    OR MICROS_BUILD_ELF_SPAWN_TEST
    OR MICROS_BUILD_VM_READY_EARLY_TEST
    OR MICROS_BUILD_VM_SELF_FAULT_TEST
    OR MICROS_BUILD_VM_SELF_FAULT_SEALED_TEST
)
    function(micros_add_bootstrap_user target output_name)
        add_executable(
            "${target}"
            lib/runtime/memory.c
            lib/runtime/memory_symbols.c
            lib/runtime/raw_syscall.S
            lib/runtime/runtime.c
            lib/runtime/start.S
            ${ARGN}
        )
        set_target_properties(
            "${target}"
            PROPERTIES
                OUTPUT_NAME "${output_name}"
                SUFFIX ".elf"
                RUNTIME_OUTPUT_DIRECTORY
                    "${CMAKE_BINARY_DIR}/bootstrap"
                LINK_DEPENDS
                    "${MICROS_USER_LINKER_SCRIPT};${MICROS_USER_ELF_CHECKER_SOURCES}"
        )
        target_include_directories(
            "${target}"
            PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}"
                "${CMAKE_CURRENT_SOURCE_DIR}/include"
        )
        target_compile_features("${target}" PRIVATE c_std_17)
        target_compile_options(
            "${target}"
            PRIVATE
                -march=rv64imac_zicsr_zifencei
                -mabi=lp64
                -mcmodel=medany
                -msmall-data-limit=0
                -mno-save-restore
                -ffunction-sections
                -fdata-sections
                -g0
                "$<$<COMPILE_LANGUAGE:C>:-std=c17;-ffreestanding;-fno-builtin;-fno-common;-fno-pic;-fno-pie;-fno-stack-protector;-fno-unwind-tables;-fno-asynchronous-unwind-tables>"
                "$<$<COMPILE_LANGUAGE:C>:-Wall;-Wextra;-Werror;-Wpedantic;-Wmissing-prototypes;-Wstrict-prototypes>"
        )
        target_link_options(
            "${target}"
            PRIVATE
                -march=rv64imac_zicsr_zifencei
                -mabi=lp64
                -mcmodel=medany
                -nostdlib
                -static
                "--ld-path=${MICROS_LD_LLD}"
                "-Wl,-T,${MICROS_USER_LINKER_SCRIPT}"
                -Wl,--build-id=none
                -Wl,--fatal-warnings
                -Wl,--gc-sections
                -Wl,--no-relax
                -Wl,-z,max-page-size=4096
                -Wl,-z,common-page-size=4096
                "-Wl,-Map,${CMAKE_BINARY_DIR}/bootstrap/${output_name}.map"
        )
        set(user_elf_budget "")
        if("${target}" STREQUAL "micros_pm_service")
            set(
                user_elf_budget
                --resident-page-limit
                32
                --stack-page-count
                1
            )
        endif()
        add_custom_command(
            TARGET "${target}"
            POST_BUILD
            COMMAND
                "${MICROS_PYTHON}"
                -B
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_user_elf.py"
                --elf
                "$<TARGET_FILE:${target}>"
                ${user_elf_budget}
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            VERBATIM
        )
    endfunction()

    if(
        MICROS_BUILD_TTY_SERVICE_IMAGE
        OR MICROS_BUILD_TTY_SERVICE_TEST
        OR MICROS_BUILD_RAMFS_SERVICE_TEST
        OR MICROS_BUILD_VFS_SERVICE_TEST
        OR MICROS_BUILD_ELF_SPAWN_TEST
    )
        micros_add_bootstrap_user(
            micros_tty_service
            micros-tty-service
            servers/tty/tty_control.c
            servers/tty/tty_core.c
            servers/tty/tty_service.c
            ${MICROS_TTY_SERVICE_CORE_SOURCES}
            servers/tty/tty_uart.c
        )
        if(MICROS_BUILD_TTY_SERVICE_TEST)
            target_sources(
                micros_tty_service
                PRIVATE
                    tests/qemu/tty_service_claim_wait.S
            )
            target_compile_definitions(
                micros_tty_service
                PRIVATE
                    MICROS_BUILD_TTY_SERVICE_TEST=1
                    MICROS_TTY_CONFIGURED_VFS_SERVICE_ID=5
            )
        endif()
    endif()

    if(
        MICROS_BUILD_RAMFS_SERVICE_IMAGE
        OR MICROS_BUILD_RAMFS_SERVICE_TEST
        OR MICROS_BUILD_VFS_SERVICE_TEST
        OR MICROS_BUILD_ELF_SPAWN_TEST
    )
        set(MICROS_RAMFS_SERVICE_SEED_IMAGE "${MICROS_RAMFS_SEED_IMAGE}")
        if(MICROS_BUILD_ELF_SPAWN_TEST)
            set(MICROS_RAMFS_SERVICE_SEED_IMAGE "${MICROS_ELF_SPAWN_SEED_IMAGE}")
        endif()
        set(
            MICROS_RAMFS_EMBEDDED_SEED_SOURCE
            "${CMAKE_BINARY_DIR}/generated/ramfs_embedded_seed.c"
        )
        add_custom_command(
            OUTPUT "${MICROS_RAMFS_EMBEDDED_SEED_SOURCE}"
            COMMAND
                "${MICROS_PYTHON}"
                -B
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_ramfs_seed.py"
                --input
                "${MICROS_RAMFS_SERVICE_SEED_IMAGE}"
                --output
                "${MICROS_RAMFS_EMBEDDED_SEED_SOURCE}"
            DEPENDS
                "${MICROS_RAMFS_SERVICE_SEED_IMAGE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_ramfs_seed.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/servers/ramfs/ramfs_embedded_seed.h"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            VERBATIM
        )
        set_source_files_properties(
            "${MICROS_RAMFS_EMBEDDED_SEED_SOURCE}"
            PROPERTIES GENERATED TRUE
        )
        micros_add_bootstrap_user(
            micros_ramfs_service
            micros-ramfs-service
            "${MICROS_RAMFS_EMBEDDED_SEED_SOURCE}"
            ${MICROS_RAMFS_CORE_SOURCES}
            servers/ramfs/ramfs_seed.c
            servers/ramfs/ramfs_service.c
        )
        add_custom_command(
            TARGET micros_ramfs_service
            POST_BUILD
            COMMAND
                "${MICROS_PYTHON}"
                -B
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_user_elf.py"
                --elf
                "$<TARGET_FILE:micros_ramfs_service>"
                --resident-page-limit
                192
                --stack-page-count
                1
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            VERBATIM
        )
    endif()

    if(
        MICROS_BUILD_VFS_SERVICE_IMAGE
        OR MICROS_BUILD_VFS_SERVICE_TEST
        OR MICROS_BUILD_ELF_SPAWN_TEST
    )
        micros_add_bootstrap_user(
            micros_vfs_service
            micros-vfs-service
            ${MICROS_SPAWN_REQUEST_SOURCES}
            ${MICROS_VFS_PORTABLE_SOURCES}
            servers/vfs/vfs_service_core.c
            servers/vfs/vfs_service.c
        )
        add_custom_command(
            TARGET micros_vfs_service
            POST_BUILD
            COMMAND
                "${MICROS_PYTHON}"
                -B
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_user_elf.py"
                --elf
                "$<TARGET_FILE:micros_vfs_service>"
                --resident-page-limit
                80
                --stack-page-count
                1
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            VERBATIM
        )
        if(MICROS_BUILD_VFS_SERVICE_TEST)
            target_compile_definitions(
                micros_vfs_service
                PRIVATE
                    MICROS_BUILD_VFS_SERVICE_TEST=1
            )
        endif()
        if(MICROS_BUILD_ELF_SPAWN_TEST)
            target_compile_definitions(micros_vfs_service PRIVATE MICROS_BUILD_ELF_SPAWN_TEST=1)
        endif()
    endif()

    micros_add_bootstrap_user(
        micros_bootstrap_launcher
        micros-bootstrap-launcher
        ${MICROS_BOOTSTRAP_LAUNCHER_SOURCES}
    )
    if(
        MICROS_BUILD_VM_HANDOFF_TEST
        OR MICROS_BUILD_PM_SERVICE_TEST
        OR MICROS_BUILD_VM_SCRATCH_PREEMPTION_TEST
        OR MICROS_BUILD_TTY_SERVICE_TEST
        OR MICROS_BUILD_RAMFS_SERVICE_TEST
        OR MICROS_BUILD_VFS_SERVICE_TEST
        OR MICROS_BUILD_ELF_SPAWN_TEST
        OR MICROS_BUILD_VM_READY_EARLY_TEST
        OR MICROS_BUILD_VM_SELF_FAULT_TEST
        OR MICROS_BUILD_VM_SELF_FAULT_SEALED_TEST
    )
        micros_add_bootstrap_user(
            micros_vm_server
            micros-vm-server
            ${MICROS_SPAWN_REQUEST_SOURCES}
            ${MICROS_VM_BOOT_VALIDATION_SOURCES}
            ${MICROS_VM_IMAGE_SOURCES}
            ${MICROS_VM_CORE_SOURCES}
            ${MICROS_VM_SERVICE_CORE_SOURCES}
            ${MICROS_VM_SERVICE_BACKEND_SOURCES}
            tests/qemu/vm_self_fault.S
            ${MICROS_VM_SERVICE_QEMU_SOURCES}
        )
        if(MICROS_BUILD_VM_SCRATCH_PREEMPTION_TEST)
            target_sources(
                micros_vm_server
                PRIVATE
                    tests/qemu/vm_scratch_preemption_vm.S
            )
        endif()
        add_custom_command(
            TARGET micros_vm_server
            POST_BUILD
            COMMAND
                "${MICROS_PYTHON}"
                -B
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_user_elf.py"
                --elf
                "$<TARGET_FILE:micros_vm_server>"
                --resident-page-limit
                160
                --stack-page-count
                1
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            VERBATIM
        )
        if(MICROS_BUILD_VM_SCRATCH_PREEMPTION_TEST)
            target_compile_definitions(
                micros_vm_server
                PRIVATE
                    MICROS_VM_SCRATCH_PREEMPTION_TEST=1
            )
        elseif(MICROS_BUILD_ELF_SPAWN_TEST)
            target_compile_definitions(
                micros_vm_server
                PRIVATE
                    MICROS_BUILD_ELF_SPAWN_TEST=1
            )
        elseif(MICROS_BUILD_VFS_SERVICE_TEST)
            target_compile_definitions(
                micros_vm_server
                PRIVATE
                    MICROS_VFS_SERVICE_TEST=1
            )
        elseif(MICROS_BUILD_RAMFS_SERVICE_TEST)
            target_compile_definitions(
                micros_vm_server
                PRIVATE
                    MICROS_RAMFS_SERVICE_TEST=1
            )
        elseif(MICROS_BUILD_TTY_SERVICE_TEST)
            target_compile_definitions(
                micros_vm_server
                PRIVATE
                    MICROS_TTY_HANDOFF_TEST=1
            )
        elseif(MICROS_BUILD_PM_SERVICE_TEST)
            target_sources(
                micros_vm_server
                PRIVATE
                    "${MICROS_APPLICATION_RUNTIME_FIXTURE_SOURCE}"
                    tests/qemu/application_runtime_vm.c
            )
            target_compile_definitions(
                micros_vm_server
                PRIVATE
                    MICROS_PM_SERVICE_TEST=1
            )
        elseif(MICROS_BUILD_VM_READY_EARLY_TEST)
            target_compile_definitions(
                micros_vm_server
                PRIVATE
                    MICROS_VM_READY_EARLY=1
            )
        elseif(MICROS_BUILD_VM_SELF_FAULT_TEST)
            target_compile_definitions(
                micros_vm_server
                PRIVATE
                    MICROS_VM_SELF_FAULT_RUNNING=1
            )
        elseif(MICROS_BUILD_VM_SELF_FAULT_SEALED_TEST)
            target_compile_definitions(
                micros_vm_server
                PRIVATE
                    MICROS_VM_SELF_FAULT_SEALED=1
            )
        endif()
        if(MICROS_BUILD_VM_SCRATCH_PREEMPTION_TEST)
            micros_add_bootstrap_user(
                micros_pm_service
                micros-pm-service
                servers/pm/pm_control.c
                ${MICROS_PM_CORE_SOURCES}
                ${MICROS_PM_SERVICE_SOURCES}
                ${MICROS_PM_COORDINATOR_SOURCES}
                ${MICROS_PM_RUNTIME_BACKEND_SOURCES}
                ${MICROS_SPAWN_REQUEST_SOURCES}
                tests/qemu/vm_scratch_preemption_peer.S
            )
            target_compile_definitions(
                micros_pm_service
                PRIVATE
                    MICROS_VM_SCRATCH_PREEMPTION_TEST=1
            )
            micros_add_bootstrap_user(
                micros_vm_scratch_preemption_driver
                micros-vm-scratch-preemption-driver
                tests/qemu/vm_scratch_preemption_driver.c
            )
            set(
                MICROS_VM_SCRATCH_PREEMPTION_FIXTURE_SOURCE
                "${CMAKE_BINARY_DIR}/generated/vm_scratch_preemption_fixture.c"
            )
            add_custom_command(
                OUTPUT
                    "${MICROS_VM_SCRATCH_PREEMPTION_FIXTURE_SOURCE}"
                COMMAND
                    "${MICROS_PYTHON}"
                    -B
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_vm_scratch_preemption_fixture.py"
                    --launcher-elf
                    "$<TARGET_FILE:micros_bootstrap_launcher>"
                    --vm-elf
                    "$<TARGET_FILE:micros_vm_server>"
                    --pm-elf
                    "$<TARGET_FILE:micros_pm_service>"
                    --driver-elf
                    "$<TARGET_FILE:micros_vm_scratch_preemption_driver>"
                    --output
                    "${MICROS_VM_SCRATCH_PREEMPTION_FIXTURE_SOURCE}"
                DEPENDS
                    micros_bootstrap_launcher
                    micros_vm_server
                    micros_pm_service
                    micros_vm_scratch_preemption_driver
                    ${MICROS_USER_ELF_CHECKER_SOURCES}
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_bootstrap_fixture.py"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_vm_scratch_preemption_fixture.py"
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                VERBATIM
            )
            set_source_files_properties(
                "${MICROS_VM_SCRATCH_PREEMPTION_FIXTURE_SOURCE}"
                PROPERTIES GENERATED TRUE
            )
        elseif(MICROS_BUILD_PM_SERVICE_TEST)
            micros_add_bootstrap_user(
                micros_pm_service
                micros-pm-service
                servers/pm/pm_control.c
                ${MICROS_PM_CORE_SOURCES}
                ${MICROS_PM_SERVICE_SOURCES}
                ${MICROS_PM_COORDINATOR_SOURCES}
                ${MICROS_PM_RUNTIME_BACKEND_SOURCES}
                ${MICROS_SPAWN_REQUEST_SOURCES}
                tests/qemu/pm_service_report.S
            )
            target_compile_definitions(
                micros_pm_service
                PRIVATE
                    MICROS_PM_SERVICE_TEST=1
            )
            micros_add_bootstrap_user(
                micros_pm_service_probe
                micros-pm-service-probe
                tests/qemu/pm_service_probe.c
            )
            set(
                MICROS_PM_SERVICE_FIXTURE_SOURCE
                "${CMAKE_BINARY_DIR}/generated/pm_service_fixture.c"
            )
            add_custom_command(
                OUTPUT "${MICROS_PM_SERVICE_FIXTURE_SOURCE}"
                COMMAND
                    "${MICROS_PYTHON}"
                    -B
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_pm_service_fixture.py"
                    --launcher-elf
                    "$<TARGET_FILE:micros_bootstrap_launcher>"
                    --vm-elf
                    "$<TARGET_FILE:micros_vm_server>"
                    --pm-elf
                    "$<TARGET_FILE:micros_pm_service>"
                    --probe-elf
                    "$<TARGET_FILE:micros_pm_service_probe>"
                    --output
                    "${MICROS_PM_SERVICE_FIXTURE_SOURCE}"
                DEPENDS
                    micros_bootstrap_launcher
                    micros_vm_server
                    micros_pm_service
                    micros_pm_service_probe
                    ${MICROS_USER_ELF_CHECKER_SOURCES}
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_bootstrap_fixture.py"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_pm_service_fixture.py"
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                VERBATIM
            )
            set_source_files_properties(
                "${MICROS_PM_SERVICE_FIXTURE_SOURCE}"
                PROPERTIES GENERATED TRUE
            )
        elseif(MICROS_BUILD_TTY_SERVICE_TEST)
            micros_add_bootstrap_user(
                micros_pm_service
                micros-pm-service
                servers/pm/pm_control.c
                ${MICROS_PM_CORE_SOURCES}
                ${MICROS_PM_SERVICE_SOURCES}
                ${MICROS_PM_COORDINATOR_SOURCES}
                ${MICROS_PM_RUNTIME_BACKEND_SOURCES}
                ${MICROS_SPAWN_REQUEST_SOURCES}
            )
            micros_add_bootstrap_user(
                micros_tty_service_vfs
                micros-tty-service-vfs
                tests/qemu/tty_service_report.S
                tests/qemu/tty_service_vfs.c
            )
            set(
                MICROS_TTY_SERVICE_FIXTURE_SOURCE
                "${CMAKE_BINARY_DIR}/generated/tty_service_fixture.c"
            )
            add_custom_command(
                OUTPUT "${MICROS_TTY_SERVICE_FIXTURE_SOURCE}"
                COMMAND
                    "${MICROS_PYTHON}"
                    -B
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_tty_service_fixture.py"
                    --launcher-elf
                    "$<TARGET_FILE:micros_bootstrap_launcher>"
                    --vm-elf
                    "$<TARGET_FILE:micros_vm_server>"
                    --pm-elf
                    "$<TARGET_FILE:micros_pm_service>"
                    --tty-elf
                    "$<TARGET_FILE:micros_tty_service>"
                    --vfs-elf
                    "$<TARGET_FILE:micros_tty_service_vfs>"
                    --output
                    "${MICROS_TTY_SERVICE_FIXTURE_SOURCE}"
                DEPENDS
                    micros_bootstrap_launcher
                    micros_vm_server
                    micros_pm_service
                    micros_tty_service
                    micros_tty_service_vfs
                    ${MICROS_USER_ELF_CHECKER_SOURCES}
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_bootstrap_fixture.py"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_tty_service_fixture.py"
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                VERBATIM
            )
            set_source_files_properties(
                "${MICROS_TTY_SERVICE_FIXTURE_SOURCE}"
                PROPERTIES GENERATED TRUE
            )
        elseif(MICROS_BUILD_ELF_SPAWN_TEST)
            micros_add_bootstrap_user(
                micros_pm_service
                micros-pm-service
                servers/pm/pm_control.c
                ${MICROS_PM_CORE_SOURCES}
                ${MICROS_PM_SERVICE_SOURCES}
                ${MICROS_PM_COORDINATOR_SOURCES}
                ${MICROS_PM_RUNTIME_BACKEND_SOURCES}
                ${MICROS_SPAWN_REQUEST_SOURCES}
                tests/qemu/elf_spawn_pm.c
                tests/qemu/elf_spawn_request.c
            )
            target_compile_definitions(
                micros_pm_service
                PRIVATE
                    MICROS_BUILD_ELF_SPAWN_TEST=1
            )
            set(
                MICROS_ELF_SPAWN_FIXTURE_SOURCE
                "${CMAKE_BINARY_DIR}/generated/elf_spawn_fixture.c"
            )
            add_custom_command(
                OUTPUT "${MICROS_ELF_SPAWN_FIXTURE_SOURCE}"
                COMMAND
                    "${MICROS_PYTHON}" -B
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_elf_spawn_fixture.py"
                    --launcher-elf "$<TARGET_FILE:micros_bootstrap_launcher>"
                    --vm-elf "$<TARGET_FILE:micros_vm_server>"
                    --pm-elf "$<TARGET_FILE:micros_pm_service>"
                    --tty-elf "$<TARGET_FILE:micros_tty_service>"
                    --ramfs-elf "$<TARGET_FILE:micros_ramfs_service>"
                    --vfs-elf "$<TARGET_FILE:micros_vfs_service>"
                    --init-elf "$<TARGET_FILE:micros_elf_spawn_init>"
                    --child-elf "$<TARGET_FILE:micros_elf_spawn_child>"
                    --output "${MICROS_ELF_SPAWN_FIXTURE_SOURCE}"
                DEPENDS
                    micros_bootstrap_launcher
                    micros_vm_server
                    micros_pm_service
                    micros_tty_service
                    micros_ramfs_service
                    micros_vfs_service
                    micros_elf_spawn_init
                    micros_elf_spawn_child
                    ${MICROS_APPLICATION_ELF_CHECKER_SOURCES}
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_elf_spawn_fixture.py"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_bootstrap_fixture.py"
                    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/elf_spawn_test_fixture.h"
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                VERBATIM
            )
            set_source_files_properties(
                "${MICROS_ELF_SPAWN_FIXTURE_SOURCE}"
                PROPERTIES GENERATED TRUE
            )
        elseif(MICROS_BUILD_VFS_SERVICE_TEST)
            micros_add_bootstrap_user(
                micros_pm_service
                micros-pm-service
                servers/pm/pm_control.c
                ${MICROS_PM_CORE_SOURCES}
                ${MICROS_PM_SERVICE_SOURCES}
                ${MICROS_PM_COORDINATOR_SOURCES}
                ${MICROS_PM_RUNTIME_BACKEND_SOURCES}
                ${MICROS_SPAWN_REQUEST_SOURCES}
            )
            micros_add_bootstrap_user(
                micros_vfs_service_probe
                micros-vfs-service-probe
                tests/qemu/vfs_service_report.S
                ${MICROS_VFS_PROBE_SOURCES}
            )
            set(
                MICROS_VFS_SERVICE_FIXTURE_SOURCE
                "${CMAKE_BINARY_DIR}/generated/vfs_service_fixture.c"
            )
            add_custom_command(
                OUTPUT "${MICROS_VFS_SERVICE_FIXTURE_SOURCE}"
                COMMAND
                    "${MICROS_PYTHON}"
                    -B
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_vfs_service_fixture.py"
                    --launcher-elf
                    "$<TARGET_FILE:micros_bootstrap_launcher>"
                    --vm-elf
                    "$<TARGET_FILE:micros_vm_server>"
                    --pm-elf
                    "$<TARGET_FILE:micros_pm_service>"
                    --tty-elf
                    "$<TARGET_FILE:micros_tty_service>"
                    --ramfs-elf
                    "$<TARGET_FILE:micros_ramfs_service>"
                    --vfs-elf
                    "$<TARGET_FILE:micros_vfs_service>"
                    --application-elf
                    "$<TARGET_FILE:micros_vfs_service_probe>"
                    --output
                    "${MICROS_VFS_SERVICE_FIXTURE_SOURCE}"
                DEPENDS
                    micros_bootstrap_launcher
                    micros_vm_server
                    micros_pm_service
                    micros_tty_service
                    micros_ramfs_service
                    micros_vfs_service
                    micros_vfs_service_probe
                    ${MICROS_USER_ELF_CHECKER_SOURCES}
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_bootstrap_fixture.py"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_vfs_service_fixture.py"
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                VERBATIM
            )
            set_source_files_properties(
                "${MICROS_VFS_SERVICE_FIXTURE_SOURCE}"
                PROPERTIES GENERATED TRUE
            )
        elseif(MICROS_BUILD_RAMFS_SERVICE_TEST)
            micros_add_bootstrap_user(
                micros_pm_service
                micros-pm-service
                servers/pm/pm_control.c
                ${MICROS_PM_CORE_SOURCES}
                ${MICROS_PM_SERVICE_SOURCES}
                ${MICROS_PM_COORDINATOR_SOURCES}
                ${MICROS_PM_RUNTIME_BACKEND_SOURCES}
                ${MICROS_SPAWN_REQUEST_SOURCES}
            )
            micros_add_bootstrap_user(
                micros_ramfs_service_vfs
                micros-ramfs-service-vfs
                tests/qemu/ramfs_service_report.S
                ${MICROS_RAMFS_QEMU_PEER_SOURCES}
            )
            set(
                MICROS_RAMFS_SERVICE_FIXTURE_SOURCE
                "${CMAKE_BINARY_DIR}/generated/ramfs_service_fixture.c"
            )
            add_custom_command(
                OUTPUT "${MICROS_RAMFS_SERVICE_FIXTURE_SOURCE}"
                COMMAND
                    "${MICROS_PYTHON}"
                    -B
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_ramfs_service_fixture.py"
                    --launcher-elf
                    "$<TARGET_FILE:micros_bootstrap_launcher>"
                    --vm-elf
                    "$<TARGET_FILE:micros_vm_server>"
                    --pm-elf
                    "$<TARGET_FILE:micros_pm_service>"
                    --tty-elf
                    "$<TARGET_FILE:micros_tty_service>"
                    --ramfs-elf
                    "$<TARGET_FILE:micros_ramfs_service>"
                    --vfs-elf
                    "$<TARGET_FILE:micros_ramfs_service_vfs>"
                    --output
                    "${MICROS_RAMFS_SERVICE_FIXTURE_SOURCE}"
                DEPENDS
                    micros_bootstrap_launcher
                    micros_vm_server
                    micros_pm_service
                    micros_tty_service
                    micros_ramfs_service
                    micros_ramfs_service_vfs
                    ${MICROS_USER_ELF_CHECKER_SOURCES}
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_bootstrap_fixture.py"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_ramfs_service_fixture.py"
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                VERBATIM
            )
            set_source_files_properties(
                "${MICROS_RAMFS_SERVICE_FIXTURE_SOURCE}"
                PROPERTIES GENERATED TRUE
            )
        else()
            micros_add_bootstrap_user(
                micros_vm_handoff_probe
                micros-vm-handoff-probe
                tests/qemu/vm_handoff_probe.S
                tests/qemu/vm_handoff_probe.c
            )
            set(
                MICROS_VM_HANDOFF_FIXTURE_SOURCE
                "${CMAKE_BINARY_DIR}/generated/vm_handoff_fixture.c"
            )
            add_custom_command(
                OUTPUT "${MICROS_VM_HANDOFF_FIXTURE_SOURCE}"
                COMMAND
                    "${MICROS_PYTHON}"
                    -B
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_vm_handoff_fixture.py"
                    --launcher-elf
                    "$<TARGET_FILE:micros_bootstrap_launcher>"
                    --vm-elf
                    "$<TARGET_FILE:micros_vm_server>"
                    --probe-elf
                    "$<TARGET_FILE:micros_vm_handoff_probe>"
                    --output
                    "${MICROS_VM_HANDOFF_FIXTURE_SOURCE}"
                DEPENDS
                    micros_bootstrap_launcher
                    micros_vm_server
                    micros_vm_handoff_probe
                    ${MICROS_USER_ELF_CHECKER_SOURCES}
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_bootstrap_fixture.py"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_vm_handoff_fixture.py"
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                VERBATIM
            )
            set_source_files_properties(
                "${MICROS_VM_HANDOFF_FIXTURE_SOURCE}"
                PROPERTIES GENERATED TRUE
            )
        endif()
    else()
        micros_add_bootstrap_user(
            micros_bootstrap_probe
            micros-bootstrap-probe
            tests/qemu/bootstrap_probe.S
            tests/qemu/bootstrap_probe.c
        )
        if(MICROS_BUILD_BOOTSTRAP_READY_TIMEOUT_TEST)
            target_compile_definitions(
                micros_bootstrap_probe
                PRIVATE
                    MICROS_BOOTSTRAP_PROBE_OMIT_READY=1
            )
        endif()
        set(
            MICROS_BOOTSTRAP_FIXTURE_SOURCE
            "${CMAKE_BINARY_DIR}/generated/bootstrap_fixture.c"
        )
        add_custom_command(
            OUTPUT "${MICROS_BOOTSTRAP_FIXTURE_SOURCE}"
            COMMAND
                "${MICROS_PYTHON}"
                -B
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_bootstrap_fixture.py"
                --launcher-elf
                "$<TARGET_FILE:micros_bootstrap_launcher>"
                --probe-elf
                "$<TARGET_FILE:micros_bootstrap_probe>"
                --output
                "${MICROS_BOOTSTRAP_FIXTURE_SOURCE}"
            DEPENDS
                micros_bootstrap_launcher
                micros_bootstrap_probe
                ${MICROS_USER_ELF_CHECKER_SOURCES}
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_bootstrap_fixture.py"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            VERBATIM
        )
        set_source_files_properties(
            "${MICROS_BOOTSTRAP_FIXTURE_SOURCE}"
            PROPERTIES GENERATED TRUE
        )
    endif()
endif()

add_executable(
    micros
    arch/riscv64/boot.S
    arch/riscv64/mmu.S
    arch/riscv64/panic.S
    arch/riscv64/scheduler.S
    arch/riscv64/sbi.c
    arch/riscv64/trap.S
    arch/riscv64/uart.c
    arch/riscv64/user_entry.S
    ${MICROS_KERNEL_ADDRESS_SPACE_SOURCES}
    kernel/bootstrap_control.c
    kernel/bootstrap_control_core.c
    ${MICROS_BOOTSTRAP_IMAGE_SOURCES}
    ${MICROS_BOOTSTRAP_MANIFEST_SOURCES}
    kernel/bootstrap_memory.c
    ${MICROS_BOOTSTRAP_RUNTIME_SOURCES}
    ${MICROS_BOOTSTRAP_SYSCALL_SOURCES}
    ${MICROS_ENDPOINT_CORE_SOURCES}
    kernel/platform/fdt.c
    kernel/frame_allocator.c
    ${MICROS_FRAME_OWNERSHIP_SOURCES}
    kernel/grant.c
    kernel/grant_abi.c
    ${MICROS_GRANT_COPY_TARGET_SOURCES}
    kernel/grant_copy_core.c
    kernel/grant_syscall_core.c
    kernel/grant_syscall.c
    kernel/grant_runtime.c
    kernel/ipc_abi.c
    ${MICROS_IPC_CORE_SOURCES}
    kernel/ipc_buffer.c
    kernel/ipc_runtime.c
    ${MICROS_IPC_SYSCALL_SOURCES}
    ${MICROS_SYSCALL_SOURCES}
    kernel/kernel_object_runtime.c
    ${MICROS_KERNEL_OBJECT_CORE_SOURCES}
    ${MICROS_SCHEDULER_CORE_SOURCES}
    ${MICROS_SCHEDULER_RUNTIME_SOURCES}
    ${MICROS_BOOT_FOUNDATION_SOURCES}
    kernel/panic.c
    kernel/platform/plic.c
    kernel/platform/plic_core.c
    ${MICROS_PM_CONTROL_CORE_SOURCES}
    ${MICROS_PM_CONTROL_TARGET_SOURCES}
    kernel/sv39.c
    kernel/platform/timer.c
    ${MICROS_TRAP_SOURCES}
    kernel/trap_route_core.c
    kernel/tty_control_core.c
    kernel/tty_control_syscall.c
    kernel/tty_control_syscall_core.c
    kernel/tty_fault.c
    kernel/tty_handoff_core.c
    ${MICROS_TTY_HANDOFF_RUNTIME_SOURCES}
    kernel/tty_interrupt.c
    kernel/uart_console_core.c
    ${MICROS_USER_ADDRESS_SPACE_CORE_SOURCES}
    ${MICROS_USER_ADDRESS_SPACE_SOURCES}
    ${MICROS_USER_EXECUTION_SOURCES}
    kernel/vm_bootstrap.c
    ${MICROS_VM_HANDOFF_CORE_SOURCES}
    kernel/vm_handoff_runtime.c
    ${MICROS_VM_HANDOFF_SYSCALL_SOURCES}
    ${MICROS_VM_SNAPSHOT_SOURCES}
    ${MICROS_VM_BOOT_VALIDATION_SOURCES}
)

if(MICROS_BUILD_UART_CONSOLE_TEST)
    target_compile_definitions(
        micros
        PRIVATE
            MICROS_BUILD_UART_CONSOLE_TEST=1
    )
    target_sources(
        micros
        PRIVATE
            kernel/uart_console_test.c
    )
endif()

if(MICROS_BUILD_USER_RUNTIME_TEST)
    add_dependencies(micros micros_user_runtime_service)
    target_compile_definitions(
        micros
        PRIVATE
            MICROS_BUILD_USER_RUNTIME_TEST=1
    )
    target_sources(
        micros
        PRIVATE
            "${MICROS_USER_RUNTIME_FIXTURE_SOURCE}"
            arch/riscv64/user_runtime_test.S
            ${MICROS_USER_RUNTIME_KERNEL_TEST_SOURCES}
    )
endif()

if(
    MICROS_BUILD_BOOTSTRAP_LAUNCHER_TEST
    OR MICROS_BUILD_BOOTSTRAP_READY_TIMEOUT_TEST
    OR MICROS_BUILD_BOOTSTRAP_MANIFEST_PANIC_TEST
)
    add_dependencies(
        micros
        micros_bootstrap_launcher
        micros_bootstrap_probe
    )
    if(MICROS_BUILD_BOOTSTRAP_LAUNCHER_TEST)
        target_compile_definitions(
            micros
            PRIVATE
                MICROS_BUILD_BOOTSTRAP_LAUNCHER_TEST=1
        )
    elseif(MICROS_BUILD_BOOTSTRAP_READY_TIMEOUT_TEST)
        target_compile_definitions(
            micros
            PRIVATE
                MICROS_BUILD_BOOTSTRAP_READY_TIMEOUT_TEST=1
        )
    else()
        target_compile_definitions(
            micros
            PRIVATE
                MICROS_BUILD_BOOTSTRAP_MANIFEST_PANIC_TEST=1
        )
    endif()
    target_sources(
        micros
        PRIVATE
            "${MICROS_BOOTSTRAP_FIXTURE_SOURCE}"
            kernel/bootstrap_test.c
    )
endif()

if(
    MICROS_BUILD_VM_HANDOFF_TEST
    OR MICROS_BUILD_VM_READY_EARLY_TEST
    OR MICROS_BUILD_VM_SELF_FAULT_TEST
    OR MICROS_BUILD_VM_SELF_FAULT_SEALED_TEST
)
    add_dependencies(
        micros
        micros_bootstrap_launcher
        micros_vm_server
        micros_vm_handoff_probe
    )
    if(MICROS_BUILD_VM_HANDOFF_TEST)
        target_compile_definitions(
            micros
            PRIVATE
                MICROS_BUILD_VM_HANDOFF_TEST=1
        )
    elseif(MICROS_BUILD_VM_READY_EARLY_TEST)
        target_compile_definitions(
            micros
            PRIVATE
                MICROS_BUILD_VM_READY_EARLY_TEST=1
        )
    elseif(MICROS_BUILD_VM_SELF_FAULT_TEST)
        target_compile_definitions(
            micros
            PRIVATE
                MICROS_BUILD_VM_SELF_FAULT_TEST=1
        )
    else()
        target_compile_definitions(
            micros
            PRIVATE
                MICROS_BUILD_VM_SELF_FAULT_SEALED_TEST=1
        )
    endif()
    target_sources(
        micros
        PRIVATE
            "${MICROS_VM_HANDOFF_FIXTURE_SOURCE}"
            kernel/vm_handoff_test.c
    )
endif()

if(MICROS_BUILD_PM_SERVICE_TEST)
    add_dependencies(
        micros
        micros_bootstrap_launcher
        micros_vm_server
        micros_pm_service
        micros_pm_service_probe
    )
    target_compile_definitions(
        micros
        PRIVATE
            MICROS_BUILD_PM_SERVICE_TEST=1
    )
    target_sources(
        micros
        PRIVATE
            "${MICROS_PM_SERVICE_FIXTURE_SOURCE}"
            ${MICROS_PM_SERVICE_QEMU_TEST_SOURCES}
    )
endif()

if(MICROS_BUILD_VM_SCRATCH_PREEMPTION_TEST)
    add_dependencies(
        micros
        micros_bootstrap_launcher
        micros_vm_server
        micros_pm_service
        micros_vm_scratch_preemption_driver
    )
    target_compile_definitions(
        micros
        PRIVATE
            MICROS_BUILD_VM_SCRATCH_PREEMPTION_TEST=1
    )
    target_sources(
        micros
        PRIVATE
            "${MICROS_VM_SCRATCH_PREEMPTION_FIXTURE_SOURCE}"
            ${MICROS_VM_SCRATCH_PREEMPTION_QEMU_TEST_SOURCES}
    )
endif()

if(MICROS_BUILD_TTY_SERVICE_TEST)
    add_dependencies(
        micros
        micros_bootstrap_launcher
        micros_vm_server
        micros_pm_service
        micros_tty_service
        micros_tty_service_vfs
    )
    target_compile_definitions(
        micros
        PRIVATE
            MICROS_BUILD_TTY_SERVICE_TEST=1
    )
    target_sources(
        micros
        PRIVATE
            "${MICROS_TTY_SERVICE_FIXTURE_SOURCE}"
            kernel/tty_service_test.c
    )
endif()

if(MICROS_BUILD_RAMFS_SERVICE_TEST)
    add_dependencies(
        micros
        micros_bootstrap_launcher
        micros_vm_server
        micros_pm_service
        micros_tty_service
        micros_ramfs_service
        micros_ramfs_service_vfs
    )
    target_compile_definitions(
        micros
        PRIVATE
            MICROS_BUILD_RAMFS_SERVICE_TEST=1
    )
    target_sources(
        micros
        PRIVATE
            "${MICROS_RAMFS_SERVICE_FIXTURE_SOURCE}"
            ${MICROS_RAMFS_QEMU_REPORT_SOURCES}
    )
endif()

if(MICROS_BUILD_ELF_SPAWN_TEST)
    add_dependencies(
        micros
        micros_bootstrap_launcher
        micros_vm_server
        micros_pm_service
        micros_tty_service
        micros_ramfs_service
        micros_vfs_service
        micros_elf_spawn_init
        micros_elf_spawn_child
    )
    target_compile_definitions(micros PRIVATE MICROS_BUILD_ELF_SPAWN_TEST=1)
    target_sources(
        micros
        PRIVATE
            "${MICROS_ELF_SPAWN_FIXTURE_SOURCE}"
            kernel/elf_spawn_test.c
            kernel/elf_spawn_test_fixture.c
            kernel/elf_spawn_test_runtime.c
            kernel/elf_spawn_test_capture.c
            kernel/elf_spawn_test_services.c
            kernel/elf_spawn_test_service_checks.c
            kernel/elf_spawn_test_report.c
            ${MICROS_ELF_SPAWN_RESOURCE_SOURCES}
    )
endif()

if(MICROS_BUILD_VFS_SERVICE_TEST)
    add_dependencies(
        micros
        micros_bootstrap_launcher
        micros_vm_server
        micros_pm_service
        micros_tty_service
        micros_ramfs_service
        micros_vfs_service
        micros_vfs_service_probe
    )
    target_compile_definitions(
        micros
        PRIVATE
            MICROS_BUILD_VFS_SERVICE_TEST=1
    )
    target_sources(
        micros
        PRIVATE
            "${MICROS_VFS_SERVICE_FIXTURE_SOURCE}"
            kernel/vfs_service_test.c
    )
endif()

set_target_properties(
    micros
    PROPERTIES
        OUTPUT_NAME micros
        SUFFIX ".elf"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/kernel"
        LINK_DEPENDS
            "${MICROS_LINKER_SCRIPT};${CMAKE_CURRENT_SOURCE_DIR}/tools/check_elf_sections.py"
)

target_include_directories(
    micros
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_compile_definitions(
    micros
    PRIVATE
        "MICROS_VERSION=\"${PROJECT_VERSION}\""
)

if(MICROS_BUILD_PANIC_TEST)
    target_compile_definitions(micros PRIVATE MICROS_BUILD_PANIC_TEST=1)
endif()

if(MICROS_BUILD_TRAP_TEST)
    target_compile_definitions(micros PRIVATE MICROS_BUILD_TRAP_TEST=1)
endif()

if(MICROS_BUILD_TIMER_TEST)
    target_compile_definitions(micros PRIVATE MICROS_BUILD_TIMER_TEST=1)
endif()

if(MICROS_BUILD_FRAME_ALLOCATOR_TEST)
    target_compile_definitions(
        micros
        PRIVATE
            MICROS_BUILD_FRAME_ALLOCATOR_TEST=1
    )
endif()

if(MICROS_BUILD_TRAP_PANIC_TEST)
    target_compile_definitions(
        micros
        PRIVATE
            MICROS_BUILD_TRAP_PANIC_TEST=1
    )
endif()

if(MICROS_BUILD_MMU_TEST)
    target_compile_definitions(micros PRIVATE MICROS_BUILD_MMU_TEST=1)
    target_sources(
        micros
        PRIVATE
            arch/riscv64/mmu_test.S
            kernel/mmu_test.c
    )
endif()

if(MICROS_BUILD_OBJECT_MODEL_TEST)
    add_custom_target(
        test-qemu-object-model
        COMMAND
            "${MICROS_PYTHON}"
            -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu_smoke.py"
            --qemu
            "${MICROS_QEMU}"
            --kernel
            "$<TARGET_FILE:micros>"
            --marker
            "MICROS_BOOT ${PROJECT_VERSION}"
            --marker
            "MICROS_FDT_READY"
            --marker
            "MICROS_TRAP_READY"
            --marker
            "MICROS_OBJECT_MODEL_TEST_PASS process-generation=advanced stale=rejected thread-limit=enforced hart-local=preserved"
            --require-fdt-events
            --require-fdt-reservations
            --require-trap-ready
            --require-frame-allocator-ready
            --require-mmu-ready
            --require-frame-ownership-ready
            --require-objects-ready
            --require-object-model-test-report
            --timeout
            8
        DEPENDS micros
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )
endif()

if(MICROS_BUILD_ENDPOINT_TEST)
    add_custom_target(
        test-qemu-endpoint
        COMMAND
            "${MICROS_PYTHON}"
            -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu_smoke.py"
            --qemu
            "${MICROS_QEMU}"
            --kernel
            "$<TARGET_FILE:micros>"
            --marker
            "MICROS_BOOT ${PROJECT_VERSION}"
            --marker
            "MICROS_FDT_READY"
            --marker
            "MICROS_TRAP_READY"
            --marker
            "MICROS_ENDPOINT_TEST_PASS generation=validated profiles=immutable visibility=staged authorization=separate grants=generation-safe"
            --require-fdt-events
            --require-fdt-reservations
            --require-trap-ready
            --require-frame-allocator-ready
            --require-mmu-ready
            --require-frame-ownership-ready
            --require-objects-ready
            --require-endpoint-test-report
            --timeout
            8
        DEPENDS micros
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )
endif()

if(MICROS_BUILD_GRANT_TEST)
    add_custom_target(
        test-qemu-grant
        COMMAND
            "${MICROS_PYTHON}"
            -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu_smoke.py"
            --qemu
            "${MICROS_QEMU}"
            --kernel
            "$<TARGET_FILE:micros>"
            --marker
            "MICROS_BOOT ${PROJECT_VERSION}"
            --marker
            "MICROS_FDT_READY"
            --marker
            "MICROS_TRAP_READY"
            --marker
            "MICROS_GRANT_TEST_PASS identity=generation-safe directions=checked bounds=validated copies=atomic phase=bootstrap cleanup=complete"
            --require-fdt-events
            --require-fdt-reservations
            --require-trap-ready
            --require-frame-allocator-ready
            --require-mmu-ready
            --require-frame-ownership-ready
            --require-objects-ready
            --timeout
            8
        DEPENDS micros
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )
endif()

if(MICROS_BUILD_GRANT_TEST)
    target_compile_definitions(
        micros
        PRIVATE
            MICROS_BUILD_GRANT_TEST=1
    )
    target_sources(
        micros
        PRIVATE
            kernel/grant_test.c
    )
endif()

if(MICROS_BUILD_IPC_TEST)
    add_custom_target(
        test-qemu-ipc
        COMMAND
            "${MICROS_PYTHON}"
            -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu_smoke.py"
            --qemu
            "${MICROS_QEMU}"
            --kernel
            "$<TARGET_FILE:micros>"
            --marker
            "MICROS_BOOT ${PROJECT_VERSION}"
            --marker
            "MICROS_FDT_READY"
            --marker
            "MICROS_TRAP_READY"
            --marker
            "MICROS_IPC_TEST_PASS endpoints=generation-safe queues=blocking calls=tokenized notifications=coalesced kernel-events=injected deadlock=rejected"
            --require-fdt-events
            --require-fdt-reservations
            --require-trap-ready
            --require-frame-allocator-ready
            --require-mmu-ready
            --require-frame-ownership-ready
            --require-objects-ready
            --require-ipc-test-report
            --timeout
            8
        DEPENDS micros
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )
endif()

if(MICROS_BUILD_IPC_ECALL_CORE_TEST)
    add_custom_target(
        test-qemu-ipc-ecall-core
        COMMAND
            "${MICROS_PYTHON}"
            -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu_smoke.py"
            --qemu
            "${MICROS_QEMU}"
            --kernel
            "$<TARGET_FILE:micros>"
            --marker
            "MICROS_BOOT ${PROJECT_VERSION}"
            --marker
            "MICROS_FDT_READY"
            --marker
            "MICROS_TRAP_READY"
            --marker
            "MICROS_IPC_ECALL_CORE_TEST_PASS dispatch=production guard=transactional completion=returned errors=stable registers=preserved"
            --require-fdt-events
            --require-fdt-reservations
            --require-trap-ready
            --require-frame-allocator-ready
            --require-mmu-ready
            --require-frame-ownership-ready
            --require-objects-ready
            --timeout
            8
        DEPENDS micros
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )
endif()

if(MICROS_BUILD_IPC_SYSCALL_TEST)
    add_custom_target(
        test-qemu-ipc-syscall
        COMMAND
            "${MICROS_PYTHON}"
            -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu_smoke.py"
            --qemu
            "${MICROS_QEMU}"
            --kernel
            "$<TARGET_FILE:micros>"
            --marker
            "MICROS_BOOT ${PROJECT_VERSION}"
            --marker
            "MICROS_FDT_READY"
            --marker
            "MICROS_TRAP_READY"
            --marker
            "MICROS_IPC_SYSCALL_TEST_PASS spaces=three operations=six blocking=validated calls=tokenized notifications=coalesced errors=stable completion=deferred registers=preserved cleanup=complete"
            --require-fdt-events
            --require-fdt-reservations
            --require-trap-ready
            --require-frame-allocator-ready
            --require-mmu-ready
            --require-frame-ownership-ready
            --require-objects-ready
            --timeout
            8
        DEPENDS micros
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )
endif()

if(MICROS_BUILD_IPC_SYSCALL_PANIC_TEST)
    add_custom_target(
        test-qemu-ipc-syscall-panic
        COMMAND
            "${MICROS_PYTHON}"
            -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu_smoke.py"
            --qemu
            "${MICROS_QEMU}"
            --kernel
            "$<TARGET_FILE:micros>"
            --marker
            "MICROS_BOOT ${PROJECT_VERSION}"
            --marker
            "MICROS_FDT_READY"
            --marker
            "MICROS_TRAP_READY"
            --marker
            "MICROS_PANIC reason=invalid-bootstrap-ipc-buffer"
            --expect
            panic
            --require-fdt-events
            --require-fdt-reservations
            --require-panic-report
            --require-trap-ready
            --require-frame-allocator-ready
            --require-mmu-ready
            --require-frame-ownership-ready
            --require-objects-ready
            --require-trap-context
            --timeout
            8
        DEPENDS micros
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )
endif()

if(MICROS_BUILD_BOOTSTRAP_LAUNCHER_TEST)
    add_custom_target(
        test-qemu-bootstrap-launcher
        COMMAND
            "${MICROS_PYTHON}"
            -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu_smoke.py"
            --qemu
            "${MICROS_QEMU}"
            --kernel
            "$<TARGET_FILE:micros>"
            --marker
            "MICROS_BOOT ${PROJECT_VERSION}"
            --marker
            "MICROS_FDT_READY"
            --marker
            "MICROS_TRAP_READY"
            --marker
            "MICROS_BOOTSTRAP_TEST_PASS manifest=immutable order=topological profiles=exact endpoints=staged readiness=acknowledged authority=revoked"
            --require-fdt-events
            --require-fdt-reservations
            --require-trap-ready
            --require-frame-allocator-ready
            --require-mmu-ready
            --require-frame-ownership-ready
            --require-objects-ready
            --timeout
            20
        DEPENDS micros
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )
endif()

if(MICROS_BUILD_VM_HANDOFF_TEST)
    add_custom_target(
        test-qemu-vm-handoff
        COMMAND
            "${MICROS_PYTHON}"
            -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu_smoke.py"
            --qemu
            "${MICROS_QEMU}"
            --kernel
            "$<TARGET_FILE:micros>"
            --marker
            "MICROS_BOOT ${PROJECT_VERSION}"
            --marker
            "MICROS_FDT_READY"
            --marker
            "MICROS_TRAP_READY"
            --marker
            "MICROS_VM_HANDOFF_TEST_PASS snapshot=validated ownership=handed-off vm=wired readiness=acknowledged authority=vm"
            --require-fdt-events
            --require-fdt-reservations
            --require-trap-ready
            --require-frame-allocator-ready
            --require-mmu-ready
            --require-frame-ownership-ready
            --require-objects-ready
            --timeout
            45
        DEPENDS micros
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )
endif()

if(MICROS_BUILD_PM_SERVICE_TEST)
    add_custom_target(
        test-qemu-pm-service
        COMMAND
            "${MICROS_PYTHON}"
            -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu_smoke.py"
            --qemu
            "${MICROS_QEMU}"
            --kernel
            "$<TARGET_FILE:micros>"
            --marker
            "MICROS_BOOT ${PROJECT_VERSION}"
            --marker
            "MICROS_FDT_READY"
            --marker
            "MICROS_TRAP_READY"
            --marker
            "MICROS_PM_SERVICE_TEST_PASS handoff=complete readiness=acknowledged protocol=stable sealed=received prepare=aborted-reprepared activate=committed exit=detached-released resources=clean process-generation=advanced thread-generation=advanced tokens=advanced"
            --marker
            "MICROS_APPLICATION_RUNTIME_TEST_PASS image=checked argc=2 env=1 auxv=validated gp-tp=zero return=pm-exit code=37 cleanup=complete"
            --require-fdt-events
            --require-fdt-reservations
            --require-trap-ready
            --require-frame-allocator-ready
            --require-mmu-ready
            --require-frame-ownership-ready
            --require-objects-ready
            --timeout
            45
        DEPENDS micros
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )
endif()

if(MICROS_BUILD_VM_SCRATCH_PREEMPTION_TEST)
    add_custom_target(
        test-qemu-vm-scratch-preemption
        COMMAND
            "${MICROS_PYTHON}"
            -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu_smoke.py"
            --qemu
            "${MICROS_QEMU}"
            --kernel
            "$<TARGET_FILE:micros>"
            --marker
            "MICROS_BOOT ${PROJECT_VERSION}"
            --marker
            "MICROS_FDT_READY"
            --marker
            "MICROS_TRAP_READY"
            --marker
            "MICROS_VM_SCRATCH_PREEMPTION_TEST_PASS map=real timer=preempted roots=distinct inactive-validation=accepted reactivation=fenced sentinel=preserved commit=real stale-translation=faulted pte=absent ownership=transferable scratch=clear"
            --require-fdt-events
            --require-fdt-reservations
            --require-trap-ready
            --require-frame-allocator-ready
            --require-mmu-ready
            --require-frame-ownership-ready
            --require-objects-ready
            --timeout
            45
        DEPENDS micros
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )
endif()

if(MICROS_BUILD_TTY_SERVICE_TEST)
    add_custom_target(
        test-qemu-tty
        COMMAND
            "${MICROS_PYTHON}"
            -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu_smoke.py"
            --qemu
            "${MICROS_QEMU}"
            --kernel
            "$<TARGET_FILE:micros>"
            --marker
            "MICROS_BOOT ${PROJECT_VERSION}"
            --marker
            "MICROS_FDT_READY"
            --marker
            "MICROS_TRAP_READY"
            --marker
            "MICROS_TTY_TEST_PASS handoff=two-phase mapping=exact irq=deferred input=canonical output=interrupt-driven grants=checked"
            --serial-input-trigger
            "MICROS_TTY_INPUT_READY"
            --serial-input-hex
            "6d6963726f732d747479787f2d696e7075740d"
            --require-fdt-events
            --require-fdt-reservations
            --require-trap-ready
            --require-frame-allocator-ready
            --require-mmu-ready
            --require-frame-ownership-ready
            --require-objects-ready
            --timeout
            90
        DEPENDS micros
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )
endif()

if(MICROS_BUILD_RAMFS_SERVICE_TEST)
    add_custom_target(
        test-qemu-ramfs
        COMMAND
            "${MICROS_PYTHON}"
            -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu_smoke.py"
            --qemu
            "${MICROS_QEMU}"
            --kernel
            "$<TARGET_FILE:micros>"
            --marker
            "MICROS_BOOT ${PROJECT_VERSION}"
            --marker
            "MICROS_FDT_READY"
            --marker
            "MICROS_TRAP_READY"
            --marker
            "MICROS_RAMFS_TEST_PASS seed=validated mount=single lookup=bounded files=writable directories=cursor grants=checked refs=balanced"
            --require-fdt-events
            --require-fdt-reservations
            --require-trap-ready
            --require-frame-allocator-ready
            --require-mmu-ready
            --require-frame-ownership-ready
            --require-objects-ready
            --timeout
            180
        DEPENDS micros
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )
endif()

if(MICROS_BUILD_VFS_SERVICE_TEST)
    add_custom_target(
        test-qemu-vfs
        COMMAND
            "${MICROS_PYTHON}"
            -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu_smoke.py"
            --qemu
            "${MICROS_QEMU}"
            --kernel
            "$<TARGET_FILE:micros>"
            --marker
            "MICROS_BOOT ${PROJECT_VERSION}"
            --marker
            "MICROS_FDT_READY"
            --marker
            "MICROS_TRAP_READY"
            --marker
            "MICROS_VFS_TEST_PASS mount=single descriptors=owned paths=absolute,relative files=two-hop directories=translated console=two-hop grants=balanced"
            --serial-input-trigger
            "MICROS_VFS_TEST_INPUT_READY"
            --serial-input-hex
            "6d6963726f732d766673787f2d696e7075740d"
            --require-fdt-events
            --require-fdt-reservations
            --require-trap-ready
            --require-frame-allocator-ready
            --require-mmu-ready
            --require-frame-ownership-ready
            --require-objects-ready
            --timeout
            180
        DEPENDS micros
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )
endif()

if(MICROS_BUILD_ELF_SPAWN_TEST)
    add_custom_target(
        test-qemu-elf-spawn
        COMMAND
            "${MICROS_PYTHON}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu_smoke.py"
            --qemu "${MICROS_QEMU}"
            --kernel "$<TARGET_FILE:micros>"
            --marker "MICROS_BOOT ${PROJECT_VERSION}"
            --marker "MICROS_FDT_READY"
            --marker "MICROS_TRAP_READY"
            --marker
            "MICROS_ELF_SPAWN_TEST_PASS services=production init=dynamic rollback=post-freeze arguments=validated descriptors=inherited io=two-hop exit=normal wait=one-shot resources=baseline"
            --require-fdt-events
            --require-fdt-reservations
            --require-trap-ready
            --require-frame-allocator-ready
            --require-mmu-ready
            --require-frame-ownership-ready
            --require-objects-ready
            --timeout 180
        DEPENDS micros
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )
endif()

if(MICROS_BUILD_VM_READY_EARLY_TEST)
    add_custom_target(
        test-qemu-vm-ready-early
        COMMAND
            "${MICROS_PYTHON}"
            -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu_smoke.py"
            --qemu
            "${MICROS_QEMU}"
            --kernel
            "$<TARGET_FILE:micros>"
            --marker
            "MICROS_BOOT ${PROJECT_VERSION}"
            --marker
            "MICROS_FDT_READY"
            --marker
            "MICROS_TRAP_READY"
            --marker
            "MICROS_PANIC reason=bootstrap-failure"
            --pattern
            "MICROS_BOOTSTRAP_FAILURE reason=ready-role-gate service=0x0000000000000002 endpoint=0x0000000000001001 phase=failed state=starting detail=0x0000000000000000"
            --forbid-marker
            "MICROS_VM_HANDOFF_TEST_PASS snapshot=validated ownership=handed-off vm=wired readiness=acknowledged authority=vm"
            --expect
            panic
            --require-fdt-events
            --require-fdt-reservations
            --require-panic-report
            --require-trap-ready
            --require-frame-allocator-ready
            --require-mmu-ready
            --require-frame-ownership-ready
            --require-objects-ready
            --timeout
            30
        DEPENDS micros
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )
endif()

if(MICROS_BUILD_VM_SELF_FAULT_TEST)
    add_custom_target(
        test-qemu-vm-self-fault
        COMMAND
            "${MICROS_PYTHON}"
            -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu_smoke.py"
            --qemu
            "${MICROS_QEMU}"
            --nm
            "${CMAKE_NM}"
            --kernel
            "$<TARGET_FILE:micros>"
            --marker
            "MICROS_BOOT ${PROJECT_VERSION}"
            --marker
            "MICROS_FDT_READY"
            --marker
            "MICROS_TRAP_READY"
            --marker
            "MICROS_PANIC reason=bootstrap-failure"
            --ordered-pattern
            "MICROS_VM_SELF_FAULT service=0x0000000000000002 process-slot=0x0000000000000001 process-generation=0x0000000000000001 endpoint=0x0000000000001001 scause=0x000000000000000f stval=0x0000000070000000 sepc=0x[0-9a-f]{16} ownership=handed-off"
            --ordered-pattern
            "MICROS_BOOTSTRAP_FAILURE reason=service-fault service=0x0000000000000002 endpoint=0x0000000000001001 phase=failed state=starting detail=0x0000000000000000"
            --ordered-pattern
            "MICROS_PANIC reason=bootstrap-failure"
            --forbid-marker
            "MICROS_VM_HANDOFF_TEST_PASS snapshot=validated ownership=handed-off vm=wired readiness=acknowledged authority=vm"
            --expect
            panic
            --require-fdt-events
            --require-fdt-reservations
            --require-panic-report
            --require-trap-ready
            --require-frame-allocator-ready
            --require-mmu-ready
            --require-frame-ownership-ready
            --require-objects-ready
            --require-trap-context
            --trap-context-origin
            U
            --trap-context-scause
            0xf
            --trap-context-stval
            0x70000000
            --require-vm-self-fault
            --vm-self-fault-elf
            "$<TARGET_FILE:micros_vm_server>"
            --vm-self-fault-symbol
            micros_vm_self_fault_instruction
            --vm-self-fault-scause
            0xf
            --vm-self-fault-stval
            0x70000000
            --vm-self-fault-ownership
            handed-off
            --timeout
            30
        DEPENDS micros
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )
endif()

if(MICROS_BUILD_VM_SELF_FAULT_SEALED_TEST)
    add_custom_target(
        test-qemu-vm-self-fault-sealed
        COMMAND
            "${MICROS_PYTHON}"
            -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu_smoke.py"
            --qemu
            "${MICROS_QEMU}"
            --nm
            "${CMAKE_NM}"
            --kernel
            "$<TARGET_FILE:micros>"
            --marker
            "MICROS_BOOT ${PROJECT_VERSION}"
            --marker
            "MICROS_FDT_READY"
            --marker
            "MICROS_TRAP_READY"
            --marker
            "MICROS_PANIC reason=vm-self-fault"
            --ordered-pattern
            "MICROS_VM_SELF_FAULT service=0x0000000000000002 process-slot=0x0000000000000001 process-generation=0x0000000000000001 endpoint=0x0000000000001001 scause=0x000000000000000f stval=0x0000000070000000 sepc=0x[0-9a-f]{16} ownership=handed-off"
            --ordered-pattern
            "MICROS_PANIC reason=vm-self-fault"
            --forbid-pattern
            "MICROS_BOOTSTRAP_FAILURE .+"
            --forbid-marker
            "MICROS_VM_HANDOFF_TEST_PASS snapshot=validated ownership=handed-off vm=wired readiness=acknowledged authority=vm"
            --expect
            panic
            --require-fdt-events
            --require-fdt-reservations
            --require-panic-report
            --require-trap-ready
            --require-frame-allocator-ready
            --require-mmu-ready
            --require-frame-ownership-ready
            --require-objects-ready
            --require-trap-context
            --trap-context-origin
            U
            --trap-context-scause
            0xf
            --trap-context-stval
            0x70000000
            --require-vm-self-fault
            --vm-self-fault-elf
            "$<TARGET_FILE:micros_vm_server>"
            --vm-self-fault-symbol
            micros_vm_self_fault_instruction
            --vm-self-fault-scause
            0xf
            --vm-self-fault-stval
            0x70000000
            --vm-self-fault-ownership
            handed-off
            --timeout
            30
        DEPENDS micros
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )
endif()

if(MICROS_BUILD_BOOTSTRAP_READY_TIMEOUT_TEST)
    add_custom_target(
        test-qemu-bootstrap-ready-timeout
        COMMAND
            "${MICROS_PYTHON}"
            -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu_smoke.py"
            --qemu
            "${MICROS_QEMU}"
            --kernel
            "$<TARGET_FILE:micros>"
            --marker
            "MICROS_BOOT ${PROJECT_VERSION}"
            --marker
            "MICROS_FDT_READY"
            --marker
            "MICROS_TRAP_READY"
            --marker
            "MICROS_PANIC reason=bootstrap-failure"
            --pattern
            "MICROS_BOOTSTRAP_FAILURE reason=ready-timeout service=0x0000000000000002 endpoint=0x0000000000001001 phase=failed state=starting detail=0x[0-9a-f]{16}"
            --forbid-marker
            "MICROS_BOOTSTRAP_TEST_PASS manifest=immutable order=topological profiles=exact endpoints=staged readiness=acknowledged authority=revoked"
            --expect
            panic
            --require-fdt-events
            --require-fdt-reservations
            --require-panic-report
            --require-trap-ready
            --require-frame-allocator-ready
            --require-mmu-ready
            --require-frame-ownership-ready
            --require-objects-ready
            --timeout
            20
        DEPENDS micros
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )
endif()

if(MICROS_BUILD_BOOTSTRAP_MANIFEST_PANIC_TEST)
    add_custom_target(
        test-qemu-bootstrap-manifest-panic
        COMMAND
            "${MICROS_PYTHON}"
            -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu_smoke.py"
            --qemu
            "${MICROS_QEMU}"
            --kernel
            "$<TARGET_FILE:micros>"
            --marker
            "MICROS_BOOT ${PROJECT_VERSION}"
            --marker
            "MICROS_FDT_READY"
            --marker
            "MICROS_TRAP_READY"
            --marker
            "MICROS_PANIC reason=bootstrap-failure"
            --marker
            "MICROS_BOOTSTRAP_FAILURE reason=manifest-cycle service=0x0000000000000002 endpoint=0x00000000fffffffe phase=failed state=none detail=0x0000000000000006"
            --forbid-marker
            "MICROS_BOOTSTRAP_TEST_PASS manifest=immutable order=topological profiles=exact endpoints=staged readiness=acknowledged authority=revoked"
            --expect
            panic
            --require-fdt-events
            --require-fdt-reservations
            --require-panic-report
            --require-trap-ready
            --require-frame-allocator-ready
            --require-mmu-ready
            --require-frame-ownership-ready
            --require-objects-ready
            --timeout
            20
        DEPENDS micros
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )
endif()

if(MICROS_BUILD_GRANT_SYSCALL_TEST)
    add_custom_target(
        test-qemu-grant-syscall
        COMMAND
            "${MICROS_PYTHON}"
            -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu_smoke.py"
            --qemu
            "${MICROS_QEMU}"
            --kernel
            "$<TARGET_FILE:micros>"
            --marker
            "MICROS_BOOT ${PROJECT_VERSION}"
            --marker
            "MICROS_FDT_READY"
            --marker
            "MICROS_TRAP_READY"
            --marker
            "MICROS_GRANT_SYSCALL_TEST_PASS namespace=unified phase=bootstrap lifecycle=checked directions=checked errors=stable registers=preserved cleanup=complete"
            --require-fdt-events
            --require-fdt-reservations
            --require-trap-ready
            --require-frame-allocator-ready
            --require-mmu-ready
            --require-frame-ownership-ready
            --require-objects-ready
            --timeout
            15
        DEPENDS micros
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )
endif()

if(MICROS_BUILD_USER_RUNTIME_TEST)
    add_custom_target(
        test-qemu-user-runtime
        COMMAND
            "${MICROS_PYTHON}"
            -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu_smoke.py"
            --qemu
            "${MICROS_QEMU}"
            --kernel
            "$<TARGET_FILE:micros>"
            --marker
            "MICROS_BOOT ${PROJECT_VERSION}"
            --marker
            "MICROS_FDT_READY"
            --marker
            "MICROS_TRAP_READY"
            --marker
            "MICROS_USER_RUNTIME_TEST_PASS elf=freestanding startup=validated syscalls=1-10 registers=preserved stack=external data=initialized bss=zero rodata=protected return=trapped cleanup=complete"
            --require-fdt-events
            --require-fdt-reservations
            --require-trap-ready
            --require-frame-allocator-ready
            --require-mmu-ready
            --require-frame-ownership-ready
            --require-objects-ready
            --timeout
            12
        DEPENDS micros
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )
endif()

if(MICROS_BUILD_GRANT_SYSCALL_TEST)
    target_compile_definitions(
        micros
        PRIVATE
            MICROS_BUILD_GRANT_SYSCALL_TEST=1
    )
    target_sources(
        micros
        PRIVATE
            arch/riscv64/grant_syscall_test.S
            ${MICROS_GRANT_SYSCALL_QEMU_TEST_SOURCES}
    )
endif()

if(MICROS_BUILD_NESTED_TRAP_TEST)
    add_custom_target(
        test-qemu-nested-trap
        COMMAND
            "${MICROS_PYTHON}"
            -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu_smoke.py"
            --qemu
            "${MICROS_QEMU}"
            --kernel
            "$<TARGET_FILE:micros>"
            --marker
            "MICROS_BOOT ${PROJECT_VERSION}"
            --marker
            "MICROS_FDT_READY"
            --marker
            "MICROS_TRAP_READY"
            --marker
            "MICROS_NESTED_TRAP_TEST_PASS hart=routed emergency-stack=selected"
            --require-fdt-events
            --require-fdt-reservations
            --require-trap-ready
            --require-frame-allocator-ready
            --require-mmu-ready
            --require-frame-ownership-ready
            --require-objects-ready
            --require-nested-trap-test-report
            --timeout
            8
        DEPENDS micros
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )
endif()

if(MICROS_BUILD_FRAME_OWNERSHIP_TEST)
    add_custom_target(
        test-qemu-frame-ownership
        COMMAND
            "${MICROS_PYTHON}"
            -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu_smoke.py"
            --qemu
            "${MICROS_QEMU}"
            --kernel
            "$<TARGET_FILE:micros>"
            --marker
            "MICROS_BOOT ${PROJECT_VERSION}"
            --marker
            "MICROS_FDT_READY"
            --marker
            "MICROS_TRAP_READY"
            --marker
            "MICROS_FRAME_OWNERSHIP_TEST_PASS stale=rejected release=blocked handoff=atomic invariants=preserved"
            --require-fdt-events
            --require-fdt-reservations
            --require-trap-ready
            --require-frame-allocator-ready
            --require-mmu-ready
            --require-frame-ownership-ready
            --require-frame-ownership-test-report
            --require-objects-ready
            --timeout
            8
        DEPENDS micros
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )
endif()

if(MICROS_BUILD_USER_ADDRESS_SPACE_TEST)
    add_custom_target(
        test-qemu-user-address-space
        COMMAND
            "${MICROS_PYTHON}"
            -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu_smoke.py"
            --qemu
            "${MICROS_QEMU}"
            --kernel
            "$<TARGET_FILE:micros>"
            --marker
            "MICROS_BOOT ${PROJECT_VERSION}"
            --marker
            "MICROS_FDT_READY"
            --marker
            "MICROS_TRAP_READY"
            --marker
            "MICROS_USER_ADDRESS_SPACE_TEST_PASS roots=isolated reuse=zeroed active=guarded ownership=validated sum=cleared"
            --require-fdt-events
            --require-fdt-reservations
            --require-trap-ready
            --require-frame-allocator-ready
            --require-mmu-ready
            --require-frame-ownership-ready
            --require-user-address-space-test-report
            --require-objects-ready
            --timeout
            8
        DEPENDS micros
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )
endif()

if(MICROS_BUILD_ADDRESS_SPACE_HANDOFF_TEST)
    add_custom_target(
        test-qemu-address-space-handoff
        COMMAND
            "${MICROS_PYTHON}"
            -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu_smoke.py"
            --qemu
            "${MICROS_QEMU}"
            --kernel
            "$<TARGET_FILE:micros>"
            --marker
            "MICROS_BOOT ${PROJECT_VERSION}"
            --marker
            "MICROS_FDT_READY"
            --marker
            "MICROS_TRAP_READY"
            --marker
            "MICROS_ADDRESS_SPACE_HANDOFF_TEST_PASS phase=handed-off wired=validated ipc=resident grants=atomic mutation=revoked"
            --marker
            "MICROS_GRANT_SYSCALL_HANDOFF_PASS phase=handed-off operations=create,revoke,copy-from,copy-to errors=stable registers=preserved"
            --require-fdt-events
            --require-fdt-reservations
            --require-trap-ready
            --require-frame-allocator-ready
            --require-mmu-ready
            --require-frame-ownership-ready
            --require-objects-ready
            --timeout
            20
        DEPENDS micros
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )
endif()

if(MICROS_BUILD_USER_EXECUTION_TEST)
    add_custom_target(
        test-qemu-user-execution
        COMMAND
            "${MICROS_PYTHON}"
            -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu_smoke.py"
            --qemu
            "${MICROS_QEMU}"
            --kernel
            "$<TARGET_FILE:micros>"
            --marker
            "MICROS_BOOT ${PROJECT_VERSION}"
            --marker
            "MICROS_FDT_READY"
            --marker
            "MICROS_TRAP_READY"
            --marker
            "MICROS_USER_EXECUTION_TEST_PASS mode=entered faults=isolated context=preserved stack=owned return=resumed"
            --require-fdt-events
            --require-fdt-reservations
            --require-trap-ready
            --require-frame-allocator-ready
            --require-mmu-ready
            --require-frame-ownership-ready
            --require-user-execution-test-report
            --require-objects-ready
            --timeout
            8
        DEPENDS micros
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )
endif()

if(MICROS_BUILD_SCHEDULER_TEST)
    add_custom_target(
        test-qemu-scheduler
        COMMAND
            "${MICROS_PYTHON}"
            -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu_smoke.py"
            --qemu
            "${MICROS_QEMU}"
            --kernel
            "$<TARGET_FILE:micros>"
            --marker
            "MICROS_BOOT ${PROJECT_VERSION}"
            --marker
            "MICROS_FDT_READY"
            --marker
            "MICROS_TRAP_READY"
            --marker
            "MICROS_IPC_RETURN completions=shared paths=start,user,captured,idle buffers=bounded"
            --marker
            "MICROS_SCHEDULER_TEST_PASS queues=minix-priority current=reachable accounting=separate switches=alternating idle=resumed registers=preserved"
            --marker
            "MICROS_TTY_TRAP_TEST_PASS user=cause9-scheduled supervisor=cause9-direct idle=cause9-selected"
            --require-fdt-events
            --require-fdt-reservations
            --require-trap-ready
            --require-frame-allocator-ready
            --require-mmu-ready
            --require-frame-ownership-ready
            --require-scheduler-test-report
            --require-objects-ready
            --timeout
            8
        DEPENDS micros
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )
endif()

if(
    MICROS_BUILD_SCHEDULER_INVALID_OUTGOING_TEST
    OR MICROS_BUILD_SCHEDULER_INVALID_NEXT_TEST
)
    if(MICROS_BUILD_SCHEDULER_INVALID_OUTGOING_TEST)
        set(MICROS_SCHEDULER_INVALID_STATE outgoing)
    else()
        set(MICROS_SCHEDULER_INVALID_STATE next)
    endif()
    add_custom_target(
        test-qemu-scheduler-invalid-context
        COMMAND
            "${MICROS_PYTHON}"
            -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu_smoke.py"
            --qemu
            "${MICROS_QEMU}"
            --kernel
            "$<TARGET_FILE:micros>"
            --marker
            "MICROS_BOOT ${PROJECT_VERSION}"
            --marker
            "MICROS_FDT_READY"
            --marker
            "MICROS_TRAP_READY"
            --marker
            "MICROS_PANIC reason=invalid-bootstrap-user-context"
            --marker
            "MICROS_SCHEDULER_INVALID_CONTEXT state=${MICROS_SCHEDULER_INVALID_STATE} ownership=preserved accounting=kernel"
            --pattern
            "MICROS_TRAP_CONTEXT origin=U sstatus=0x[0-9a-f]{16} scause=0x8000000000000005 stval=0x[0-9a-f]{16} sepc=0x[0-9a-f]{16} ra=0x[0-9a-f]{16} sp=0x[0-9a-f]{16}"
            --expect
            panic
            --require-fdt-events
            --require-fdt-reservations
            --require-panic-report
            --require-trap-ready
            --require-frame-allocator-ready
            --require-mmu-ready
            --require-frame-ownership-ready
            --require-objects-ready
            --require-trap-context
            --require-scheduler-invalid-context
            "${MICROS_SCHEDULER_INVALID_STATE}"
            --timeout
            8
        DEPENDS micros
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )
endif()

if(MICROS_BUILD_OBJECT_MODEL_TEST)
    target_compile_definitions(
        micros
        PRIVATE
            MICROS_BUILD_OBJECT_MODEL_TEST=1
    )
    target_sources(
        micros
        PRIVATE
            ${MICROS_KERNEL_OBJECT_QEMU_TEST_SOURCES}
    )
endif()

if(MICROS_BUILD_ENDPOINT_TEST)
    target_compile_definitions(
        micros
        PRIVATE
            MICROS_BUILD_ENDPOINT_TEST=1
    )
    target_sources(
        micros
        PRIVATE
            ${MICROS_ENDPOINT_QEMU_TEST_SOURCES}
    )
endif()

if(MICROS_BUILD_IPC_TEST)
    target_compile_definitions(
        micros
        PRIVATE
            MICROS_BUILD_IPC_TEST=1
    )
    target_sources(
        micros
        PRIVATE
            ${MICROS_IPC_QEMU_TEST_SOURCES}
    )
endif()

if(MICROS_BUILD_IPC_ECALL_CORE_TEST)
    target_compile_definitions(
        micros
        PRIVATE
            MICROS_BUILD_IPC_ECALL_CORE_TEST=1
    )
    target_sources(
        micros
        PRIVATE
            arch/riscv64/ipc_ecall_test.S
            ${MICROS_IPC_ECALL_QEMU_TEST_SOURCES}
    )
endif()

if(MICROS_BUILD_IPC_SYSCALL_TEST)
    target_compile_definitions(
        micros
        PRIVATE
            MICROS_BUILD_IPC_SYSCALL_TEST=1
    )
    target_sources(
        micros
        PRIVATE
            arch/riscv64/ipc_syscall_test.S
            ${MICROS_IPC_SYSCALL_QEMU_TEST_SOURCES}
    )
endif()

if(MICROS_BUILD_IPC_SYSCALL_PANIC_TEST)
    target_compile_definitions(
        micros
        PRIVATE
            MICROS_BUILD_IPC_SYSCALL_PANIC_TEST=1
    )
    target_sources(
        micros
        PRIVATE
            arch/riscv64/ipc_syscall_panic_test.S
            kernel/ipc_syscall_panic_test.c
    )
endif()

if(MICROS_BUILD_NESTED_TRAP_TEST)
    target_compile_definitions(
        micros
        PRIVATE
            MICROS_BUILD_NESTED_TRAP_TEST=1
    )
    target_sources(
        micros
        PRIVATE
            arch/riscv64/nested_trap_test.S
    )
endif()

if(MICROS_BUILD_FRAME_OWNERSHIP_TEST)
    target_compile_definitions(
        micros
        PRIVATE
            MICROS_BUILD_FRAME_OWNERSHIP_TEST=1
    )
    target_sources(
        micros
        PRIVATE
            ${MICROS_FRAME_OWNERSHIP_QEMU_TEST_SOURCES}
    )
endif()

if(MICROS_BUILD_USER_ADDRESS_SPACE_TEST)
    target_compile_definitions(
        micros
        PRIVATE
            MICROS_BUILD_USER_ADDRESS_SPACE_TEST=1
    )
    target_sources(
        micros
        PRIVATE
            arch/riscv64/user_address_space_test.S
            ${MICROS_USER_ADDRESS_SPACE_QEMU_TEST_SOURCES}
    )
endif()

if(MICROS_BUILD_ADDRESS_SPACE_HANDOFF_TEST)
    target_compile_definitions(
        micros
        PRIVATE
            MICROS_BUILD_ADDRESS_SPACE_HANDOFF_TEST=1
    )
    target_sources(
        micros
        PRIVATE
            arch/riscv64/address_space_handoff_test.S
            ${MICROS_ADDRESS_SPACE_HANDOFF_QEMU_TEST_SOURCES}
    )
endif()

if(MICROS_BUILD_USER_EXECUTION_TEST)
    target_compile_definitions(
        micros
        PRIVATE
            MICROS_BUILD_USER_EXECUTION_TEST=1
    )
    target_sources(
        micros
        PRIVATE
            arch/riscv64/user_execution_test.S
            ${MICROS_USER_EXECUTION_QEMU_TEST_SOURCES}
    )
endif()

if(MICROS_BUILD_SCHEDULER_TEST)
    target_compile_definitions(
        micros
        PRIVATE
            MICROS_BUILD_SCHEDULER_TEST=1
    )
    target_sources(
        micros
        PRIVATE
            arch/riscv64/scheduler_test.S
            ${MICROS_SCHEDULER_QEMU_TEST_SOURCES}
    )
endif()

if(
    MICROS_BUILD_SCHEDULER_INVALID_OUTGOING_TEST
    OR MICROS_BUILD_SCHEDULER_INVALID_NEXT_TEST
)
    target_sources(
        micros
        PRIVATE
            arch/riscv64/scheduler_test.S
            ${MICROS_SCHEDULER_INVALID_QEMU_TEST_SOURCES}
    )
endif()

if(MICROS_BUILD_SCHEDULER_INVALID_OUTGOING_TEST)
    target_compile_definitions(
        micros
        PRIVATE
            MICROS_BUILD_SCHEDULER_INVALID_OUTGOING_TEST=1
    )
endif()

if(MICROS_BUILD_SCHEDULER_INVALID_NEXT_TEST)
    target_compile_definitions(
        micros
        PRIVATE
            MICROS_BUILD_SCHEDULER_INVALID_NEXT_TEST=1
    )
endif()

if(MICROS_BUILD_TRAP_TEST OR MICROS_BUILD_TRAP_PANIC_TEST)
    target_sources(micros PRIVATE arch/riscv64/trap_test.S)
endif()

target_compile_options(
    micros
    PRIVATE
        -march=rv64imac_zicsr_zifencei
        -mabi=lp64
        -mcmodel=medany
        -msmall-data-limit=0
        -mno-save-restore
        -ffunction-sections
        -fdata-sections
        -g3
        "$<$<COMPILE_LANGUAGE:C>:-std=c17;-ffreestanding;-fno-builtin;-fno-common;-fno-pic;-fno-pie;-fno-stack-protector;-fno-unwind-tables;-fno-asynchronous-unwind-tables;-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/=>"
        "$<$<COMPILE_LANGUAGE:C>:-Wall;-Wextra;-Werror;-Wpedantic;-Wmissing-prototypes;-Wstrict-prototypes>"
)

foreach(
    kernel_directory
    IN ITEMS
        boot
        diagnostic
        endpoint
        execution
        grant
        ipc
        memory
        objects
        platform
        pm
        scheduler
        trap
        tty
        vm
        tests/boot
        tests/endpoint
        tests/execution
        tests/grant
        tests/ipc
        tests/memory
        tests/objects
        tests/pm
        tests/ramfs
        tests/runtime
        tests/scheduler
        tests/spawn
        tests/trap
        tests/tty
        tests/vfs
        tests/vm
)
    target_compile_options(
        micros
        PRIVATE
            "$<$<COMPILE_LANGUAGE:C>:-fmacro-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/kernel/${kernel_directory}/=kernel/>"
    )
endforeach()

target_link_options(
    micros
    PRIVATE
        -march=rv64imac_zicsr_zifencei
        -mabi=lp64
        -mcmodel=medany
        -nostdlib
        -static
        "--ld-path=${MICROS_LD_LLD}"
        "-Wl,-T,${MICROS_LINKER_SCRIPT}"
        -Wl,--build-id=none
        -Wl,--fatal-warnings
        -Wl,--gc-sections
        -Wl,--no-relax
        "-Wl,-Map,${CMAKE_BINARY_DIR}/kernel/micros.map"
)

if(NOT CMAKE_NM OR NOT CMAKE_READELF)
    message(FATAL_ERROR "LLVM nm and readelf are required for ELF validation")
endif()

add_custom_command(
    TARGET micros
    POST_BUILD
    COMMAND
        "${MICROS_PYTHON}"
        -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_elf_sections.py"
        --readelf
        "${CMAKE_READELF}"
        --nm
        "${CMAKE_NM}"
        --elf
        "$<TARGET_FILE:micros>"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBATIM
)

add_custom_target(
    test-qemu-smoke
    COMMAND
        "${MICROS_PYTHON}"
        -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu_smoke.py"
        --qemu
        "${MICROS_QEMU}"
        --kernel
        "$<TARGET_FILE:micros>"
        --marker
        "MICROS_BOOT ${PROJECT_VERSION}"
        --marker
        "MICROS_FDT_MEMORY base=0x0000000080000000 size=0x0000000008000000"
        --marker
        "MICROS_FDT_READY"
        --marker
        "MICROS_TRAP_READY"
        --require-fdt-events
        --require-fdt-reservations
        --require-trap-ready
        --require-frame-allocator-ready
        --require-mmu-ready
        --require-frame-ownership-ready
        --require-objects-ready
        --timeout
        8
    DEPENDS micros
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    USES_TERMINAL
)

if(MICROS_BUILD_UART_CONSOLE_TEST)
    add_custom_target(
        test-qemu-uart-console
        COMMAND
            "${MICROS_PYTHON}"
            -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu_smoke.py"
            --qemu
            "${MICROS_QEMU}"
            --kernel
            "$<TARGET_FILE:micros>"
            --marker
            "MICROS_BOOT ${PROJECT_VERSION}"
            --marker
            "MICROS_FDT_READY"
            --marker
            "MICROS_TRAP_READY"
            --marker
            "MICROS_UART_CONSOLE_TEST begin=early"
            --marker
            "MICROS_UART_CONSOLE_TEST_PASS quiesce=drained ier=disabled ownership=tty panic=seized"
            --marker
            "MICROS_TTY_IRQ_TEST_PASS route=state-before-enable claim=source10 retained=in-service completion=explicit"
            --require-fdt-events
            --require-fdt-reservations
            --require-trap-ready
            --require-frame-allocator-ready
            --require-mmu-ready
            --require-frame-ownership-ready
            --require-objects-ready
            --timeout
            8
        DEPENDS micros
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )
endif()

if(MICROS_BUILD_PANIC_TEST)
    add_custom_target(
        test-qemu-panic
        COMMAND
            "${MICROS_PYTHON}"
            -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu_smoke.py"
            --qemu
            "${MICROS_QEMU}"
            --kernel
            "$<TARGET_FILE:micros>"
            --marker
            "MICROS_BOOT ${PROJECT_VERSION}"
            --marker
            "MICROS_FDT_MEMORY base=0x0000000080000000 size=0x0000000008000000"
            --marker
            "MICROS_FDT_READY"
            --marker
            "MICROS_TRAP_READY"
            --marker
            "MICROS_PANIC reason=intentional-test"
            --marker
            "MICROS_PANIC_BUILD version=${PROJECT_VERSION}"
            --marker
            "MICROS_PANIC_HART mode=S id=0x0000000000000000"
            --pattern
            "MICROS_PANIC_SOURCE file=kernel/boot_component_tests[.]c line=0x[0-9a-f]{16}"
            --pattern
            "MICROS_PANIC_MACHINE sstatus=0x[0-9a-f]{16} scause=0x[0-9a-f]{16} stval=0x[0-9a-f]{16} sepc=0x[0-9a-f]{16} ra=0x[0-9a-f]{16} sp=0x[0-9a-f]{16}"
            --expect
            panic
            --require-fdt-events
            --require-fdt-reservations
            --require-panic-report
            --require-trap-ready
            --require-frame-allocator-ready
            --require-mmu-ready
            --require-frame-ownership-ready
            --require-objects-ready
            --timeout
            8
        DEPENDS micros
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )
endif()

if(MICROS_BUILD_TRAP_TEST)
    add_custom_target(
        test-qemu-trap
        COMMAND
            "${MICROS_PYTHON}"
            -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu_smoke.py"
            --qemu
            "${MICROS_QEMU}"
            --kernel
            "$<TARGET_FILE:micros>"
            --marker
            "MICROS_BOOT ${PROJECT_VERSION}"
            --marker
            "MICROS_FDT_READY"
            --marker
            "MICROS_TRAP_READY"
            --marker
            "MICROS_TRAP_TEST_PASS origin=S cause=illegal-instruction registers=preserved hart-context=routed primary-stack=selected sscratch=anchor"
            --require-fdt-events
            --require-fdt-reservations
            --require-trap-test-report
            --require-frame-allocator-ready
            --require-mmu-ready
            --require-frame-ownership-ready
            --require-objects-ready
            --timeout
            8
        DEPENDS micros
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )
endif()

if(MICROS_BUILD_TIMER_TEST)
    add_custom_target(
        test-qemu-timer
        COMMAND
            "${MICROS_PYTHON}"
            -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu_smoke.py"
            --qemu
            "${MICROS_QEMU}"
            --kernel
            "$<TARGET_FILE:micros>"
            --marker
            "MICROS_BOOT ${PROJECT_VERSION}"
            --marker
            "MICROS_FDT_READY"
            --marker
            "MICROS_TRAP_READY"
            --marker
            "MICROS_TIMER_TEST_PASS ticks=0x0000000000000003 interval=0x00000000000186a0 active=0x0000000000000000 deadline=0xffffffffffffffff owner=hart"
            --require-fdt-events
            --require-fdt-reservations
            --require-trap-ready
            --require-timer-test-report
            --require-frame-allocator-ready
            --require-mmu-ready
            --require-frame-ownership-ready
            --require-objects-ready
            --timeout
            8
        DEPENDS micros
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )
endif()

if(MICROS_BUILD_FRAME_ALLOCATOR_TEST)
    add_custom_target(
        test-qemu-frame-allocator
        COMMAND
            "${MICROS_PYTHON}"
            -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu_smoke.py"
            --qemu
            "${MICROS_QEMU}"
            --kernel
            "$<TARGET_FILE:micros>"
            --memory
            "128M"
            --memory
            "256M"
            --expected-frame-allocator-managed-delta
            "0x8000"
            --marker
            "MICROS_BOOT ${PROJECT_VERSION}"
            --marker
            "MICROS_FDT_READY"
            --marker
            "MICROS_TRAP_READY"
            --marker
            "MICROS_FRAME_ALLOCATOR_TEST_PASS allocations=0x0000000000000004 reuse=lowest invariants=preserved"
            --require-fdt-events
            --require-fdt-reservations
            --require-trap-ready
            --require-frame-allocator-test-report
            --require-mmu-ready
            --require-frame-ownership-ready
            --require-objects-ready
            --timeout
            8
        DEPENDS micros
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )
endif()

if(MICROS_BUILD_TRAP_PANIC_TEST)
    add_custom_target(
        test-qemu-trap-panic
        COMMAND
            "${MICROS_PYTHON}"
            -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu_smoke.py"
            --qemu
            "${MICROS_QEMU}"
            --kernel
            "$<TARGET_FILE:micros>"
            --marker
            "MICROS_BOOT ${PROJECT_VERSION}"
            --marker
            "MICROS_FDT_READY"
            --marker
            "MICROS_TRAP_READY"
            --marker
            "MICROS_PANIC reason=unexpected-exception"
            --marker
            "MICROS_PANIC_BUILD version=${PROJECT_VERSION}"
            --marker
            "MICROS_PANIC_HART mode=S id=0x0000000000000000"
            --pattern
            "MICROS_PANIC_SOURCE file=kernel/trap[.]c line=0x[0-9a-f]{16}"
            --pattern
            "MICROS_PANIC_MACHINE sstatus=0x[0-9a-f]{16} scause=0x[0-9a-f]{16} stval=0x[0-9a-f]{16} sepc=0x[0-9a-f]{16} ra=0x[0-9a-f]{16} sp=0x[0-9a-f]{16}"
            --pattern
            "MICROS_TRAP_CONTEXT origin=S sstatus=0x[0-9a-f]{16} scause=0x0000000000000002 stval=0x[0-9a-f]{16} sepc=0x[0-9a-f]{16} ra=0x[0-9a-f]{16} sp=0x[0-9a-f]{16}"
            --expect
            panic
            --require-fdt-events
            --require-fdt-reservations
            --require-panic-report
            --require-trap-ready
            --require-frame-allocator-ready
            --require-mmu-ready
            --require-frame-ownership-ready
            --require-objects-ready
            --require-trap-context
            --nm
            "${CMAKE_NM}"
            --trap-context-sepc-symbol
            "micros_trap_panic_test_fault"
            --timeout
            8
        DEPENDS micros
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )
endif()

if(MICROS_BUILD_MMU_TEST)
    add_custom_target(
        test-qemu-mmu
        COMMAND
            "${MICROS_PYTHON}"
            -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu_smoke.py"
            --qemu
            "${MICROS_QEMU}"
            --kernel
            "$<TARGET_FILE:micros>"
            --marker
            "MICROS_BOOT ${PROJECT_VERSION}"
            --marker
            "MICROS_FDT_READY"
            --marker
            "MICROS_TRAP_READY"
            --marker
            "MICROS_MMU_TEST_PASS store-fault=text execute-fault=writable traps=0x0000000000000002"
            --marker
            "MICROS_TTY_CONTROLLER_TEST_PASS mapping=exact priority=disabled enable=disabled context=supervisor seie=independent"
            --require-fdt-events
            --require-fdt-reservations
            --require-trap-ready
            --require-frame-allocator-ready
            --require-mmu-ready
            --require-frame-ownership-ready
            --require-mmu-test-report
            --require-objects-ready
            --timeout
            8
        DEPENDS micros
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )
endif()

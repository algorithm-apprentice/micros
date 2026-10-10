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
    MICROS_BUILD_TTY_SERVICE_TEST
    MICROS_BUILD_TTY_SERVICE_IMAGE
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

if(MICROS_BUILD_HOST_TESTS)
    enable_testing()

    add_executable(
        fdt_test
        kernel/fdt.c
        tests/host/fdt_test.c
    )
    add_executable(
        frame_allocator_test
        kernel/frame_allocator.c
        tests/host/frame_allocator_test.c
    )
    add_executable(
        sv39_test
        kernel/sv39.c
        tests/host/sv39_test.c
    )
    add_executable(
        kernel_objects_test
        kernel/kernel_objects.c
        kernel/scheduler_core.c
        tests/host/kernel_objects_test.c
    )
    add_executable(
        frame_ownership_test
        kernel/frame_allocator.c
        kernel/frame_ownership.c
        kernel/kernel_objects.c
        kernel/scheduler_core.c
        tests/host/frame_ownership_test.c
    )
    add_executable(
        scheduler_test
        kernel/kernel_objects.c
        kernel/scheduler_core.c
        tests/host/ipc_current_guard_test.c
        tests/host/scheduler_model_test.c
        tests/host/scheduler_test.c
    )
    add_executable(
        endpoint_test
        kernel/kernel_objects.c
        kernel/scheduler_core.c
        kernel/endpoint.c
        kernel/grant.c
        kernel/grant_abi.c
        kernel/grant_copy_core.c
        kernel/grant_syscall_core.c
        kernel/user_address_space_core.c
        kernel/ipc_abi.c
        kernel/ipc.c
        tests/host/ipc_abi_test.c
        tests/host/ipc_call_test.c
        tests/host/ipc_close_test.c
        tests/host/ipc_deadlock_test.c
        tests/host/ipc_delivery_test.c
        tests/host/grant_test.c
        tests/host/grant_copy_test.c
        tests/host/grant_syscall_core_test.c
        tests/host/user_address_space_core_test.c
        tests/host/ipc_notify_test.c
        tests/host/ipc_operation_test.c
        tests/host/ipc_queue_test.c
        tests/host/ipc_queue_mutation_test.c
        tests/host/ipc_reply_test.c
        tests/host/ipc_state_test.c
        tests/host/endpoint_model_test.c
        tests/host/endpoint_test.c
    )
    add_executable(
        ipc_model_test
        kernel/kernel_objects.c
        kernel/scheduler_core.c
        kernel/endpoint.c
        kernel/grant.c
        kernel/grant_copy_core.c
        kernel/ipc.c
        tests/host/endpoint_model_test.c
        tests/host/grant_test.c
        tests/host/grant_copy_test.c
        tests/host/ipc_close_test.c
        tests/host/ipc_model_main.c
        tests/host/ipc_model_test.c
        tests/host/ipc_notify_test.c
    )
    add_executable(
        runtime_test
        lib/runtime/memory.c
        lib/runtime/runtime.c
        tests/qemu/bootstrap_launcher_control.c
        tests/host/runtime_test.c
    )
    add_executable(
        bootstrap_test
        kernel/bootstrap_manifest.c
        tests/host/bootstrap_manifest_test.c
    )
    add_executable(
        bootstrap_control_test
        kernel/bootstrap_control.c
        kernel/bootstrap_control_core.c
        kernel/bootstrap_image.c
        kernel/bootstrap_manifest.c
        kernel/endpoint.c
        kernel/ipc.c
        kernel/kernel_objects.c
        kernel/scheduler_core.c
        tests/host/bootstrap_control_test.c
    )
    add_executable(
        vm_bootstrap_test
        kernel/frame_allocator.c
        kernel/frame_ownership.c
        kernel/vm_bootstrap.c
        kernel/vm_handoff_core.c
        tests/host/vm_bootstrap_test.c
        tests/host/vm_handoff_test.c
    )
    add_executable(
        pm_test
        kernel/kernel_objects.c
        kernel/pm_control_core.c
        kernel/pm_control_syscall_core.c
        kernel/scheduler_core.c
        servers/pm/pm_core.c
        tests/host/pm_control_test.c
        tests/host/pm_test.c
    )
    add_executable(
        tty_test
        kernel/plic_core.c
        kernel/tty_control_core.c
        kernel/tty_control_syscall_core.c
        kernel/tty_handoff_core.c
        kernel/tty_handoff_runtime.c
        kernel/uart_console_core.c
        servers/tty/tty_core.c
        tests/host/plic_test.c
        tests/host/tty_control_test.c
        tests/host/tty_handoff_test.c
        tests/host/tty_test.c
    )
    add_executable(
        tty_service_test
        servers/tty/tty_core.c
        servers/tty/tty_service_core.c
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
        servers/tty/tty_service_core.c
        servers/tty/tty_uart.c
        tests/host/tty_model_test.c
    )
    add_executable(
        tty_service_control_test
        servers/tty/tty_control.c
        tests/host/tty_service_control_test.c
    )
    add_executable(
        vm_handoff_syscall_test
        kernel/tty_handoff_core.c
        kernel/vm_handoff_core.c
        kernel/vm_handoff_syscall.c
        tests/host/vm_handoff_syscall_test.c
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
        tests/host/tty_control_syscall_test.c
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
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_include_directories(
        frame_ownership_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_include_directories(
        scheduler_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_include_directories(
        endpoint_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_include_directories(
        ipc_model_test
        PRIVATE
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

    add_test(NAME fdt-parser COMMAND fdt_test)
    add_test(NAME frame-allocator COMMAND frame_allocator_test)
    add_test(NAME sv39-encoding COMMAND sv39_test)
    add_test(NAME kernel-objects COMMAND kernel_objects_test)
    add_test(NAME frame-ownership COMMAND frame_ownership_test)
    add_test(NAME scheduler-core COMMAND scheduler_test)
    add_test(NAME endpoint-profile COMMAND endpoint_test)
    add_test(NAME ipc-model COMMAND ipc_model_test)
    add_test(NAME user-runtime COMMAND runtime_test)
    add_test(NAME bootstrap-manifest COMMAND bootstrap_test)
    add_test(NAME bootstrap-control COMMAND bootstrap_control_test)
    add_test(NAME vm-bootstrap COMMAND vm_bootstrap_test)
    add_test(NAME pm-lifecycle COMMAND pm_test)
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
            bootstrap_test
            bootstrap_control_test
            vm_bootstrap_test
            pm_test
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
            bootstrap_test
            bootstrap_control_test
            vm_bootstrap_test
            pm_test
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

if(MICROS_BUILD_USER_RUNTIME_TEST)
    add_executable(
        micros_user_runtime_service
        lib/runtime/memory.c
        lib/runtime/memory_symbols.c
        lib/runtime/raw_syscall.S
        lib/runtime/runtime.c
        lib/runtime/start.S
        tests/qemu/user_runtime_probe.S
        tests/qemu/user_runtime_service.c
    )
    set_target_properties(
        micros_user_runtime_service
        PROPERTIES
            OUTPUT_NAME micros-user-runtime-service
            SUFFIX ".elf"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/user-runtime"
            LINK_DEPENDS
                "${MICROS_USER_LINKER_SCRIPT};${CMAKE_CURRENT_SOURCE_DIR}/tools/check_user_elf.py"
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
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_user_elf.py"
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
    OR MICROS_BUILD_TTY_SERVICE_TEST
    OR MICROS_BUILD_TTY_SERVICE_IMAGE
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
                    "${MICROS_USER_LINKER_SCRIPT};${CMAKE_CURRENT_SOURCE_DIR}/tools/check_user_elf.py"
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
        add_custom_command(
            TARGET "${target}"
            POST_BUILD
            COMMAND
                "${MICROS_PYTHON}"
                -B
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_user_elf.py"
                --elf
                "$<TARGET_FILE:${target}>"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            VERBATIM
        )
    endfunction()

    if(
        MICROS_BUILD_TTY_SERVICE_IMAGE
        OR MICROS_BUILD_TTY_SERVICE_TEST
    )
        micros_add_bootstrap_user(
            micros_tty_service
            micros-tty-service
            servers/tty/tty_control.c
            servers/tty/tty_core.c
            servers/tty/tty_service.c
            servers/tty/tty_service_core.c
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

    micros_add_bootstrap_user(
        micros_bootstrap_launcher
        micros-bootstrap-launcher
        tests/qemu/bootstrap_launcher_control.c
        tests/qemu/bootstrap_launcher.c
    )
    if(
        MICROS_BUILD_VM_HANDOFF_TEST
        OR MICROS_BUILD_PM_SERVICE_TEST
        OR MICROS_BUILD_TTY_SERVICE_TEST
        OR MICROS_BUILD_VM_READY_EARLY_TEST
        OR MICROS_BUILD_VM_SELF_FAULT_TEST
        OR MICROS_BUILD_VM_SELF_FAULT_SEALED_TEST
    )
        micros_add_bootstrap_user(
            micros_vm_server
            micros-vm-server
            tests/qemu/vm_self_fault.S
            tests/qemu/vm_server.c
        )
        if(MICROS_BUILD_TTY_SERVICE_TEST)
            target_compile_definitions(
                micros_vm_server
                PRIVATE
                    MICROS_TTY_HANDOFF_TEST=1
            )
        elseif(MICROS_BUILD_PM_SERVICE_TEST)
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
        if(MICROS_BUILD_PM_SERVICE_TEST)
            micros_add_bootstrap_user(
                micros_pm_service
                micros-pm-service
                servers/pm/pm_control.c
                servers/pm/pm_core.c
                servers/pm/pm_service.c
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
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_user_elf.py"
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
                servers/pm/pm_core.c
                servers/pm/pm_service.c
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
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_user_elf.py"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_bootstrap_fixture.py"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_tty_service_fixture.py"
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                VERBATIM
            )
            set_source_files_properties(
                "${MICROS_TTY_SERVICE_FIXTURE_SOURCE}"
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
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_user_elf.py"
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
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_user_elf.py"
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
    kernel/address_space.c
    kernel/bootstrap_control.c
    kernel/bootstrap_control_core.c
    kernel/bootstrap_image.c
    kernel/bootstrap_manifest.c
    kernel/bootstrap_memory.c
    kernel/bootstrap_runtime.c
    kernel/bootstrap_syscall.c
    kernel/endpoint.c
    kernel/fdt.c
    kernel/frame_allocator.c
    kernel/frame_ownership.c
    kernel/grant.c
    kernel/grant_abi.c
    kernel/grant_copy.c
    kernel/grant_copy_core.c
    kernel/grant_syscall_core.c
    kernel/grant_syscall.c
    kernel/grant_runtime.c
    kernel/ipc_abi.c
    kernel/ipc.c
    kernel/ipc_buffer.c
    kernel/ipc_runtime.c
    kernel/ipc_syscall.c
    kernel/syscall.c
    kernel/kernel_object_runtime.c
    kernel/kernel_objects.c
    kernel/scheduler_core.c
    kernel/scheduler.c
    kernel/main.c
    kernel/panic.c
    kernel/plic.c
    kernel/plic_core.c
    kernel/pm_control_core.c
    kernel/pm_control_runtime.c
    kernel/pm_control_syscall.c
    kernel/pm_control_syscall_core.c
    kernel/sv39.c
    kernel/timer.c
    kernel/trap.c
    kernel/trap_route_core.c
    kernel/tty_control_core.c
    kernel/tty_control_syscall.c
    kernel/tty_control_syscall_core.c
    kernel/tty_fault.c
    kernel/tty_handoff_core.c
    kernel/tty_handoff_runtime.c
    kernel/tty_interrupt.c
    kernel/uart_console_core.c
    kernel/user_address_space.c
    kernel/user_address_space_core.c
    kernel/user_execution.c
    kernel/vm_bootstrap.c
    kernel/vm_handoff_core.c
    kernel/vm_handoff_runtime.c
    kernel/vm_handoff_syscall.c
    kernel/vm_snapshot.c
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
            kernel/user_runtime_test.c
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
            kernel/pm_service_test.c
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
            "MICROS_PM_SERVICE_TEST_PASS handoff=complete readiness=acknowledged protocol=stable sealed=received reserve=aborted resources=clean generation=advanced transaction=advanced"
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
            kernel/grant_syscall_test.c
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
            8
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
            kernel/object_model_test.c
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
            kernel/endpoint_test.c
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
            kernel/ipc_test.c
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
            kernel/ipc_ecall_test.c
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
            kernel/ipc_syscall_test.c
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
            kernel/frame_ownership_test.c
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
            kernel/user_address_space_test.c
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
            kernel/address_space_handoff_test.c
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
            kernel/user_execution_test.c
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
            kernel/scheduler_test.c
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
            kernel/scheduler_invalid_test.c
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
            "MICROS_PANIC_SOURCE file=kernel/main[.]c line=0x[0-9a-f]{16}"
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

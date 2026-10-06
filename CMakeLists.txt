cmake_minimum_required(VERSION 3.25)

project(micros VERSION 0.1.0 LANGUAGES C)

option(MICROS_BUILD_HOST_TESTS "Build native host tests" OFF)
option(MICROS_BUILD_PANIC_TEST "Build the intentional panic image" OFF)
option(MICROS_BUILD_TRAP_TEST "Build the trap recovery test image" OFF)
option(MICROS_BUILD_TIMER_TEST "Build the timer interrupt test image" OFF)
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
    MICROS_BUILD_FRAME_ALLOCATOR_TEST
    MICROS_BUILD_TRAP_PANIC_TEST
    MICROS_BUILD_MMU_TEST
    MICROS_BUILD_OBJECT_MODEL_TEST
    MICROS_BUILD_NESTED_TRAP_TEST
    MICROS_BUILD_FRAME_OWNERSHIP_TEST
    MICROS_BUILD_USER_ADDRESS_SPACE_TEST
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
        tests/host/scheduler_model_test.c
        tests/host/scheduler_test.c
    )
    add_executable(
        endpoint_test
        kernel/kernel_objects.c
        kernel/scheduler_core.c
        kernel/endpoint.c
        tests/host/endpoint_test.c
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
    target_compile_features(fdt_test PRIVATE c_std_17)
    target_compile_features(frame_allocator_test PRIVATE c_std_17)
    target_compile_features(sv39_test PRIVATE c_std_17)
    target_compile_features(kernel_objects_test PRIVATE c_std_17)
    target_compile_features(frame_ownership_test PRIVATE c_std_17)
    target_compile_features(scheduler_test PRIVATE c_std_17)
    target_compile_features(endpoint_test PRIVATE c_std_17)
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
    endif()

    add_test(NAME fdt-parser COMMAND fdt_test)
    add_test(NAME frame-allocator COMMAND frame_allocator_test)
    add_test(NAME sv39-encoding COMMAND sv39_test)
    add_test(NAME kernel-objects COMMAND kernel_objects_test)
    add_test(NAME frame-ownership COMMAND frame_ownership_test)
    add_test(NAME scheduler-core COMMAND scheduler_test)
    add_test(NAME endpoint-profile COMMAND endpoint_test)

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
    kernel/bootstrap_memory.c
    kernel/endpoint.c
    kernel/fdt.c
    kernel/frame_allocator.c
    kernel/frame_ownership.c
    kernel/kernel_object_runtime.c
    kernel/kernel_objects.c
    kernel/scheduler_core.c
    kernel/scheduler.c
    kernel/main.c
    kernel/panic.c
    kernel/sv39.c
    kernel/timer.c
    kernel/trap.c
    kernel/user_address_space.c
    kernel/user_execution.c
)

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
            "MICROS_SCHEDULER_TEST_PASS queues=minix-priority current=reachable accounting=separate switches=alternating idle=resumed registers=preserved"
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

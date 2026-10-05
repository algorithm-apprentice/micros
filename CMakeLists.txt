cmake_minimum_required(VERSION 3.25)

project(micros VERSION 0.1.0 LANGUAGES C)

option(MICROS_BUILD_HOST_TESTS "Build native host tests" OFF)
option(MICROS_BUILD_PANIC_TEST "Build the intentional panic image" OFF)
option(MICROS_BUILD_TRAP_TEST "Build the trap recovery test image" OFF)
option(MICROS_BUILD_TIMER_TEST "Build the timer interrupt test image" OFF)
option(
    MICROS_BUILD_TRAP_PANIC_TEST
    "Build the unexpected-trap panic test image"
    OFF
)
option(MICROS_ENABLE_SANITIZERS "Enable host-test sanitizers" ON)

if(
    (MICROS_BUILD_PANIC_TEST AND MICROS_BUILD_TRAP_TEST)
    OR (MICROS_BUILD_PANIC_TEST AND MICROS_BUILD_TIMER_TEST)
    OR (MICROS_BUILD_PANIC_TEST AND MICROS_BUILD_TRAP_PANIC_TEST)
    OR (MICROS_BUILD_TRAP_TEST AND MICROS_BUILD_TIMER_TEST)
    OR (MICROS_BUILD_TRAP_TEST AND MICROS_BUILD_TRAP_PANIC_TEST)
    OR (MICROS_BUILD_TIMER_TEST AND MICROS_BUILD_TRAP_PANIC_TEST)
)
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
    target_include_directories(
        fdt_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_compile_features(fdt_test PRIVATE c_std_17)
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
    endif()

    add_test(NAME fdt-parser COMMAND fdt_test)

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
        DEPENDS fdt_test
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
    arch/riscv64/panic.S
    arch/riscv64/sbi.c
    arch/riscv64/trap.S
    arch/riscv64/uart.c
    kernel/fdt.c
    kernel/main.c
    kernel/panic.c
    kernel/trap.c
)

set_target_properties(
    micros
    PROPERTIES
        OUTPUT_NAME micros
        SUFFIX ".elf"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/kernel"
        LINK_DEPENDS "${MICROS_LINKER_SCRIPT}"
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

if(MICROS_BUILD_TRAP_PANIC_TEST)
    target_compile_definitions(
        micros
        PRIVATE
            MICROS_BUILD_TRAP_PANIC_TEST=1
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
            "MICROS_TRAP_TEST_PASS origin=S cause=illegal-instruction registers=preserved"
            --require-fdt-events
            --require-fdt-reservations
            --require-trap-test-report
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
            "MICROS_TIMER_TEST_PASS ticks=0x0000000000000003 interval=0x00000000000186a0"
            --require-fdt-events
            --require-fdt-reservations
            --require-trap-ready
            --require-timer-test-report
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

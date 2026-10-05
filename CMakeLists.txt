cmake_minimum_required(VERSION 3.25)

project(micros VERSION 0.1.0 LANGUAGES C)

option(MICROS_BUILD_HOST_TESTS "Build native host tests" OFF)
option(MICROS_BUILD_PANIC_TEST "Build the intentional panic image" OFF)
option(MICROS_ENABLE_SANITIZERS "Enable host-test sanitizers" ON)

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
    arch/riscv64/uart.c
    kernel/fdt.c
    kernel/main.c
    kernel/panic.c
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
        --require-fdt-events
        --require-fdt-reservations
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
            --timeout
            8
        DEPENDS micros
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
    )
endif()

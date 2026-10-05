cmake_minimum_required(VERSION 3.25)

project(micros VERSION 0.1.0 LANGUAGES C ASM)

if(NOT CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "micros target builds require the RISC-V toolchain file")
endif()

if(NOT MICROS_LD_LLD)
    message(FATAL_ERROR "MICROS_LD_LLD must name the target LLD executable")
endif()

find_program(MICROS_PYTHON NAMES python3 REQUIRED)
find_program(MICROS_QEMU NAMES qemu-system-riscv64 REQUIRED)

set(MICROS_LINKER_SCRIPT
    "${CMAKE_CURRENT_SOURCE_DIR}/arch/riscv64/kernel.ld"
)

add_executable(
    micros
    arch/riscv64/boot.S
    arch/riscv64/sbi.c
    arch/riscv64/uart.c
    kernel/main.c
)

set_target_properties(
    micros
    PROPERTIES
        OUTPUT_NAME micros
        SUFFIX ".elf"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/kernel"
        LINK_DEPENDS "${MICROS_LINKER_SCRIPT}"
)

target_include_directories(micros PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_compile_definitions(
    micros
    PRIVATE
        "MICROS_VERSION=\"${PROJECT_VERSION}\""
)

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
        "$<$<COMPILE_LANGUAGE:C>:-std=c17;-ffreestanding;-fno-builtin;-fno-common;-fno-pic;-fno-pie;-fno-stack-protector;-fno-unwind-tables;-fno-asynchronous-unwind-tables>"
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
        --timeout
        8
    DEPENDS micros
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    USES_TERMINAL
)

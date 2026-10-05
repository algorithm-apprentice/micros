set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv64)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(MICROS_TARGET_TRIPLE riscv64-unknown-elf)

if(DEFINED ENV{MICROS_CLANG} AND NOT "$ENV{MICROS_CLANG}" STREQUAL "")
    set(_micros_clang "$ENV{MICROS_CLANG}")
elseif(
    DEFINED ENV{MICROS_LLVM_ROOT}
    AND NOT "$ENV{MICROS_LLVM_ROOT}" STREQUAL ""
)
    set(_micros_clang "$ENV{MICROS_LLVM_ROOT}/bin/clang")
else()
    if(CMAKE_HOST_APPLE)
        find_program(_micros_brew NAMES brew)
        if(_micros_brew)
            execute_process(
                COMMAND "${_micros_brew}" --prefix llvm
                RESULT_VARIABLE _micros_brew_llvm_result
                OUTPUT_VARIABLE _micros_brew_llvm_root
                ERROR_QUIET
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            if(
                _micros_brew_llvm_result EQUAL 0
                AND EXISTS "${_micros_brew_llvm_root}/bin/clang"
            )
                set(_micros_clang "${_micros_brew_llvm_root}/bin/clang")
            endif()
        endif()
    endif()
    if(NOT _micros_clang)
        find_program(_micros_clang NAMES clang clang-23 clang-22)
    endif()
endif()

if(NOT _micros_clang OR NOT EXISTS "${_micros_clang}")
    message(
        FATAL_ERROR
        "Clang was not found. Set MICROS_CLANG or MICROS_LLVM_ROOT."
    )
endif()

if(DEFINED ENV{MICROS_LD_LLD} AND NOT "$ENV{MICROS_LD_LLD}" STREQUAL "")
    set(_micros_ld_lld "$ENV{MICROS_LD_LLD}")
elseif(
    DEFINED ENV{MICROS_LLD_ROOT}
    AND NOT "$ENV{MICROS_LLD_ROOT}" STREQUAL ""
)
    set(_micros_ld_lld "$ENV{MICROS_LLD_ROOT}/bin/ld.lld")
else()
    if(CMAKE_HOST_APPLE)
        if(NOT _micros_brew)
            find_program(_micros_brew NAMES brew)
        endif()
        if(_micros_brew)
            execute_process(
                COMMAND "${_micros_brew}" --prefix lld
                RESULT_VARIABLE _micros_brew_lld_result
                OUTPUT_VARIABLE _micros_brew_lld_root
                ERROR_QUIET
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            if(
                _micros_brew_lld_result EQUAL 0
                AND EXISTS "${_micros_brew_lld_root}/bin/ld.lld"
            )
                set(_micros_ld_lld "${_micros_brew_lld_root}/bin/ld.lld")
            endif()
        endif()
    endif()
    if(NOT _micros_ld_lld)
        find_program(_micros_ld_lld NAMES ld.lld ld.lld-23 ld.lld-22)
    endif()
endif()

if(NOT _micros_ld_lld OR NOT EXISTS "${_micros_ld_lld}")
    message(
        FATAL_ERROR
        "LLD was not found. Set MICROS_LD_LLD or MICROS_LLD_ROOT."
    )
endif()

get_filename_component(_micros_llvm_bin "${_micros_clang}" DIRECTORY)

set(CMAKE_C_COMPILER "${_micros_clang}" CACHE FILEPATH "Target C compiler")
set(CMAKE_ASM_COMPILER "${_micros_clang}" CACHE FILEPATH "Target assembler")
set(
    CMAKE_C_COMPILER_TARGET
    "${MICROS_TARGET_TRIPLE}"
    CACHE STRING
    "Target C compiler triple"
)
set(
    CMAKE_ASM_COMPILER_TARGET
    "${MICROS_TARGET_TRIPLE}"
    CACHE STRING
    "Target assembler triple"
)
set(
    MICROS_LD_LLD
    "${_micros_ld_lld}"
    CACHE FILEPATH
    "Target LLD executable"
)

if(EXISTS "${_micros_llvm_bin}/llvm-ar")
    set(
        CMAKE_AR
        "${_micros_llvm_bin}/llvm-ar"
        CACHE FILEPATH
        "Target archiver"
    )
endif()

if(EXISTS "${_micros_llvm_bin}/llvm-ranlib")
    set(
        CMAKE_RANLIB
        "${_micros_llvm_bin}/llvm-ranlib"
        CACHE FILEPATH
        "Target archive indexer"
    )
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

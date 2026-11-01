if(NOT CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "Stack metadata requires the RV64 target toolchain")
endif()
if(NOT MICROS_STACK_BUILD_MODE MATCHES "^(ordinary|metadata)$")
    message(FATAL_ERROR "Stack artifacts require an explicit ordinary or metadata mode")
endif()
if(MICROS_STACK_BUILD_MODE STREQUAL "metadata")
    add_compile_options("$<$<COMPILE_LANGUAGE:C>:-fstack-usage;-fstack-size-section>")
    if(NOT MICROS_STACK_PAIR_RECORD OR NOT EXISTS "${MICROS_STACK_PAIR_RECORD}")
        message(FATAL_ERROR "Stack metadata requires its sealed ordinary artifact pair")
    endif()
endif()
function(micros_stack_bind_leaf_boundaries)
    foreach(stack_leaf IN ITEMS micros_elf_spawn_init micros_elf_spawn_child micros_application_runtime_probe micros_application_minimal_probe)
        if(TARGET "${stack_leaf}")
            target_link_options("${stack_leaf}" PRIVATE
                "-Wl,-Map,$<TARGET_FILE_DIR:${stack_leaf}>/$<TARGET_FILE_BASE_NAME:${stack_leaf}>.map")
            if(MICROS_STACK_BUILD_MODE STREQUAL "metadata"
               AND NOT stack_leaf STREQUAL "micros_application_minimal_probe")
                add_custom_command(TARGET "${stack_leaf}" POST_BUILD
                    COMMAND "${MICROS_PYTHON}" ${MICROS_PYTHON_ARGS}
                        "${CMAKE_CURRENT_SOURCE_DIR}/tools/canonicalize_stack_metadata_elf.py"
                        --pair "${MICROS_STACK_PAIR_RECORD}" --leaf "${stack_leaf}"
                    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" VERBATIM)
            endif()
        endif()
    endforeach()
endfunction()
cmake_language(DEFER CALL micros_stack_bind_leaf_boundaries)

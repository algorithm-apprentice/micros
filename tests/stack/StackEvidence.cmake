add_custom_target(
    test-stack-evidence
    COMMAND "${MICROS_PYTHON}" ${MICROS_PYTHON_ARGS}
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_stack_evidence.py"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    USES_TERMINAL
)

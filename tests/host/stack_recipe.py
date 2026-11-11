"""Current stack workflow and host-only aggregate ownership."""

import json
from pathlib import Path

from tests.host.build_source_records import cmake_if_blocks


MODULE_PATH = "tests/stack/StackEvidence.cmake"
INCLUDE = '    include("${CMAKE_CURRENT_SOURCE_DIR}/tests/stack/StackEvidence.cmake")\n'
RECIPE = (
    "add_custom_target(\n"
    "    test-stack-evidence\n"
    '    COMMAND "${MICROS_PYTHON}" ${MICROS_PYTHON_ARGS}\n'
    '        "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_stack_evidence.py"\n'
    '    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"\n'
    "    USES_TERMINAL\n"
    ")\n"
)
BUILD_PRESET = {
    "name": "test-stack-evidence", "configurePreset": "host-debug", "targets": ["test-stack-evidence"],
}
WORKFLOW_PRESET = {
    "name": "test-stack-evidence", "displayName": "Certify current static and observed stack bounds",
    "steps": [
        {"type": "configure", "name": "host-debug"},
        {"type": "build", "name": "test-stack-evidence"},
    ],
}
COMMAND = ("cmake", "--workflow", "--preset", "test-stack-evidence")


def validate_stack_recipe(root):
    root = Path(root)
    source = (root / "CMakeLists.txt").read_text()
    host, = cmake_if_blocks(source, "MICROS_BUILD_HOST_TESTS")
    if source.count(MODULE_PATH) != 1 or host.count(INCLUDE) != 1:
        raise AssertionError("stack aggregate requires one current host-only include")
    if any((root / name).is_symlink() for name in ("tests", "tests/stack", MODULE_PATH)):
        raise AssertionError("stack recipe namespace is linked")
    if (root / MODULE_PATH).read_text() != RECIPE:
        raise AssertionError("stack aggregate differs from its independent recipe")


def validate_stack_workflow(root):
    root = Path(root)
    try:
        validate_stack_recipe(root)
        presets = json.loads((root / "CMakePresets.json").read_text())
        for kind, declaration in (("buildPresets", BUILD_PRESET), ("workflowPresets", WORKFLOW_PRESET)):
            rows = tuple(row for row in presets[kind] if row.get("name") == declaration["name"])
            if rows != (declaration,):
                raise ValueError("stack workflow differs from its complete closed recipe")
        if "cmake --workflow --preset test-stack-evidence" not in (root / "docs/testing-strategy.md").read_text():
            raise ValueError("stack workflow lacks its documented command")
    except (AssertionError, OSError, KeyError) as error:
        raise ValueError(f"invalid stack workflow registration: {error}") from error

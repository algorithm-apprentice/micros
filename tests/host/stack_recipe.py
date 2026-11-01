"""Finite EV03 workflow/recipe additions; historical authorities stay unchanged."""

import json
from pathlib import Path


MODULE_PATH = "tests/stack/StackEvidence.cmake"
INCLUDE = '    include("${CMAKE_CURRENT_SOURCE_DIR}/tests/stack/StackEvidence.cmake")\n'
ANCHOR = '    include("${CMAKE_CURRENT_SOURCE_DIR}/tests/work/WorkCounts.cmake")\n'
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
PLANNER_ADDITIONS = (
    ("from tests.host.stack_recipe import validate_stack_workflow\n", ""),
    ('STACK_EVIDENCE = ("cmake", "--workflow", "--preset", "test-stack-evidence")\n', ""),
    (
        "        commands.extend(workflow(name) for name in IMAGE_WORKFLOWS)\n"
        "        commands.append(STACK_EVIDENCE)\n",
        "        commands.extend(workflow(name) for name in IMAGE_WORKFLOWS)\n",
    ),
    (
        "    if workflows or image_workflows:\n"
        "        append_unique(commands, STACK_EVIDENCE)\n"
        "    commands.extend(diff_commands(base))\n",
        "    commands.extend(diff_commands(base))\n",
    ),
    (
        "def validate_inventory(root):\n    validate_stack_workflow(root)\n",
        "def validate_inventory(root):\n",
    ),
    (
        "        *SLOW_MODEL_INPUTS, *QEMU_WORKFLOWS, *IMAGE_WORKFLOWS, STACK_EVIDENCE[-1],\n",
        "        *SLOW_MODEL_INPUTS, *QEMU_WORKFLOWS, *IMAGE_WORKFLOWS,\n",
    ),
)


def project_root_recipe(source):
    if INCLUDE not in source:
        return source
    if source.count(INCLUDE) != 1 or source.count(INCLUDE + ANCHOR) != 1:
        raise AssertionError("stack include is not the exact host-only extension")
    return source.replace(INCLUDE, "", 1)


def project_presets(source):
    if '"name": "test-stack-evidence"' not in source:
        return source
    if source.count('"name": "test-stack-evidence"') != 3:
        raise AssertionError("stack preset extension has an undeclared occurrence")
    for declaration, ending in (
        (BUILD_PRESET, '\n  ],\n  "workflowPresets": ['),
        (WORKFLOW_PRESET, "\n  ]\n}\n"),
    ):
        block = "\n".join("    " + line for line in json.dumps(declaration, indent=2).splitlines())
        added = ",\n" + block + ending
        if source.count(added) != 1:
            raise AssertionError("stack presets are not the exact appended extension")
        source = source.replace(added, ending, 1)
    return source


def project_external_evidence(relative, data):
    if relative == "CMakePresets.json":
        return project_presets(data.decode("utf-8")).encode("utf-8")
    return data


def project_planner_registration(source):
    if PLANNER_ADDITIONS[0][0] not in source:
        return source
    for current, original in PLANNER_ADDITIONS:
        if source.count(current) != 1:
            raise AssertionError("stack planner registration is not the exact additive extension")
        source = source.replace(current, original, 1)
    return source


def project_command_extension(commands):
    if COMMAND not in commands:
        return commands
    if commands.count(COMMAND) != 1:
        raise AssertionError("duplicate stack gate cannot be projected")
    position = commands.index(COMMAND)
    suffix = commands[position + 1:]
    if suffix and tuple(suffix[0][1:]) == ("-E", "-S", "-B", "tools/check_docs.py"):
        suffix = suffix[1:]
    if (
        len(suffix) != 3
        or suffix[0] != ("git", "diff", "--cached", "--check")
        or suffix[1] != ("git", "diff", "--check")
        or len(suffix[2]) != 4 or suffix[2][:3] != ("git", "diff", "--check")
    ):
        raise AssertionError("relocated stack gate cannot be projected")
    return [command for command in commands if command != COMMAND]


def validate_stack_recipe(root):
    root = Path(root)
    source = (root / "CMakeLists.txt").read_text()
    if INCLUDE not in source:
        raise AssertionError("missing current stack aggregate include")
    project_root_recipe(source)
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

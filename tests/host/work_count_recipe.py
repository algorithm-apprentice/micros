"""Closed native-recipe artifact and historical root-recipe projection."""

import hashlib
from pathlib import Path

from tools.validation_inputs import UnsupportedContext


MODULE_PATH = "tests/work/WorkCounts.cmake"
INCLUDE = '    include("${CMAKE_CURRENT_SOURCE_DIR}/tests/work/WorkCounts.cmake")\n'
INCLUDE_ANCHOR = "    return()\nendif()\n\nif(NOT CMAKE_CROSSCOMPILING)"
TARGETS = ("work_counts_test", "work_observation_disabled_test")
NATIVE_COMMANDS = (
    ("add_executable", (
        "work_counts_test", "tests/work/work_counts.c",
        "tests/host/work_counts_test.c", "tests/host/work_counts_fixture.c",
    )),
    ("add_executable", (
        "work_observation_disabled_test",
        "tests/host/work_observation_disabled_test.c",
        "tests/host/work_counts_fixture.c",
    )),
    ("foreach", ("work_target", "IN", "ITEMS", *TARGETS)),
    ("target_include_directories", (
        "${work_target}", "PRIVATE", "${CMAKE_CURRENT_SOURCE_DIR}",
        "${CMAKE_CURRENT_SOURCE_DIR}/include",
    )),
    ("target_compile_features", ("${work_target}", "PRIVATE", "c_std_17")),
    ("target_compile_options", (
        "${work_target}", "PRIVATE", "-Wall", "-Wextra", "-Werror",
        "-Wpedantic", "-Wmissing-prototypes", "-Wstrict-prototypes",
    )),
    ("if", (
        "MICROS_ENABLE_SANITIZERS", "AND", "CMAKE_C_COMPILER_ID",
        "MATCHES", "Clang|GNU",
    )),
    ("target_compile_options", (
        "${work_target}", "PRIVATE", "-fno-omit-frame-pointer",
        "-fsanitize=address,undefined",
    )),
    ("target_link_options", (
        "${work_target}", "PRIVATE", "-fsanitize=address,undefined",
    )),
    ("endif", ()),
    ("endforeach", ()),
    ("target_compile_definitions", (
        "work_counts_test", "PRIVATE", "MICROS_TEST_WORK_COUNTS=1",
    )),
    ("add_test", ("NAME", "work-counts", "COMMAND", "work_counts_test")),
    ("add_test", (
        "NAME", "work-observation-disabled", "COMMAND",
        "work_observation_disabled_test",
    )),
    ("add_dependencies", ("test-unit-fast", *TARGETS)),
    ("add_dependencies", ("test-unit", *TARGETS)),
)


def render_native_recipe():
    lines = ["# Generated from the Accepted EV02 native recipe declaration."]
    depth = 0
    for command, arguments in NATIVE_COMMANDS:
        if command in ("endif", "endforeach"):
            depth -= 1
        values = [
            f'"{value}"' if "$" in value or "|" in value else value
            for value in arguments
        ]
        lines.append("    " * depth + command + "(" + " ".join(values) + ")")
        if command in ("if", "foreach"):
            depth += 1
    if depth != 0:
        raise AssertionError("unbalanced native recipe declaration")
    return "\n".join(lines) + "\n"


def project_context_registration(source):
    import_line = "from tests.host.work_count_recipe import projected_recipe_digest\n"
    if import_line not in source:
        return source
    checksum = hashlib.sha256(render_native_recipe().encode()).hexdigest()
    changes = (
        (import_line, ""),
        (f'    "{MODULE_PATH}": "{checksum}",\n', ""),
        (
            "            if projected_recipe_digest(root / name) != expected:\n",
            "            if _file_digest(root / name) != expected:\n",
        ),
    )
    for added, original in changes:
        if source.count(added) != 1:
            raise AssertionError("EV02 context registration is not exact")
        source = source.replace(added, original, 1)
    return source


def project_manifest_registration(source):
    import_line = "    from tests.host.work_count_recipe import native_test_declarations\n"
    if import_line not in source:
        return source
    changes = (
        (import_line, ""),
        ("    declared.extend(native_test_declarations(root))\n", ""),
    )
    for added, original in changes:
        if source.count(added) != 1:
            raise AssertionError("EV02 native inventory registration is not exact")
        source = source.replace(added, original, 1)
    return source


def project_external_evidence(relative, data):
    if relative == "tools/validation_build_manifest.py":
        return project_manifest_registration(data.decode("utf-8")).encode("utf-8")
    return data


def project_gate_compatibility(relative, source):
    if relative == "tests/host/test_ramfs_source_layout.py":
        changes = (
            ("import sys\n", ""),
            ('            "MICROS_PYTHON": sys.executable,\n', ""),
            (
                'patch("tools.validation_context.projected_recipe_digest", '
                'return_value="unreviewed-recipe-bytes")',
                'patch("tools.validation_context._file_digest", '
                'return_value="unreviewed-recipe-bytes")',
            ),
        )
    elif relative == "tests/host/test_validation_context_layout.py":
        changes = (
            ("from tests.host.work_count_recipe import project_external_evidence\n", ""),
            (
                '            source = project_external_evidence(\n'
                '                path, (ROOT / path).read_bytes()\n'
                '            ).decode("utf-8")\n'
                '            tree = ast.parse(source)\n',
                '            tree = ast.parse((ROOT / path).read_text())\n',
            ),
        )
    else:
        return source
    present = [source.count(added) for added, original in changes]
    if not any(present):
        return source
    if any(count != 1 for count in present):
        raise AssertionError("EV02 gate compatibility is not exact")
    for added, original in changes:
        source = source.replace(added, original, 1)
    return source


def project_root_recipe(source):
    if INCLUDE not in source:
        return source
    if source.count(INCLUDE) != 1 or source.count(INCLUDE + INCLUDE_ANCHOR) != 1:
        raise AssertionError("EV02 include is not the exact host-only extension")
    return source.replace(INCLUDE, "", 1)


def projected_recipe_digest(path):
    path = Path(path)
    try:
        if path.name != "CMakeLists.txt":
            return hashlib.sha256(path.read_bytes()).hexdigest()
        source = path.read_text(encoding="utf-8")
        if INCLUDE in source:
            validate_native_recipe(path.parent)
        return hashlib.sha256(
            project_root_recipe(source).encode("utf-8")
        ).hexdigest()
    except (AssertionError, OSError) as error:
        raise UnsupportedContext(f"unreviewed native recipe: {error}") from error


def validate_native_recipe(root):
    root = Path(root)
    module = root / MODULE_PATH
    if any((root / prefix).is_symlink() for prefix in ("tests", "tests/work", MODULE_PATH)):
        raise AssertionError("native recipe must not use a linked namespace")
    if module.read_text(encoding="utf-8") != render_native_recipe():
        raise AssertionError("native recipe differs from the independent declaration")


def native_test_declarations(root):
    root = Path(root)
    source = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    if INCLUDE not in source:
        return ()
    try:
        project_root_recipe(source)
        validate_native_recipe(root)
    except (AssertionError, OSError) as error:
        raise UnsupportedContext(f"unreviewed native recipe: {error}") from error
    return tuple(
        (arguments[1], arguments[3])
        for command, arguments in NATIVE_COMMANDS if command == "add_test"
    )

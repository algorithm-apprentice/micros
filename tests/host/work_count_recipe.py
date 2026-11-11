"""Current native work-observer recipe and host-runner ownership."""

from pathlib import Path

from tools.validation_inputs import UnsupportedContext


MODULE_PATH = "tests/work/WorkCounts.cmake"


INCLUDE = '    include("${CMAKE_CURRENT_SOURCE_DIR}/tests/work/WorkCounts.cmake")\n'


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


def _host_target_prefix(target, arguments):
    source = (
        "    add_custom_target(\n"
        f"        {target}\n"
        "        COMMAND\n"
        '            "${CMAKE_CTEST_COMMAND}"\n'
        "            --test-dir\n"
        '            "${CMAKE_BINARY_DIR}"\n'
        "            --output-on-failure\n"
    )
    if target == "test-unit-fast":
        source += "            -LE\n            slow\n"
    return source + (
        "        COMMAND\n"
        '            "${MICROS_PYTHON}"\n'
        "            ${MICROS_PYTHON_ARGS}\n"
        + "".join("            " + argument + "\n" for argument in arguments)
        + "        DEPENDS\n"
    )


def validate_host_test_commands(source):
    runner = '"${CMAKE_CURRENT_SOURCE_DIR}/tools/run_host_tests.py"'
    expected = tuple(
        _host_target_prefix(target, (runner, "--tier", tier))
        for target, tier in (("test-unit-fast", "fast"), ("test-unit", "full"))
    )
    if source.count("tools/run_host_tests.py") != 2 or any(source.count(command) != 1 for command in expected):
        raise AssertionError("current native host-test invocation is partial, duplicated or changed")


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
        if source.count(INCLUDE) != 1:
            raise AssertionError("native work recipe has duplicate includes")
        validate_native_recipe(root)
    except (AssertionError, OSError) as error:
        raise UnsupportedContext(f"unreviewed native recipe: {error}") from error
    return tuple(
        (arguments[1], arguments[3])
        for command, arguments in NATIVE_COMMANDS if command == "add_test"
    )

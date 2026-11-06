"""Closed native-recipe artifact and historical root-recipe projection."""

import hashlib
from pathlib import Path

from tests.host.stack_recipe import INCLUDE as STACK_INCLUDE, project_root_recipe as project_stack_recipe, validate_stack_recipe
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
    owner = "EV02"
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
    elif relative == "tests/host/test_trap_syscall_source_layout.py":
        owner = "COPY01"
        cold_import = "from tests.host.runtime_memory_authority import FIXTURE_PATH, memory_identity_required\n"
        scoped_import = (
            "from tests.host.runtime_memory_authority import FIXTURE_PATH\n"
            "from tests.host.evidence_authority_scope import memory_identity_required\n"
        )
        if cold_import in source and scoped_import in source:
            raise AssertionError("COPY01 source reader has mixed authority imports")
        changes = (
            (scoped_import if scoped_import in source else cold_import, ""),
            ("            FIXTURE_PATH,\n", ""),
            ("        memory_required = memory_identity_required(REPOSITORY_ROOT)\n", ""),
            (
                '            if path == "lib/runtime/memory.c" and not memory_required:\n'
                "                continue\n",
                "",
            ),
        )
    else:
        return source
    present = [source.count(added) for added, original in changes]
    if not any(present):
        return source
    if any(count != 1 for count in present):
        raise AssertionError(f"{owner} gate compatibility is not exact")
    for added, original in changes:
        source = source.replace(added, original, 1)
    return source


def project_streaming_execution(source):
    streaming_import = "from tools.validation_stream import stream_workflow\n"
    if streaming_import not in source:
        return source
    changes = (
        (streaming_import, ""),
        (
            '        log=attempt.recorded_log,\n'
            '    )\n\n\ndef _fresh_workflow(',
            '        log=attempt.recorded_log,\n'
            '    )\n'
            '    store.replay_log(attempt.recorded_log, sys.stdout)\n'
            '\n\ndef _fresh_workflow(',
        ),
        (
            '    completed = stream_workflow(\n'
            '        tuple(entry["command"]), cwd=root, log=attempt.log,\n'
            '    )\n',
            '    completed = subprocess.run(\n'
            '        tuple(entry["command"]), cwd=root, check=False,\n'
            '        stdout=attempt.log, stderr=subprocess.STDOUT,\n'
            '    )\n',
        ),
    )
    for current, previous in changes:
        if source.count(current) != 1:
            raise AssertionError("live workflow output transport is not exact")
        source = source.replace(current, previous, 1)
    return source


def project_feedback_reader(relative, source):
    consumers = {
        "tests/host/test_kernel_directory_layout.py": "kernel_mapping_authority",
        "tests/host/test_grant_data_plane_source_layout.py": "diagnostic_history_authority",
        "tests/host/test_kernel_object_source_layout.py": "diagnostic_history_authority",
        "tests/host/test_tty_source_layout.py": "diagnostic_history_authority",
    }
    if relative in consumers:
        current = "from tests.host.evidence_authority_scope import identity_required as diagnostic_identity_required\n"
        previous = f"from tests.host.{consumers[relative]} import identity_required as diagnostic_identity_required\n"
        if "tests.host.evidence_authority_scope" not in source:
            return source
        if source.count(current) != 1 or previous in source:
            raise AssertionError("source assertion has partial, mixed or duplicate view imports")
        return source.replace(current, previous, 1)
    if relative != "tests/host/kernel_layout_source.py":
        return source
    scoped_import = (
        "from tests.host.evidence_authority_scope import "
        "identity_required as diagnostic_identity_required, source_bytes\n"
    )
    if not any(marker in source for marker in (
        scoped_import, "source_bytes(root, selected)", "project_streaming_execution",
    )):
        return source
    changes = (
        (
            scoped_import,
            "from tests.host.kernel_mapping_authority import identity_required as diagnostic_identity_required\n",
        ),
        (
            "    project_gate_compatibility, project_root_recipe, project_streaming_execution,\n",
            "    project_gate_compatibility, project_root_recipe,\n",
        ),
        (
            "    raw = project_terminal_source_bytes(KERNEL_LOGICAL_TO_PHYSICAL[logical], source_bytes(root, selected))\n",
            "    raw = project_terminal_source_bytes(KERNEL_LOGICAL_TO_PHYSICAL[logical], selected.read_bytes())\n",
        ),
        (
            "        project_terminal_source_bytes(relative, source_bytes(REPOSITORY_ROOT, path)),\n",
            "        project_terminal_source_bytes(relative, path.read_bytes()),\n",
        ),
        ("        source_bytes(REPOSITORY_ROOT, path),\n", "        path.read_bytes(),\n"),
        ("    source = project_streaming_execution(source)\n", ""),
        (
            "    data = source_bytes(REPOSITORY_ROOT, path) if relative is not None else path.read_bytes()\n",
            "",
        ),
        ("project_trace_source_bytes(relative, data)", "project_trace_source_bytes(relative, path.read_bytes())"),
    )
    for name in ("restore_cmake_source", "restore_validation_plan_source"):
        changes += ((
            f'        return {name}(data.decode("utf-8")).encode()\n',
            f'        return {name}(path.read_text(encoding="utf-8")).encode()\n',
        ),)
    for name in (
        "restore_validation_context_source", "restore_validation_build_impact_source",
        "restore_validation_inputs_source", "restore_validation_execution_source",
    ):
        changes += ((
            f'        return {name}(\n            data.decode("utf-8")\n',
            f'        return {name}(\n            path.read_text(encoding="utf-8")\n',
        ),)
    for current, previous in changes:
        if source.count(current) != 1:
            raise AssertionError("checked source-reader transport is partial, duplicated or changed")
        source = source.replace(current, previous, 1)
    return source


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


def project_host_test_commands(source, *, require_current=False):
    runner = '"${CMAKE_CURRENT_SOURCE_DIR}/tools/run_host_tests.py"'
    if "tools/run_host_tests.py" not in source:
        if require_current:
            raise AssertionError("current native targets require the distinct host-test runner")
        return source
    old_arguments = ("-m", "unittest", "discover", "-s", "tests/host", "-p", '"test_*.py"')
    replacements = tuple(
        (
            _host_target_prefix(target, (runner, "--tier", tier)),
            _host_target_prefix(target, old_arguments),
        )
        for target, tier in (("test-unit-fast", "fast"), ("test-unit", "full"))
    )
    if source.count("tools/run_host_tests.py") != 2 or any(
        source.count(current) != 1 or previous in source
        for current, previous in replacements
    ):
        raise AssertionError("native host-test invocation is partial, mixed, duplicated or changed")
    for current, previous in replacements:
        source = source.replace(current, previous, 1)
    return source


def project_root_recipe(source):
    source = project_host_test_commands(source)
    source = project_stack_recipe(source)
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
        project_host_test_commands(source, require_current=True)
        if STACK_INCLUDE in source:
            validate_stack_recipe(path.parent)
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

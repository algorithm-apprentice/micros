from __future__ import annotations

import ast
from pathlib import Path
import re
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
CMAKE_PATH = REPOSITORY_ROOT / "CMakeLists.txt"
FUNCTION_REVIEW_LINE_LIMIT = 120

SOURCE_SETS = {
    "MICROS_VFS_TEST_FIXTURE_SOURCES": (
        "tests/host/vfs_test_fixture.c",
        "tests/host/vfs_test_fixture_grant.c",
        "tests/host/vfs_test_fixture_ramfs.c",
        "tests/host/vfs_test_fixture_tty.c",
    ),
    "MICROS_VFS_CORE_TEST_SOURCES": (
        "tests/host/vfs_test.c",
        "tests/host/vfs_core_test_fixture.c",
        "tests/host/vfs_core_state_test.c",
        "tests/host/vfs_core_process_test.c",
        "tests/host/vfs_core_namespace_test.c",
        "tests/host/vfs_core_io_test.c",
        "tests/host/vfs_core_directory_test.c",
        "tests/host/vfs_core_console_test.c",
    ),
    "MICROS_VFS_SPAWN_TEST_SOURCES": (
        "tests/host/vfs_spawn_test.c",
        "tests/host/vfs_spawn_test_fixture.c",
        "tests/host/vfs_spawn_image_test.c",
        "tests/host/vfs_spawn_descriptor_test.c",
        "tests/host/vfs_spawn_publication_test.c",
    ),
    "MICROS_VFS_MODEL_TEST_SOURCES": (
        "tests/host/vfs_model_test.c",
        "tests/host/vfs_model_context.c",
        "tests/host/vfs_model_reference.c",
        "tests/host/vfs_model_compare.c",
        "tests/host/vfs_model_process_operations.c",
        "tests/host/vfs_model_file_operations.c",
        "tests/host/vfs_model_console_operations.c",
        "tests/host/vfs_model_pressure.c",
        "tests/host/vfs_model_trace.c",
    ),
    "MICROS_VFS_SERVICE_TEST_SOURCES": (
        "tests/host/vfs_service_test.c",
        "tests/host/vfs_service_test_fixture.c",
        "tests/host/vfs_service_application_protocol_test.c",
        "tests/host/vfs_service_backend_protocol_test.c",
        "tests/host/vfs_service_pm_control_protocol_test.c",
        "tests/host/vfs_service_configuration_test.c",
        "tests/host/vfs_service_dispatch_test.c",
    ),
    "MICROS_VFS_PROBE_SOURCES": (
        "tests/qemu/vfs_service_probe.c",
        "tests/qemu/vfs_service_probe_client.c",
        "tests/qemu/vfs_service_probe_filesystem.c",
        "tests/qemu/vfs_service_probe_console.c",
    ),
}

PRIVATE_HEADERS = (
    "tests/host/vfs_test_fixture_internal.h",
    "tests/host/vfs_test_suites.h",
    "tests/host/vfs_core_test_fixture.h",
    "tests/host/vfs_spawn_test_fixture.h",
    "tests/host/vfs_service_test_suites.h",
    "tests/host/vfs_service_test_fixture.h",
    "tests/host/vfs_model_internal.h",
    "tests/qemu/vfs_service_probe_internal.h",
)

INITIAL_MIGRATION_ALLOWLIST = (
    ("tests/host/vfs_test_fixture.c", "fixture_lookup"),
    ("tests/host/vfs_test_fixture.c", "fixture_tty_call"),
    (
        "tests/host/vfs_test.c",
        "mount_attach_share_and_detach_are_atomic",
    ),
    (
        "tests/host/vfs_test.c",
        "open_read_close_and_validation_are_exact",
    ),
    (
        "tests/host/vfs_test.c",
        "backend_reply_validation_is_operation_specific",
    ),
    (
        "tests/host/vfs_test.c",
        "create_write_mkdir_and_chdir_are_exact",
    ),
    (
        "tests/host/vfs_test.c",
        "getdents_translation_and_cursor_are_exact",
    ),
    (
        "tests/host/vfs_test.c",
        "console_async_and_cleanup_are_exact",
    ),
    (
        "tests/host/vfs_service_test.c",
        "pm_control_requests_are_exact",
    ),
    (
        "tests/host/vfs_service_test.c",
        "pm_control_results_are_exact",
    ),
    (
        "tests/host/vfs_service_test.c",
        "backend_protocol_is_exact",
    ),
    (
        "tests/host/vfs_service_test.c",
        "service_dispatches_pm_control",
    ),
    ("tests/host/vfs_model_test.c", "run_vnode_pressure"),
    ("tests/host/vfs_model_test.c", "execute_step"),
    (
        "tests/host/vfs_spawn_test.c",
        "successful_prepare_and_release_are_exact",
    ),
    (
        "tests/host/vfs_spawn_test.c",
        "public_descriptor_prepare_and_abort_are_exact",
    ),
    (
        "tests/host/vfs_spawn_test.c",
        "reserved_process_slot_and_capacity_are_exact",
    ),
    (
        "tests/host/vfs_spawn_test.c",
        "expected_prepare_failures_roll_back",
    ),
    (
        "tests/host/vfs_spawn_test.c",
        "trusted_init_and_path_bounds_are_exact",
    ),
    (
        "tests/host/vfs_spawn_test.c",
        "prepared_snapshot_is_independent_from_vnode_size",
    ),
    ("tests/qemu/vfs_service_probe.c", "probe_test_filesystem"),
    (
        "kernel/vfs_service_test.c",
        "micros_vfs_service_test_handle_trap",
    ),
    ("tools/generate_vfs_service_fixture.py", "render_fixture"),
)

MIGRATION_ALLOWLIST = INITIAL_MIGRATION_ALLOWLIST[22:]


def cmake_command(text: str, command: str, first_argument: str) -> str:
    match = re.search(
        rf"\b{re.escape(command)}\s*\(\s*{re.escape(first_argument)}\b",
        text,
    )
    if match is None:
        raise AssertionError(
            f"missing {command} command for {first_argument}"
        )

    open_index = text.find("(", match.start())
    depth = 0
    for index in range(open_index, len(text)):
        if text[index] == "(":
            depth += 1
        elif text[index] == ")":
            depth -= 1
            if depth == 0:
                return text[match.start() : index + 1]

    raise AssertionError(
        f"unterminated {command} command for {first_argument}"
    )


def c_lexical_text(text: str) -> str:
    output = list(text)
    index = 0

    while index < len(text):
        if text.startswith("//", index):
            end = text.find("\n", index + 2)
            end = len(text) if end < 0 else end
            for selected in range(index, end):
                output[selected] = " "
            index = end
            continue
        if text.startswith("/*", index):
            end = text.find("*/", index + 2)
            if end < 0:
                raise AssertionError("unterminated C comment")
            for selected in range(index, end + 2):
                if output[selected] != "\n":
                    output[selected] = " "
            index = end + 2
            continue
        if text[index] in ('"', "'"):
            quote = text[index]
            output[index] = " "
            index += 1
            while index < len(text):
                if text[index] == "\\":
                    output[index] = " "
                    if index + 1 < len(text):
                        if output[index + 1] != "\n":
                            output[index + 1] = " "
                    index += 2
                    continue
                if text[index] == quote:
                    output[index] = " "
                    index += 1
                    break
                if output[index] != "\n":
                    output[index] = " "
                index += 1
            else:
                raise AssertionError("unterminated C literal")
            continue
        index += 1

    return "".join(output)


def matching_open_parenthesis(text: str, close_index: int) -> int:
    depth = 0

    for index in range(close_index, -1, -1):
        if text[index] == ")":
            depth += 1
        elif text[index] == "(":
            depth -= 1
            if depth == 0:
                return index

    raise AssertionError("unterminated C parameter list")


def c_signature_start(text: str, segment_start: int, name_index: int) -> int:
    prefix = text[segment_start:name_index]
    lines = prefix.splitlines(keepends=True)
    offset = segment_start
    candidates = []

    for line in lines:
        stripped = line.strip()
        if stripped and not stripped.startswith("#"):
            candidates.append(offset)
        elif stripped.startswith("#") or not stripped:
            candidates = []
        offset += len(line)

    return candidates[0] if candidates else name_index


def c_function_definitions(text: str) -> dict[str, int]:
    lexical = c_lexical_text(text)
    definitions = {}
    depth = 0
    segment_start = 0
    index = 0

    while index < len(lexical):
        selected = lexical[index]
        if selected == "{" and depth == 0:
            declaration = lexical[segment_start:index].rstrip()
            close_parenthesis = len(declaration) - 1
            if close_parenthesis >= 0 and declaration.endswith(")"):
                open_parenthesis = matching_open_parenthesis(
                    declaration,
                    close_parenthesis,
                )
                before = declaration[:open_parenthesis].rstrip()
                match = re.search(
                    r"([A-Za-z_][A-Za-z0-9_]*)$",
                    before,
                )
                if match is not None and "=" not in declaration:
                    name = match.group(1)
                    if name not in {
                        "if",
                        "for",
                        "while",
                        "switch",
                    }:
                        body_depth = 1
                        body_end = index + 1
                        while body_end < len(lexical) and body_depth != 0:
                            if lexical[body_end] == "{":
                                body_depth += 1
                            elif lexical[body_end] == "}":
                                body_depth -= 1
                            body_end += 1
                        if body_depth != 0:
                            raise AssertionError(
                                f"unterminated body for {name}"
                            )
                        name_index = (
                            segment_start
                            + open_parenthesis
                            - len(match.group(0))
                        )
                        start = c_signature_start(
                            lexical,
                            segment_start,
                            name_index,
                        )
                        definitions[name] = (
                            text.count("\n", start, body_end - 1) + 1
                        )
                        index = body_end
                        segment_start = body_end
                        continue
            depth = 1
        elif selected == "{":
            depth += 1
        elif selected == "}":
            if depth == 0:
                raise AssertionError("unexpected C closing brace")
            depth -= 1
            if depth == 0:
                segment_start = index + 1
        elif selected == ";" and depth == 0:
            segment_start = index + 1
        index += 1

    return definitions


def python_function_definitions(text: str) -> dict[str, int]:
    tree = ast.parse(text)
    return {
        node.name: node.end_lineno - node.lineno + 1
        for node in ast.walk(tree)
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
        and node.end_lineno is not None
    }


def all_test_sources() -> tuple[str, ...]:
    return tuple(
        dict.fromkeys(
            source
            for sources in SOURCE_SETS.values()
            for source in sources
        )
    )


class VfsTestSourceLayoutTest(unittest.TestCase):
    def test_expected_sources_and_private_headers_exist(self) -> None:
        required = (*all_test_sources(), *PRIVATE_HEADERS)
        missing = [
            path
            for path in required
            if not (REPOSITORY_ROOT / path).is_file()
        ]

        self.assertEqual([], missing)

    def test_cmake_source_sets_have_exact_ordered_membership(self) -> None:
        cmake = CMAKE_PATH.read_text(encoding="utf-8")

        for source_set_name, expected_sources in SOURCE_SETS.items():
            with self.subTest(source_set=source_set_name):
                source_set = cmake_command(
                    cmake,
                    "set",
                    source_set_name,
                )
                listed_sources = tuple(
                    re.findall(
                        r"(?:tests/host|tests/qemu)/"
                        r"[A-Za-z0-9_]+\.(?:c|S)",
                        source_set,
                    )
                )
                self.assertEqual(expected_sources, listed_sources)

        for source in all_test_sources():
            self.assertEqual(1, cmake.count(source), source)

    def test_native_consumers_use_exact_source_sets_once(self) -> None:
        cmake = CMAKE_PATH.read_text(encoding="utf-8")
        expected_sets = {
            "vfs_test": (
                "MICROS_VFS_TEST_FIXTURE_SOURCES",
                "MICROS_VFS_CORE_TEST_SOURCES",
                "MICROS_VFS_SPAWN_TEST_SOURCES",
            ),
            "vfs_model_test": (
                "MICROS_VFS_TEST_FIXTURE_SOURCES",
                "MICROS_VFS_MODEL_TEST_SOURCES",
            ),
            "vfs_service_test": (
                "MICROS_VFS_TEST_FIXTURE_SOURCES",
                "MICROS_VFS_SERVICE_TEST_SOURCES",
            ),
        }

        for target, selected_sets in expected_sets.items():
            with self.subTest(target=target):
                block = cmake_command(cmake, "add_executable", target)
                for source_set_name in SOURCE_SETS:
                    self.assertEqual(
                        int(source_set_name in selected_sets),
                        block.count(f"${{{source_set_name}}}"),
                        source_set_name,
                    )

    def test_probe_consumes_exact_probe_source_set(self) -> None:
        cmake = CMAKE_PATH.read_text(encoding="utf-8")
        block = cmake_command(
            cmake,
            "micros_add_bootstrap_user",
            "micros_vfs_service_probe",
        )

        self.assertEqual(
            1,
            block.count("${MICROS_VFS_PROBE_SOURCES}"),
        )
        self.assertEqual(
            1,
            block.count("tests/qemu/vfs_service_report.S"),
        )

    def test_private_headers_contain_declarations_only(self) -> None:
        definitions = {}

        for relative_path in PRIVATE_HEADERS:
            path = REPOSITORY_ROOT / relative_path
            if not path.is_file():
                continue
            selected = c_function_definitions(
                path.read_text(encoding="utf-8")
            )
            if selected:
                definitions[relative_path] = selected

        self.assertEqual({}, definitions)

    def test_production_sources_do_not_include_test_private_headers(
        self,
    ) -> None:
        violations = []

        for root in ("arch", "include", "kernel", "lib", "servers", "user"):
            for path in (REPOSITORY_ROOT / root).rglob("*"):
                if path.suffix not in {".c", ".h", ".S"}:
                    continue
                text = path.read_text(encoding="utf-8")
                for private_path in PRIVATE_HEADERS:
                    if private_path in text:
                        violations.append(
                            (
                                path.relative_to(REPOSITORY_ROOT).as_posix(),
                                private_path,
                            )
                        )

        self.assertEqual([], violations)

    def test_model_trace_draws_random_once_per_step(self) -> None:
        model_sources = SOURCE_SETS["MICROS_VFS_MODEL_TEST_SOURCES"]
        trace_path = "tests/host/vfs_model_trace.c"
        random_call = re.compile(r"\bmodel_random\s*\(")

        for relative_path in model_sources:
            if relative_path == "tests/host/vfs_model_context.c":
                continue
            text = (REPOSITORY_ROOT / relative_path).read_text(
                encoding="utf-8"
            )
            self.assertEqual(
                1 if relative_path == trace_path else 0,
                len(random_call.findall(text)),
                relative_path,
            )

    def test_migration_allowlist_matches_all_over_limit_functions(
        self,
    ) -> None:
        accepted = set(INITIAL_MIGRATION_ALLOWLIST)
        migration = set(MIGRATION_ALLOWLIST)
        oversized = {}
        c_paths = (
            *all_test_sources(),
            "kernel/vfs_service_test.c",
        )

        self.assertEqual(
            len(MIGRATION_ALLOWLIST),
            len(migration),
            "migration allowlist contains duplicates",
        )
        self.assertTrue(
            migration <= accepted,
            "migration allowlist may only shrink",
        )

        for relative_path in c_paths:
            path = REPOSITORY_ROOT / relative_path
            if not path.is_file():
                continue
            for function_name, line_count in c_function_definitions(
                path.read_text(encoding="utf-8")
            ).items():
                if line_count > FUNCTION_REVIEW_LINE_LIMIT:
                    oversized[(relative_path, function_name)] = line_count

        generator_path = "tools/generate_vfs_service_fixture.py"
        for function_name, line_count in python_function_definitions(
            (REPOSITORY_ROOT / generator_path).read_text(encoding="utf-8")
        ).items():
            if line_count > FUNCTION_REVIEW_LINE_LIMIT:
                oversized[(generator_path, function_name)] = line_count

        self.assertEqual(
            migration,
            set(oversized),
            oversized,
        )


if __name__ == "__main__":
    unittest.main()

from pathlib import Path
import re
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
CMAKE_PATH = REPOSITORY_ROOT / "CMakeLists.txt"
INTERNAL_HEADER_PATH = REPOSITORY_ROOT / "servers/vfs/vfs_internal.h"
PORTABLE_SOURCE_SET = "MICROS_VFS_PORTABLE_SOURCES"
PORTABLE_SOURCES = (
    "servers/vfs/vfs_core.c",
    "servers/vfs/vfs_state.c",
    "servers/vfs/vfs_ownership.c",
    "servers/vfs/vfs_transfer.c",
    "servers/vfs/vfs_namespace.c",
    "servers/vfs/vfs_io.c",
    "servers/vfs/vfs_console.c",
    "servers/vfs/vfs_process.c",
    "servers/vfs/vfs_spawn.c",
)
REQUIRED_SPLIT_FUNCTIONS = (
    ("servers/vfs/vfs_state.c", "validate_open_files"),
    ("servers/vfs/vfs_state.c", "validate_pending"),
    ("servers/vfs/vfs_state.c", "validate_spawn"),
    ("servers/vfs/vfs_namespace.c", "vfs_namespace_handle_open"),
    ("servers/vfs/vfs_namespace.c", "vfs_namespace_handle_mkdir"),
    ("servers/vfs/vfs_io.c", "vfs_io_handle_regular_read"),
    ("servers/vfs/vfs_io.c", "vfs_io_handle_getdents"),
    ("servers/vfs/vfs_io.c", "vfs_io_handle_transfer"),
    ("servers/vfs/vfs_console.c", "vfs_console_handle_write"),
    ("servers/vfs/vfs_spawn.c", "vfs_spawn_prepare_image"),
    (
        "servers/vfs/vfs_spawn.c",
        "vfs_spawn_prepare_descriptors",
    ),
    ("servers/vfs/vfs_core.c", "micros_vfs_handle_request"),
    (
        "servers/vfs/vfs_service_core.c",
        "micros_vfs_service_build_pm_control_result",
    ),
    (
        "servers/vfs/vfs_service_core.c",
        "micros_vfs_service_handle_message",
    ),
)
FUNCTION_REVIEW_LINE_LIMIT = 120


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


def skip_c_ignored(text: str, index: int) -> int:
    while index < len(text):
        if text[index].isspace():
            index += 1
        elif text.startswith("//", index):
            newline = text.find("\n", index + 2)
            index = len(text) if newline < 0 else newline + 1
        elif text.startswith("/*", index):
            close = text.find("*/", index + 2)
            if close < 0:
                raise AssertionError("unterminated C comment")
            index = close + 2
        else:
            break
    return index


def matching_c_delimiter(
    text: str,
    open_index: int,
    opening: str,
    closing: str,
) -> int:
    depth = 0
    index = open_index

    while index < len(text):
        if text.startswith("//", index):
            newline = text.find("\n", index + 2)
            index = len(text) if newline < 0 else newline + 1
            continue
        if text.startswith("/*", index):
            close = text.find("*/", index + 2)
            if close < 0:
                raise AssertionError("unterminated C comment")
            index = close + 2
            continue
        if text[index] in ('"', "'"):
            quote = text[index]
            index += 1
            while index < len(text) and text[index] != quote:
                index += 2 if text[index] == "\\" else 1
            if index >= len(text):
                raise AssertionError("unterminated C literal")
        elif text[index] == opening:
            depth += 1
        elif text[index] == closing:
            depth -= 1
            if depth == 0:
                return index
        index += 1

    raise AssertionError(f"unterminated {opening}{closing} region")


def function_line_count(text: str, name: str) -> int:
    signatures = re.finditer(
        rf"(?m)^(?:static\s+)?enum\s+[A-Za-z0-9_]+\s*"
        rf"(?:\n\s*)?{re.escape(name)}\s*\(",
        text,
    )

    for signature in signatures:
        open_parenthesis = signature.end() - 1
        close_parenthesis = matching_c_delimiter(
            text,
            open_parenthesis,
            "(",
            ")",
        )
        open_brace = skip_c_ignored(text, close_parenthesis + 1)
        if open_brace >= len(text) or text[open_brace] != "{":
            continue
        close_brace = matching_c_delimiter(
            text,
            open_brace,
            "{",
            "}",
        )
        return text.count("\n", signature.start(), close_brace) + 1

    raise AssertionError(f"missing function definition for {name}")


def static_function_definitions(text: str) -> list[str]:
    definitions = []
    signatures = re.finditer(
        r"(?m)^static\b[^;{}=]*?\b"
        r"(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*\(",
        text,
    )

    for signature in signatures:
        open_parenthesis = signature.end() - 1
        close_parenthesis = matching_c_delimiter(
            text,
            open_parenthesis,
            "(",
            ")",
        )
        body = skip_c_ignored(text, close_parenthesis + 1)
        if body < len(text) and text[body] == "{":
            definitions.append(signature.group("name"))

    return definitions


class VfsSourceLayoutTest(unittest.TestCase):
    def test_function_line_count_skips_forward_declarations(
        self,
    ) -> None:
        definition = """enum sample_error
target_function(
    int value
)
{
    return value == 0 ? SAMPLE_OK : SAMPLE_ERROR;
}"""
        source = f"""enum sample_error target_function(int value);

static enum sample_error helper(void)
{{
    return SAMPLE_OK;
}}

{definition}
"""

        self.assertEqual(
            len(definition.splitlines()),
            function_line_count(source, "target_function"),
        )

    def test_static_function_definition_detection(self) -> None:
        source = """static bool declaration_only(int value);

static bool ordinary_body(int value)
{
    return value != 0;
}

static
inline bool split_inline_body(void)
{
    return true;
}
"""

        self.assertEqual(
            ["ordinary_body", "split_inline_body"],
            static_function_definitions(source),
        )

    def test_portable_modules_exist(self) -> None:
        missing = [
            source
            for source in PORTABLE_SOURCES
            if not (REPOSITORY_ROOT / source).is_file()
        ]

        self.assertEqual([], missing)

    def test_private_contract_has_no_static_function_bodies(
        self,
    ) -> None:
        header = INTERNAL_HEADER_PATH.read_text(encoding="utf-8")

        self.assertEqual([], static_function_definitions(header))

    def test_consumers_use_complete_portable_source_set(self) -> None:
        cmake = CMAKE_PATH.read_text(encoding="utf-8")
        source_set = cmake_command(cmake, "set", PORTABLE_SOURCE_SET)
        listed_sources = tuple(
            re.findall(r"servers/vfs/[A-Za-z0-9_]+\.c", source_set)
        )

        self.assertEqual(PORTABLE_SOURCES, listed_sources)
        for source in PORTABLE_SOURCES:
            self.assertEqual(1, cmake.count(source), source)

        for target in ("vfs_test", "vfs_model_test", "elf_spawn_model_test"):
            block = cmake_command(cmake, "add_executable", target)
            self.assertIn(f"${{{PORTABLE_SOURCE_SET}}}", block)

        service_test = cmake_command(
            cmake,
            "add_executable",
            "vfs_service_test",
        )
        self.assertIn(f"${{{PORTABLE_SOURCE_SET}}}", service_test)
        self.assertIn("servers/vfs/vfs_service_core.c", service_test)

        service = cmake_command(
            cmake,
            "micros_add_bootstrap_user",
            "micros_vfs_service",
        )
        self.assertIn(f"${{{PORTABLE_SOURCE_SET}}}", service)
        self.assertIn("servers/vfs/vfs_service_core.c", service)
        self.assertIn("servers/vfs/vfs_service.c", service)

    def test_required_function_splits_stay_below_review_signal(
        self,
    ) -> None:
        oversized = {}

        for relative_path, function_name in REQUIRED_SPLIT_FUNCTIONS:
            text = (REPOSITORY_ROOT / relative_path).read_text(
                encoding="utf-8"
            )
            line_count = function_line_count(text, function_name)
            if line_count > FUNCTION_REVIEW_LINE_LIMIT:
                oversized[function_name] = line_count

        self.assertEqual({}, oversized)


if __name__ == "__main__":
    unittest.main()

from pathlib import Path
import re
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
CMAKE_PATH = REPOSITORY_ROOT / "CMakeLists.txt"
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


class VfsSourceLayoutTest(unittest.TestCase):
    def test_portable_modules_exist(self) -> None:
        missing = [
            source
            for source in PORTABLE_SOURCES
            if not (REPOSITORY_ROOT / source).is_file()
        ]

        self.assertEqual([], missing)

    def test_consumers_use_complete_portable_source_set(self) -> None:
        cmake = CMAKE_PATH.read_text(encoding="utf-8")
        source_set = cmake_command(cmake, "set", PORTABLE_SOURCE_SET)
        listed_sources = tuple(
            re.findall(r"servers/vfs/[A-Za-z0-9_]+\.c", source_set)
        )

        self.assertEqual(PORTABLE_SOURCES, listed_sources)
        for source in PORTABLE_SOURCES:
            self.assertEqual(1, cmake.count(source), source)

        for target in ("vfs_test", "vfs_model_test"):
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


if __name__ == "__main__":
    unittest.main()

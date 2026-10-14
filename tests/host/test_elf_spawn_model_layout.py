from pathlib import Path
import re
import unittest

from tests.host.test_frame_ownership_source_layout import (
    c_function_definitions,
    c_lexical_text,
    cmake_command,
)
from tools import validation_plan
from tests.host.test_vm_service_source_layout import file_scope_objects


ROOT = Path(__file__).resolve().parents[2]
MODEL_SOURCES = (
    "tests/host/elf_spawn_model_test.c",
    "tests/host/elf_spawn_model_context.c",
    "tests/host/elf_spawn_model_kernel.c",
    "tests/host/elf_spawn_model_vm.c",
    "tests/host/elf_spawn_model_transport.c",
    "tests/host/elf_spawn_model_operations.c",
    "tests/host/elf_spawn_model_reference.c",
    "tests/host/elf_spawn_model_compare.c",
)
MODEL_HEADER = "tests/host/elf_spawn_model_internal.h"


class ElfSpawnModelLayoutTest(unittest.TestCase):
    def test_mutable_context_and_private_contract_have_one_owner(self) -> None:
        observed = {}
        for relative in (*MODEL_SOURCES, MODEL_HEADER):
            objects = file_scope_objects((ROOT / relative).read_text(encoding="utf-8"))
            if objects:
                observed[relative] = objects
        self.assertEqual(
            {"tests/host/elf_spawn_model_context.c":
                ("static struct elf_spawn_model_context model_context;",)},
            observed,
        )
        header = (ROOT / MODEL_HEADER).read_text(encoding="utf-8")
        self.assertEqual({}, c_function_definitions(header))
        self.assertIn("struct vm_core_test_context *vm;", header)
        self.assertNotRegex(header, r"\bstruct\s+micros_vm_state\s+[A-Za-z_]")
        self.assertNotRegex(header, r"\bstruct\s+micros_vm_boot_info\s+[A-Za-z_]")

    def test_complete_model_has_one_slow_native_registration(self) -> None:
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        block = cmake_command(cmake, "set", "MICROS_ELF_SPAWN_MODEL_SOURCES")
        self.assertEqual(MODEL_SOURCES, tuple(re.findall(r"tests/host/[a-z_]+\.c", block)))
        executable = cmake_command(cmake, "add_executable", "elf_spawn_model_test")
        for source_set in (
            "MICROS_ELF_SPAWN_MODEL_SOURCES", "MICROS_PM_CORE_SOURCES",
            "MICROS_PM_COORDINATOR_SOURCES", "MICROS_SPAWN_REQUEST_SOURCES",
            "MICROS_VM_BOOT_VALIDATION_SOURCES", "MICROS_VM_IMAGE_SOURCES",
            "MICROS_VM_CORE_SOURCES", "MICROS_VM_SERVICE_CORE_SOURCES",
            "MICROS_VM_CORE_TEST_FIXTURE_SOURCES", "MICROS_VFS_PORTABLE_SOURCES",
            "MICROS_VFS_TEST_FIXTURE_SOURCES",
        ):
            self.assertEqual(1, executable.count("${" + source_set + "}"), source_set)
        self.assertIn("servers/vfs/vfs_service_core.c", executable)
        self.assertNotIn("MICROS_VM_SERVICE_BACKEND_SOURCES", executable)
        self.assertNotIn("MICROS_PM_RUNTIME_BACKEND_SOURCES", executable)
        self.assertIn("NAME elf-spawn-model", cmake)
        self.assertEqual(
            ("elf-spawn-model", "elf_spawn_model_test"),
            validation_plan.SLOW_MODEL_CTESTS.get("test-elf-spawn-model"),
        )
        validation_plan.validate_inventory(ROOT)

    def test_family_and_independent_reference_are_exact(self) -> None:
        expected = {*MODEL_SOURCES, MODEL_HEADER}
        self.assertEqual(
            expected,
            {str(path.relative_to(ROOT)) for path in (ROOT / "tests/host").glob("elf_spawn_model*")},
        )
        for relative in expected:
            text = (ROOT / relative).read_text(encoding="utf-8")
            self.assertNotIn('#include "servers/pm/pm_internal.h"', text)
            self.assertNotIn('#include "servers/vm/vm_internal.h"', text)
            self.assertNotIn('#include "servers/vfs/vfs_internal.h"', text)
            for name, size in c_function_definitions(text).items():
                self.assertLessEqual(size, 120, (relative, name, size))
        reference = c_lexical_text(
            (ROOT / "tests/host/elf_spawn_model_reference.c").read_text(encoding="utf-8")
        )
        self.assertEqual([], re.findall(r"\b(?:micros_|pm_|vm_|vfs_)[A-Za-z0-9_]+\s*\(", reference))
        main = (ROOT / MODEL_SOURCES[0]).read_text(encoding="utf-8")
        self.assertIn("8192", main)
        self.assertIn("seed=", main)
        self.assertIn("trace", main)


if __name__ == "__main__":
    unittest.main()

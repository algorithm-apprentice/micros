"""Current address-space/execution ownership and build boundaries."""

from pathlib import Path
import re
import unittest

from tests.host.build_source_records import cmake_command, cmake_if_blocks
from tests.host.c_source_records import c_function_definitions, file_scope_objects
from tests.host.source_snapshot import source_bytes
from tools import validation_plan


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
SOURCE_SETS = {
    "MICROS_KERNEL_ADDRESS_SPACE_SOURCES": (
        "kernel/memory/address_space.c", "kernel/memory/address_space_validation.c",
    ),
    "MICROS_USER_ADDRESS_SPACE_CORE_SOURCES": (
        "kernel/memory/user_address_space_core.c", "kernel/memory/user_address_space_vm_plan.c",
    ),
    "MICROS_USER_ADDRESS_SPACE_SOURCES": tuple(
        "kernel/memory/user_address_space" + suffix + ".c" for suffix in (
            "", "_runtime", "_table", "_validation", "_mapping", "_mutation",
            "_handoff", "_tty", "_vm_scratch", "_vm_freeze", "_vm_teardown", "_activation",
        )
    ),
    "MICROS_USER_EXECUTION_SOURCES": tuple(
        "kernel/execution/user_execution" + suffix + ".c" for suffix in ("", "_validation", "_trap")
    ),
    "MICROS_USER_ADDRESS_SPACE_HOST_TEST_SOURCES": (
        "tests/host/user_address_space_core_test.c", "tests/host/user_address_space_owner_test.c",
        "tests/host/user_address_space_vm_plan_test.c",
    ),
    "MICROS_USER_ADDRESS_SPACE_QEMU_TEST_SOURCES": tuple(
        "kernel/tests/memory/user_address_space_test" + suffix + ".c"
        for suffix in ("", "_fixture", "_lifecycle", "_mapping", "_validation")
    ),
    "MICROS_USER_EXECUTION_QEMU_TEST_SOURCES": tuple(
        "kernel/tests/execution/user_execution_test" + suffix + ".c"
        for suffix in ("", "_fixture", "_prepare", "_trap", "_lifecycle")
    ),
}
PRIVATE_HEADERS = (
    "kernel/memory/address_space_internal.h", "kernel/memory/user_address_space_internal.h",
    "kernel/execution/user_execution_internal.h",
    "kernel/tests/memory/user_address_space_test_internal.h",
    "kernel/tests/execution/user_execution_test_internal.h",
    "tests/host/user_address_space_core_test_suites.h",
)
PUBLIC_SYMBOLS = {
    "kernel/memory/address_space.c": ("micros_kernel_address_space_initialize", "micros_kernel_address_space_report"),
    "kernel/tests/memory/mmu_test.c": (
        "micros_kernel_address_space_run_self_test", "micros_kernel_address_space_handle_test_trap",
    ),
    "kernel/memory/user_address_space.c": ("micros_user_address_space_create", "micros_user_address_space_destroy"),
    "kernel/memory/user_address_space_mutation.c": (
        "micros_user_address_space_allocate_page", "micros_user_address_space_release_page",
        "micros_user_address_space_test_fail_after_allocations",
    ),
    "kernel/memory/user_address_space_mapping.c": (
        "micros_user_address_space_lookup", "micros_user_address_space_translate", "micros_user_address_space_inventory",
    ),
    "kernel/memory/user_address_space_handoff.c": (
        "micros_user_address_space_prepare_wired_handoff", "micros_user_address_space_complete_wired_handoff",
    ),
    "kernel/memory/user_address_space_activation.c": (
        "micros_user_address_space_activate", "micros_user_address_space_activate_kernel",
        "micros_user_address_space_prepare_activation",
    ),
    "kernel/memory/user_address_space_validation.c": ("micros_user_address_space_validate",),
    "kernel/memory/user_address_space_core.c": (
        "micros_user_address_space_leaf_owner_kind", "micros_user_address_space_validate_frame_owner",
        "micros_user_address_space_classify_managed_leaf",
    ),
    "kernel/memory/user_address_space_vm_plan.c": ("micros_user_address_space_prepare_vm_table_plan",),
    "kernel/tests/memory/user_address_space_test.c": (
        "micros_user_address_space_runtime_run_self_test", "micros_user_address_space_handle_test_trap",
    ),
    "kernel/execution/user_execution.c": (
        "micros_user_execution_prepare", "micros_user_execution_preflight_post_handoff",
        "micros_user_execution_commit_post_handoff_prevalidated", "micros_user_execution_inspect",
        "micros_user_execution_detach", "micros_user_execution_test_stack_bounds",
    ),
    "kernel/execution/user_execution_validation.c": (
        "micros_user_execution_validate_return", "micros_user_execution_validate_context",
    ),
    "kernel/execution/user_execution_trap.c": (
        "micros_user_execution_capture_trap", "micros_user_execution_store_context",
        "micros_user_execution_install_return_frame",
    ),
    "kernel/tests/execution/user_execution_test.c": (
        "micros_user_execution_runtime_run_self_test", "micros_user_execution_test_enter_production",
    ),
    "kernel/tests/execution/user_execution_test_trap.c": (
        "micros_user_execution_test_pre_capture", "micros_user_execution_handle_test_trap",
    ),
    "tests/host/user_address_space_core_test.c": ("micros_user_address_space_core_test_run",),
}


def current_text(path):
    return source_bytes(REPOSITORY_ROOT, REPOSITORY_ROOT / path).decode()


class KernelAddressSpaceSourceLayoutTest(unittest.TestCase):
    def test_current_source_sets_and_declaration_only_private_headers(self):
        cmake = current_text("CMakeLists.txt")
        for name, sources in SOURCE_SETS.items():
            with self.subTest(source_set=name):
                block = cmake_command(cmake, "set", name)
                self.assertEqual(sources, tuple(re.findall(r"(?:kernel|tests/host)/[A-Za-z0-9_/]+\.c", block)))
                self.assertEqual(len(sources), len(set(sources)))
                self.assertTrue(all((REPOSITORY_ROOT / path).is_file() for path in sources))
        for header in PRIVATE_HEADERS:
            with self.subTest(header=header):
                self.assertEqual({}, c_function_definitions(current_text(header)))

    def test_consumers_expand_the_complete_source_sets_once(self):
        cmake = current_text("CMakeLists.txt")
        consumers = (
            (cmake_command(cmake, "add_executable", "micros"), tuple(SOURCE_SETS)[:4]),
            (cmake_command(cmake, "add_executable", "endpoint_test"), (
                "MICROS_USER_ADDRESS_SPACE_CORE_SOURCES", "MICROS_USER_ADDRESS_SPACE_HOST_TEST_SOURCES",
            )),
            ("\n".join(cmake_if_blocks(cmake, "MICROS_BUILD_USER_ADDRESS_SPACE_TEST")),
             ("MICROS_USER_ADDRESS_SPACE_QEMU_TEST_SOURCES",)),
            ("\n".join(cmake_if_blocks(cmake, "MICROS_BUILD_USER_EXECUTION_TEST")),
             ("MICROS_USER_EXECUTION_QEMU_TEST_SOURCES",)),
        )
        for block, expected in consumers:
            for name in SOURCE_SETS:
                self.assertEqual(int(name in expected), block.count("${" + name + "}"), name)

    def test_public_entrypoints_keep_their_responsible_owner(self):
        definitions = {}
        paths = {path for sources in SOURCE_SETS.values() for path in sources} | set(PUBLIC_SYMBOLS)
        for path in paths:
            for name in c_function_definitions(current_text(path)):
                definitions.setdefault(name, []).append(path)
        for path, names in PUBLIC_SYMBOLS.items():
            for name in names:
                with self.subTest(symbol=name):
                    self.assertEqual([path], definitions.get(name))

    def test_source_includes_preserve_the_production_test_boundary(self):
        paths = {path for sources in SOURCE_SETS.values() for path in sources} | set(PRIVATE_HEADERS)
        test_headers = {Path(path).name for path in PRIVATE_HEADERS if "/tests/" in path or path.startswith("tests/")}
        for path in paths:
            includes = re.findall(r'(?m)^\s*#\s*include\s*[<"]([^>"]+)[>"]', current_text(path))
            self.assertFalse(any(name.endswith(".c") for name in includes), path)
            if path.startswith(("kernel/memory/", "kernel/execution/")):
                self.assertFalse(any(Path(name).name in test_headers for name in includes), path)

    def test_current_scratch_storage_has_one_owner(self):
        expected = {
            "reachable_selection": "static struct micros_frame_selection reachable_selection;",
            "release_selection": "static struct micros_frame_selection release_selection;",
            "scratch_mode": "static uint8_t scratch_mode;",
        }
        sources = SOURCE_SETS["MICROS_USER_ADDRESS_SPACE_SOURCES"]
        for declaration in expected.values():
            owners = [path for path in sources if declaration in file_scope_objects(current_text(path))]
            self.assertEqual(["kernel/memory/user_address_space_runtime.c"], owners)

    def test_split_target_fixtures_have_one_context_and_no_duplicate_state(self):
        for source_set, kind in (
            ("MICROS_USER_ADDRESS_SPACE_QEMU_TEST_SOURCES", "micros_user_address_space_test_context"),
            ("MICROS_USER_EXECUTION_QEMU_TEST_SOURCES", "micros_user_execution_test_context"),
        ):
            sources = SOURCE_SETS[source_set]
            for path in sources:
                expected = (f"static struct {kind} test_context;",) if path.endswith("_fixture.c") else ()
                with self.subTest(path=path):
                    self.assertEqual(expected, file_scope_objects(current_text(path)))

    def test_current_paths_keep_broad_validation_classification(self):
        for source_set, paths in SOURCE_SETS.items():
            for path in paths[1:]:
                for tier in ("fast", "pr"):
                    with self.subTest(source_set=source_set, path=path, tier=tier):
                        self.assertEqual(
                            validation_plan.plan([paths[0]], tier, "main"),
                            validation_plan.plan([path], tier, "main"),
                        )


if __name__ == "__main__":
    unittest.main()

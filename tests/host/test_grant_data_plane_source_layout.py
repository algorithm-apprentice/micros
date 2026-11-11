"""Current module, fixture, model and build ownership contracts."""

from pathlib import Path
import re
import unittest

from tests.host.build_source_records import cmake_commands, cmake_if_blocks
from tests.host.c_source_records import (
    c_function_body, c_function_definitions, c_lexical_text,
    file_scope_objects, function_records, matching_lexical, tokens,
)
from tests.host.source_snapshot import source_bytes
from tools.check_docs import topological_order
from tools import validation_plan


ROOT = Path(__file__).resolve().parents[2]

STRUCTURAL_TEST_PATH = "tests/host/test_grant_data_plane_source_layout.py"

COPY_SOURCES = ('kernel/grant/grant_copy.c', 'kernel/grant/grant_copy_range.c')

SYSCALL_TEST_SOURCES = (
    'kernel/tests/grant/grant_syscall_test.c',
    'kernel/tests/grant/grant_syscall_test_fixture.c',
    'kernel/tests/grant/grant_syscall_test_commands.c',
    'kernel/tests/grant/grant_syscall_test_copy_commands.c',
    'kernel/tests/grant/grant_syscall_test_transitions.c',
    'kernel/tests/grant/grant_syscall_test_effects.c',
    'kernel/tests/grant/grant_syscall_test_hooks.c',
    'kernel/tests/grant/grant_syscall_test_lifecycle.c',
)

HOST_SOURCES = (
    "tests/host/grant_test.c",
    "tests/host/grant_test_fixture.c",
    "tests/host/grant_lifecycle_test.c",
    "tests/host/grant_validation_test.c",
    "tests/host/grant_model_test.c",
    "tests/host/grant_model_reference.c",
    "tests/host/grant_model_operations.c",
    "tests/host/grant_model_copy.c",
)

FAMILY_PATHS = (
    COPY_SOURCES, ('kernel/grant/grant_syscall.c',), ('kernel/tests/grant/grant_test.c',),
    SYSCALL_TEST_SOURCES, HOST_SOURCES,
    ("tests/host/grant_copy_test.c",), ("tests/host/grant_syscall_core_test.c",),
)

SOURCE_SETS = {
    "MICROS_GRANT_COPY_TARGET_SOURCES": COPY_SOURCES,
    "MICROS_GRANT_HOST_TEST_SOURCES": HOST_SOURCES,
    "MICROS_GRANT_SYSCALL_QEMU_TEST_SOURCES": SYSCALL_TEST_SOURCES,
}

PRIVATE_HEADERS = (
    'kernel/grant/grant_copy_internal.h',
    "tests/host/grant_test_internal.h",
    'kernel/tests/grant/grant_syscall_test_internal.h',
)

NATIVE_FIXTURE_TAG = "grant_test_fixture"

NATIVE_FIXTURE_GETTER = "grant_test_context"

NATIVE_FIELDS = (
    ("struct micros_endpoint_registry endpoint_registry;", False),
    ("struct micros_grant_registry grant_registry;", False),
    ("struct micros_kernel_objects objects;", False),
    ("struct micros_process_handle processes[GRANT_PROCESS_COUNT];", False),
    ("struct micros_thread_handle threads[GRANT_PROCESS_COUNT];", False),
    ("micros_endpoint_t endpoints[GRANT_PROCESS_COUNT];", False),
    ("struct micros_hart_handle hart;", False),
    ("struct grant_model_trace_entry model_trace[GRANT_MODEL_TRACE_COUNT];", False),
    ("size_t model_trace_count;", False),
)

def source(path):
    return source_bytes(ROOT, ROOT / path).decode()

def owned_records(family, texts=None):
    texts = texts if texts is not None else {
        path: source(path) for path in FAMILY_PATHS[family] if (ROOT / path).is_file()
    }
    return {
        (path, name): record for path, text in texts.items()
        for name, record in function_records(text).items()
    }

def work_declarations(owner, tag, fields):
    lexical = c_lexical_text(owner)
    starts = tuple(re.finditer(r"\bstruct " + tag + r"\s*\{", lexical))
    if len(starts) != 1:
        raise AssertionError("one file-local execution work type owns only original automatic objects")
    opening = lexical.index("{", starts[0].start())
    closing = matching_lexical(lexical, opening, "{", "}")
    declarations = tuple(tokens(part + ";") for part in lexical[opening + 1:closing].split(";") if part.strip())
    if declarations != tuple(tokens(declaration) for declaration, _ in fields):
        raise AssertionError("automatic work retains original fields, complete types and declaration order")
    return declarations

CONSUMERS = {
    "micros": ("MICROS_GRANT_COPY_TARGET_SOURCES",),
    "endpoint_test": ("MICROS_GRANT_HOST_TEST_SOURCES",),
    "ipc_model_test": ("MICROS_GRANT_HOST_TEST_SOURCES",),
}


class GrantDataPlaneSourceLayoutTest(unittest.TestCase):
    def test_current_target_source_set_is_owned_by_its_configuration(self):
        blocks = cmake_if_blocks(source("CMakeLists.txt"), "MICROS_BUILD_GRANT_SYSCALL_TEST")
        self.assertEqual(1, sum(block.count("${MICROS_GRANT_SYSCALL_QEMU_TEST_SOURCES}") for block in blocks))

    def test_actual_private_owner_directions_are_acyclic_and_raw_control_are_fixture_leaves(self):
        directions = {
            0: {0: (1,), 1: ()},
            3: {0: (1, 2, 7), 1: (), 2: (1, 3), 3: (1,), 4: (1, 2), 5: (1,), 6: (1, 2, 4, 5, 7), 7: (1,)},
            4: {0: (2, 3), 1: (5,), 2: (1, 5), 3: (1, 5), 4: (1, 5, 6, 7), 5: (), 6: (1, 5), 7: (1, 5)},
        }
        for family, allowed in directions.items():
            records = owned_records(family)
            owners = {name: FAMILY_PATHS[family].index(path) for path, name in records}
            for (path, name), record in records.items():
                origin = FAMILY_PATHS[family].index(path)
                destinations = {
                    owners[word] for word in tokens(record.body) if word in owners and owners[word] != origin
                }
                self.assertLessEqual(destinations, set(allowed[origin]), (path, name, destinations))
            for origin, path in enumerate(FAMILY_PATHS[family]):
                destinations = {
                    owners[word] for declaration in file_scope_objects(source(path))
                    for word in tokens(declaration) if word in owners and owners[word] != origin
                }
                self.assertLessEqual(destinations, set(allowed[origin]), (path, destinations))
            remaining = set(allowed)
            while remaining:
                leaves = {owner for owner in remaining if not set(allowed[owner]) & remaining}
                self.assertTrue(leaves, (family, remaining))
                remaining -= leaves

    def test_native_fixture_has_only_nine_original_fields_and_nonresetting_storage(self):
        header = source(PRIVATE_HEADERS[1])
        self.assertEqual(
            tuple(tokens(declaration) for declaration, _ in NATIVE_FIELDS),
            work_declarations(header, NATIVE_FIXTURE_TAG, NATIVE_FIELDS),
        )
        fixture = source(HOST_SOURCES[1])
        self.assertEqual(
            tokens("{ return &fixture_state; }"),
            tokens("{" + function_records(fixture)[NATIVE_FIXTURE_GETTER].body + "}"),
        )
        for index, path in enumerate(HOST_SOURCES):
            expected = ("static struct grant_test_fixture fixture_state;",) if index == 1 else ()
            self.assertEqual(expected, tuple(file_scope_objects(source(path))), path)

    def test_range_planning_has_its_exact_read_only_leaf_owner(self):
        facade = function_records(source(COPY_SOURCES[0]))
        self.assertNotIn("plan_range", set(facade))
        self.assertNotIn("grant_copy_plan_range", set(facade))
        leaf = ROOT / COPY_SOURCES[1]
        self.assertTrue((leaf).is_file())
        self.assertIn("grant_copy_plan_range", set(function_records(source(str((leaf).relative_to(ROOT))))))

    def test_current_source_sets_and_consumers(self):
            cmake = source("CMakeLists.txt")
            for name, paths in SOURCE_SETS.items():
                blocks = cmake_commands(cmake, "set", name)
                self.assertEqual(1, len(blocks), name)
                listed = tuple(re.findall(r"(?:kernel|servers|tests)/[A-Za-z0-9_/]+\.c", blocks[0]))
                self.assertEqual(paths, listed, name)
                self.assertEqual(len(paths), len(set(paths)), name)
                for path in paths:
                    self.assertTrue((ROOT / path).is_file(), path)
            for path in PRIVATE_HEADERS:
                self.assertEqual({}, c_function_definitions(source(path)), path)
            for target, selected in CONSUMERS.items():
                blocks = cmake_commands(cmake, "add_executable", target)
                self.assertEqual(1, len(blocks), target)
                for name in SOURCE_SETS:
                    self.assertEqual(int(name in selected), blocks[0].count("${" + name + "}"), (target, name))

    def test_current_private_headers_and_production_boundaries(self):
            test_headers = {Path(path).name for path in PRIVATE_HEADERS if "/tests/" in path or path.startswith("tests/")}
            for paths in SOURCE_SETS.values():
                for path in paths:
                    includes = re.findall(r'(?m)^\s*#\s*include\s*[<"]([^>"]+)[>"]', source(path))
                    self.assertFalse(any(name.endswith(".c") for name in includes), path)
                    if not path.startswith("tests/") and "/tests/" not in path:
                        self.assertFalse(any(Path(name).name in test_headers for name in includes), path)

    def test_current_validation_ownership_is_not_narrowed(self):
            for paths in SOURCE_SETS.values():
                for path in paths[1:]:
                    for tier in ("fast", "pr"):
                        self.assertEqual(
                            validation_plan.plan([paths[0]], tier, "main"),
                            validation_plan.plan([path], tier, "main"), path,
                        )
            self.assertIn(STRUCTURAL_TEST_PATH, validation_plan.HOST_UNIT_ONLY_INPUTS)



if __name__ == "__main__":
    unittest.main()

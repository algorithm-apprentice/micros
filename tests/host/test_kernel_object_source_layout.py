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

STRUCTURAL_TEST_PATH = "tests/host/test_kernel_object_source_layout.py"

TARGET_TASKS = (
    "kernel_object_model_test_create_process_and_thread",
    "kernel_object_model_test_attach_and_select_thread",
    "kernel_object_model_test_retire_thread_and_process",
    "kernel_object_model_test_verify_process_reuse",
)

CORE_SOURCES = (
    'kernel/objects/kernel_objects.c',
    'kernel/objects/kernel_object_state.c',
    'kernel/objects/kernel_object_identity.c',
    'kernel/objects/kernel_object_stack.c',
    'kernel/objects/kernel_object_process.c',
    'kernel/objects/kernel_object_thread.c',
    'kernel/objects/kernel_object_context.c',
    'kernel/objects/kernel_object_hart.c',
    'kernel/objects/kernel_object_validation.c',
)

SOURCE_SETS = {
    "MICROS_KERNEL_OBJECT_CORE_SOURCES": CORE_SOURCES,
    "MICROS_KERNEL_OBJECT_HOST_TEST_SOURCES": (
        "tests/host/kernel_objects_test.c",
        "tests/host/kernel_objects_test_fixture.c",
        "tests/host/kernel_objects_initialization_test.c",
        "tests/host/kernel_objects_process_test.c",
        "tests/host/kernel_objects_thread_test.c",
        "tests/host/kernel_objects_hart_test.c",
        "tests/host/kernel_objects_identity_test.c",
        "tests/host/kernel_objects_validation_test.c",
        "tests/host/kernel_objects_model_test.c",
        "tests/host/kernel_objects_model_reference.c",
        "tests/host/kernel_objects_model_projection.c",
        "tests/host/kernel_objects_model_operations.c",
    ),
    "MICROS_KERNEL_OBJECT_QEMU_TEST_SOURCES": (
        'kernel/tests/objects/object_model_test.c',
        'kernel/tests/objects/object_model_test_scenarios.c',
    ),
}

PRIVATE_HEADERS = (
    'kernel/objects/kernel_object_private.h',
    "tests/host/kernel_objects_test_internal.h",
    'kernel/tests/objects/object_model_test_internal.h',
)

CORE_DAG = {
    0: {1}, 1: {2}, 2: set(), 3: set(), 4: {2},
    5: {1, 2}, 6: {1, 2, 3, 5}, 7: {2, 3, 5}, 8: {1, 2, 3},
}

def family_paths(family):
    return tuple(SOURCE_SETS.values())[family]

def source(path):
    return source_bytes(ROOT, ROOT / path).decode()

def owned_records(family, texts=None):
    texts = texts if texts is not None else {
        path: source(path) for path in family_paths(family) if (ROOT / path).is_file()
    }
    return {
        (path, name): record for path, text in texts.items()
        for name, record in function_records(text).items()
    }

PRIVATE_PREFIX = "kernel_object_"
CONSUMERS = {
    target: (("MICROS_KERNEL_OBJECT_CORE_SOURCES", "MICROS_KERNEL_OBJECT_HOST_TEST_SOURCES")
             if target == "kernel_objects_test" else ("MICROS_KERNEL_OBJECT_CORE_SOURCES",))
    for target in ("kernel_objects_test", "frame_ownership_test", "scheduler_test", "endpoint_test",
                   "ipc_model_test", "bootstrap_control_test", "pm_test", "micros")
}


class KernelObjectSourceLayoutTest(unittest.TestCase):
    def test_current_target_source_set_is_owned_by_its_configuration(self):
        blocks = cmake_if_blocks(source("CMakeLists.txt"), "MICROS_BUILD_OBJECT_MODEL_TEST")
        self.assertEqual(1, sum(block.count("${MICROS_KERNEL_OBJECT_QEMU_TEST_SOURCES}") for block in blocks))

    def test_portable_registry_storage_has_its_reviewed_state_owner(self):
        spellings = {"storage_is_zero", "kernel_object_storage_is_zero"}
        owners = [
            path for path in CORE_SOURCES
            if (ROOT / path).is_file()
            and spellings & set(c_function_definitions(source_bytes(ROOT, ROOT / path).decode()))
        ]
        self.assertEqual(['kernel/objects/kernel_object_state.c'], owners)

    def test_host_fixture_owns_one_original_registry_and_a_nonresetting_getter(self):
        path = family_paths(1)[1]
        text = source(path)
        records = function_records(text)
        self.assertIn("kernel_object_test_registry", records)
        self.assertEqual(1, text.count("static struct micros_kernel_objects objects;"))
        self.assertEqual(
            tokens("{ return &objects; }"),
            tokens("{" + records["kernel_object_test_registry"].body + "}"),
        )
        self.assertEqual(
            tokens("struct micros_kernel_objects *kernel_object_test_registry(void)"),
            tokens(records["kernel_object_test_registry"].signature),
        )
        for owner in family_paths(1):
            expected = ("static struct micros_kernel_objects objects;",) if owner == path else ()
            self.assertEqual(tuple(map(tokens, expected)), tuple(map(tokens, file_scope_objects(source(owner)))))
            self.assertNotRegex(source(owner), r'#include\s*"[^"]*kernel_object_private\.h"')

    def test_host_private_edges_include_callbacks_and_keep_reference_independent(self):
        paths = family_paths(1)
        records = owned_records(1)
        owners = {name: path for path, name in records}
        self.assertEqual(len(records), len(owners))
        allowed = {
            0: {2, 3, 4, 5, 6, 7, 8}, 1: set(), 2: {1}, 3: {1}, 4: {1},
            5: {1}, 6: {1}, 7: {1}, 8: {1, 9, 10, 11}, 9: set(), 10: {1, 9}, 11: {1, 9},
        }
        edges = set()
        exports = set()
        for (path, name), record in records.items():
            referenced = set(re.findall(r"\b[A-Za-z_]\w*\b", c_lexical_text(record.body)))
            for callee in referenced & set(owners):
                dependency = owners[callee]
                if dependency != path:
                    self.assertIn(paths.index(dependency), allowed[paths.index(path)], (name, callee))
                    edges.add((dependency, path))
                    exports.add(callee)
        self.assertEqual(set(paths), set(topological_order(set(paths), edges)))
        declarations = re.findall(
            r"\b(kernel_object_test_\w+)\s*\([^;{}]*\)\s*;", c_lexical_text(source(PRIVATE_HEADERS[1])),
        )
        self.assertEqual(len(declarations), len(set(declarations)))
        self.assertEqual(exports, set(declarations))

    def test_target_private_edges_are_exact_and_add_no_runtime_or_static_storage_owner(self):
        root_path, scenarios_path = family_paths(2)
        records = function_records(source(scenarios_path))
        header = source(PRIVATE_HEADERS[2])
        declared = re.findall(r"\b(kernel_object_model_test_\w+)\s*\([^;{}]*\)\s*;", c_lexical_text(header))
        self.assertEqual(set(TARGET_TASKS), set(declared))
        self.assertEqual(4, len(declared))
        root = function_records(source(root_path))["micros_kernel_object_runtime_run_self_test"]
        for name in TARGET_TASKS:
            self.assertEqual(1, tokens(root.body).count(name))
        for record in records.values():
            self.assertFalse(set(TARGET_TASKS) & set(tokens(record.body)))
            self.assertNotIn("thread_stack", tokens(record.body))
            self.assertNotIn("saved_status", tokens(record.body))
            self.assertNotIn("passed", tokens(record.body))
        self.assertEqual((), file_scope_objects(source(scenarios_path)))
        self.assertEqual((), file_scope_objects(header))
        self.assertEqual({}, c_function_definitions(header))

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

    def test_current_portable_dependencies_and_private_exports(self):
            records = {
                (path, name): record for path in CORE_SOURCES
                for name, record in function_records(source(path)).items()
            }
            owners = {name: path for path, name in records}
            self.assertEqual(len(records), len(owners))
            edges, exports = set(), set()
            for (path, name), record in records.items():
                references = set(tokens(c_lexical_text(record.body)))
                for callee in references & set(owners):
                    dependency = owners[callee]
                    if dependency == path:
                        continue
                    self.assertIn(CORE_SOURCES.index(dependency), CORE_DAG[CORE_SOURCES.index(path)], (name, callee))
                    edges.add((dependency, path))
                    if not callee.startswith("micros_"):
                        exports.add(callee)
            self.assertEqual(set(CORE_SOURCES), set(topological_order(set(CORE_SOURCES), edges)))
            declarations = re.findall(
                rf"\b({PRIVATE_PREFIX}\w+)\s*\([^;{{}}]*\)\s*;", c_lexical_text(source(PRIVATE_HEADERS[0])),
            )
            self.assertEqual(len(declarations), len(set(declarations)))
            self.assertEqual(exports, set(declarations))
            self.assertEqual((), file_scope_objects(source(PRIVATE_HEADERS[0])))
            for path in CORE_SOURCES:
                self.assertEqual((), file_scope_objects(source(path)), path)



if __name__ == "__main__":
    unittest.main()

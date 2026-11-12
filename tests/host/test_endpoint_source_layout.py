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

STRUCTURAL_TEST_PATH = "tests/host/test_endpoint_source_layout.py"

CORE_SOURCES = (
    'kernel/endpoint/endpoint.c',
    'kernel/endpoint/endpoint_codec.c',
    'kernel/endpoint/endpoint_identity.c',
    'kernel/endpoint/endpoint_registry.c',
    'kernel/endpoint/endpoint_privilege.c',
    'kernel/endpoint/endpoint_completion_validation.c',
    'kernel/endpoint/endpoint_queue_validation.c',
    'kernel/endpoint/endpoint_object_validation.c',
    'kernel/endpoint/endpoint_publication.c',
    'kernel/endpoint/endpoint_close.c',
)

SOURCE_SETS = {
    "MICROS_ENDPOINT_CORE_SOURCES": CORE_SOURCES,
    "MICROS_ENDPOINT_HOST_TEST_SOURCES": (
        "tests/host/endpoint_test.c",
        "tests/host/endpoint_test_fixture.c",
        "tests/host/endpoint_identity_test.c",
        "tests/host/endpoint_profile_test.c",
        "tests/host/endpoint_validation_test.c",
        "tests/host/endpoint_lifecycle_test.c",
        "tests/host/endpoint_source_only_test.c",
    ),
    "MICROS_ENDPOINT_MODEL_HOST_TEST_SOURCES": (
        "tests/host/endpoint_model_test.c",
        "tests/host/endpoint_model_fixture.c",
        "tests/host/endpoint_model_reference.c",
        "tests/host/endpoint_model_projection.c",
        "tests/host/endpoint_model_operations.c",
    ),
    "MICROS_ENDPOINT_QEMU_TEST_SOURCES": (
        'kernel/tests/endpoint/endpoint_test.c',
        'kernel/tests/endpoint/endpoint_test_fixture.c',
        'kernel/tests/endpoint/endpoint_test_scenarios.c',
    ),
}

PRIVATE_HEADERS = (
    'kernel/endpoint/endpoint_private.h',
    "tests/host/endpoint_test_internal.h",
    "tests/host/endpoint_model_internal.h",
    'kernel/tests/endpoint/endpoint_test_internal.h',
)

CORE_DAG = {
    0: {2, 4}, 1: set(), 2: {1}, 3: {1, 2}, 4: {1, 2},
    5: {1, 2}, 6: {1, 2, 5}, 7: {1, 2, 3, 4, 5, 6},
    8: {1, 2, 4, 6, 7}, 9: {1, 2, 6, 7},
}

def source(path):
    return source_bytes(ROOT, ROOT / path).decode()

def family_paths(family):
    return tuple(SOURCE_SETS.values())[family]

def owned_definitions(family):
    return {
        (path, name): lines
        for path in family_paths(family) if (ROOT / path).is_file()
        for name, lines in c_function_definitions(source(path)).items()
    }

PRIVATE_PREFIX = "endpoint_"
CONSUMERS = {
    target: tuple(list(SOURCE_SETS)[index] for index in families)
    for target, families in {
        "endpoint_test": (0, 1, 2), "ipc_model_test": (0, 2),
        "bootstrap_control_test": (0,), "pm_test": (0,), "micros": (0,),
    }.items()
}


class EndpointSourceLayoutTest(unittest.TestCase):
    def test_current_target_source_set_is_owned_by_its_configuration(self):
        blocks = cmake_if_blocks(source("CMakeLists.txt"), "MICROS_BUILD_ENDPOINT_TEST")
        self.assertEqual(1, sum(block.count("${MICROS_ENDPOINT_QEMU_TEST_SOURCES}") for block in blocks))

    def test_canonical_storage_has_its_reviewed_leaf_owner(self):
        names = {"storage_is_zero", "endpoint_storage_is_zero"}
        owners = [
            path for path in CORE_SOURCES
            if (ROOT / path).is_file()
            and names & set(c_function_definitions(source_bytes(ROOT, ROOT / path).decode()))
        ]
        self.assertEqual(['kernel/endpoint/endpoint_codec.c'], owners)

    def test_model_private_edges_keep_reference_independent_and_include_address_taken_predicates(self):
        paths = family_paths(2)
        definitions = owned_definitions(2)
        owners = {name: path for path, name in definitions}
        self.assertEqual(len(definitions), len(owners))
        allowed = {0: {1, 2, 3, 4}, 1: set(), 2: set(), 3: {1, 2}, 4: {1, 2}}
        edges = set()
        exports = set()
        for name, path in owners.items():
            referenced = set(re.findall(r"[A-Za-z_]\w*", c_lexical_text(c_function_body(source(path), name))))
            for callee in referenced & set(owners):
                dependency = owners[callee]
                if dependency == path:
                    continue
                self.assertIn(paths.index(dependency), allowed[paths.index(path)], (name, callee))
                edges.add((dependency, path))
                exports.add(callee)
        self.assertEqual(set(paths), set(topological_order(set(paths), edges)))
        declarations = re.findall(
            r"\b(endpoint_model_\w*)\s*\([^;{}]*\)\s*;", c_lexical_text(source(PRIVATE_HEADERS[2])),
        )
        self.assertEqual(len(declarations), len(set(declarations)))
        self.assertEqual(exports, set(declarations))

    def test_function_body_extraction_skips_earlier_calls_and_prototypes(self):
        expected = "{ if (owner != expected_owner) { return false; } return true; }"
        text = (
            "static bool selected(void); "
            "bool caller(void) { if (selected()) { return false; } return true; } "
            "static bool selected(void) " + expected
        )
        self.assertEqual(tokens(expected), tokens(c_function_body(text, "selected")))
        with self.assertRaises(AssertionError):
            c_function_body(text[:text.index("static bool selected(void) " + expected)], "selected")

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
                rf"\b({PRIVATE_PREFIX}\w+)\s*\([^;{{}}]*\)\s*;",
                c_lexical_text(source(PRIVATE_HEADERS[0]) + source("kernel/endpoint/endpoint_internal.h")),
            )
            self.assertEqual(len(declarations), len(set(declarations)))
            self.assertEqual(exports, set(declarations))
            self.assertEqual((), file_scope_objects(source(PRIVATE_HEADERS[0])))
            for path in CORE_SOURCES:
                self.assertEqual((), file_scope_objects(source(path)), path)



if __name__ == "__main__":
    unittest.main()

"""Current module, fixture, model and build ownership contracts."""

from pathlib import Path
import re
import unittest

from tests.host.build_source_records import cmake_commands
from tests.host.c_source_records import (
    c_function_body, c_function_definitions, c_lexical_text,
    file_scope_objects, function_records, matching_lexical, tokens,
)
from tests.host.source_snapshot import source_bytes
from tools.check_docs import topological_order
from tools import validation_plan


ROOT = Path(__file__).resolve().parents[2]

STRUCTURAL_TEST_PATH = "tests/host/test_tty_source_layout.py"

RUNTIME_SOURCES = (
    'kernel/tty/tty_handoff_runtime.c', 'kernel/tty/tty_handoff_runtime_state.c',
    'kernel/tty/tty_handoff_runtime_validate.c', 'kernel/tty/tty_handoff_runtime_transitions.c',
)

SERVICE_SOURCES = (
    "servers/tty/tty_service_core.c", "servers/tty/tty_service_validate.c",
    "servers/tty/tty_service_read.c", "servers/tty/tty_service_write.c",
    "servers/tty/tty_service_request.c",
)

MODEL_SOURCES = (
    "tests/host/tty_model_test.c", "tests/host/tty_model_common.c",
    "tests/host/tty_model_reference.c", "tests/host/tty_model_io.c",
    "tests/host/tty_model_protocol.c", "tests/host/tty_model_operations.c",
    "tests/host/tty_model_validate.c",
)

HANDOFF_SOURCES = (
    "tests/host/tty_handoff_test.c", "tests/host/tty_handoff_test_fixture.c",
    "tests/host/tty_handoff_device_test.c", "tests/host/tty_handoff_runtime_test.c",
)

CONTROL_SOURCES = (
    "tests/host/tty_control_syscall_test.c", "tests/host/tty_control_syscall_test_fixture.c",
    "tests/host/tty_control_syscall_test_observe.c", "tests/host/tty_control_syscall_test_controller.c",
)

SOURCE_SETS = {
    "MICROS_TTY_HANDOFF_RUNTIME_SOURCES": RUNTIME_SOURCES,
    "MICROS_TTY_SERVICE_CORE_SOURCES": SERVICE_SOURCES,
    "MICROS_TTY_MODEL_TEST_SOURCES": MODEL_SOURCES,
    "MICROS_TTY_HANDOFF_HOST_TEST_SOURCES": HANDOFF_SOURCES,
    "MICROS_TTY_CONTROL_SYSCALL_HOST_TEST_SOURCES": CONTROL_SOURCES,
}

PRIVATE_HEADERS = (
    'kernel/tty/tty_handoff_runtime_internal.h', "servers/tty/tty_service_internal.h",
    "tests/host/tty_model_internal.h", "tests/host/tty_handoff_test_internal.h",
    "tests/host/tty_control_syscall_test_internal.h",
)

RUNTIME_CONTEXT_GETTER = "tty_handoff_context"

RUNTIME_CONTEXT_FIELDS = (
    "struct micros_tty_handoff_runtime_state tty_handoff_state;",
    "const struct micros_bootstrap_control_state *tty_bootstrap_state;",
)

def source(path):
    return source_bytes(ROOT, ROOT / path).decode()

def check_model_owner_dependencies(records):
    allowed = {0: {1, 2, 3, 4, 5, 6}, 1: set(), 2: set(), 3: {2}, 4: {1, 3}, 5: {1, 2, 3, 4}, 6: {1, 2}}
    for name, (owner, record) in records.items():
        callees = {word for word in tokens(record.body) if word in records and records[word][0] != owner}
        destinations = {records[word][0] for word in callees}
        if "MODEL_EXPECT" in tokens(record.body) and owner != 1:
            destinations.add(1)
        if owner == 6 and any(
            records[word][0] == 2 and word != "tty_model_reference_output_pending" for word in callees
        ):
            raise AssertionError("the corrected validation/reference edge permits only the original final output observation")
        if not destinations <= allowed[owner]:
            raise AssertionError("model private owner direction changed: " + name)

def function_references(body, names):
    words = tokens(body)
    return {
        word for index, word in enumerate(words) if word in names
        and (index == 0 or words[index - 1] not in ("struct", "enum", "union", ".", "->"))
    }

CONSUMERS = {
    "micros": ("MICROS_TTY_HANDOFF_RUNTIME_SOURCES",),
    "tty_test": ("MICROS_TTY_HANDOFF_RUNTIME_SOURCES", "MICROS_TTY_HANDOFF_HOST_TEST_SOURCES"),
    "tty_service_test": ("MICROS_TTY_SERVICE_CORE_SOURCES",),
    "tty_model_test": ("MICROS_TTY_SERVICE_CORE_SOURCES", "MICROS_TTY_MODEL_TEST_SOURCES"),
    "tty_control_syscall_test": ("MICROS_TTY_CONTROL_SYSCALL_HOST_TEST_SOURCES",),
}


class TtySourceLayoutTest(unittest.TestCase):
    def test_native_fixtures_keep_separate_state_and_live_panic_targets(self):
        for paths, kind, getter in (
            (HANDOFF_SOURCES, "tty_handoff_test_fixture", "tty_handoff_test_fixture"),
            (CONTROL_SOURCES, "tty_control_syscall_test_fixture", "tty_control_syscall_test_fixture"),
        ):
            for path in paths:
                expected = (f"static struct {kind} fixture_state;",) if path == paths[1] else ()
                self.assertEqual(expected, file_scope_objects(source(path)), path)
            self.assertEqual(
                tokens("return &fixture_state;"),
                tokens(function_records(source(paths[1]))[getter].body),
            )
        roots = {
            "tty_control_syscall_test_invariant_failure_is_fatal_before_mutation",
            "tty_control_syscall_test_hardware_corruption_precedes_tuple_failure",
        }
        captures = []
        for path in CONTROL_SOURCES:
            for name, record in function_records(source(path)).items():
                count = len(re.findall(r"\bsetjmp\s*\(", c_lexical_text(record.body)))
                if count:
                    self.assertEqual(CONTROL_SOURCES[0], path)
                    self.assertEqual(1, count)
                    captures.append(name)
        self.assertEqual(roots, set(captures))

    def test_function_references_distinguish_actual_callbacks_from_separate_c_tag_and_member_namespaces(self):
        names = {"micros_tty_handoff_runtime_state"}
        self.assertEqual(set(), function_references(
            "{ state = (struct micros_tty_handoff_runtime_state){0}; }", names,
        ))
        self.assertEqual(set(), function_references(
            "{ record.micros_tty_handoff_runtime_state = 0; record->micros_tty_handoff_runtime_state = 1; }", names,
        ))
        self.assertEqual(names, function_references(
            "{ callback = &micros_tty_handoff_runtime_state; }", names,
        ))
        self.assertEqual(names, function_references("{ callback = micros_tty_handoff_runtime_state; }", names))
        self.assertEqual(names, function_references("{ return micros_tty_handoff_runtime_state(); }", names))

    def test_model_graph_admits_only_original_reference_observation_without_reverse_edge(self):
        owners = {
            1: "bool tty_model_failure(void) { return false; }",
            2: (
                "bool tty_model_reference_output_pending(void) { return original_pending; } "
                "void tty_model_reference_receive_byte(void) { original_mutation(); }"
            ),
            6: (
                "bool tty_model_validate(void) { MODEL_EXPECT(model, original_crlf); "
                "MODEL_EXPECT(model, !expect_lf || tty_model_reference_output_pending()); return true; }"
            ),
        }
        records = lambda sources: {
            name: (owner, record) for owner, text in sources.items()
            for name, record in function_records(text).items()
        }
        check_model_owner_dependencies(records(owners))
        for changed in (
            {**owners, 6: owners[6].replace("tty_model_reference_output_pending()", "tty_model_reference_receive_byte()")},
            {**owners, 2: owners[2].replace("original_pending", "tty_model_validate()")},
            {**owners, 1: owners[1].replace("false", "tty_model_reference_output_pending()")},
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                check_model_owner_dependencies(records(changed))

    def test_service_private_symbols_do_not_collide_with_retained_real_elf_neighbors(self):
        paths = (
            *SERVICE_SOURCES, "servers/tty/tty_service.c", "servers/tty/tty_control.c",
            "servers/tty/tty_core.c", "servers/tty/tty_uart.c",
        )
        locations = {}
        for path in paths:
            for name in function_records(source(path)):
                self.assertNotIn(name, locations, name + " also defined in " + locations.get(name, ""))
                locations[name] = path

    def test_service_private_directions_preserve_explicit_state_and_no_facade_cycle(self):
        records = {
            name: (owner, record) for owner, path in enumerate(SERVICE_SOURCES)
            for name, record in function_records(source(path)).items()
        }
        allowed = {0: {1, 2, 3, 4}, 1: set(), 2: {1, 4}, 3: {1, 4}, 4: {1}}
        for name, (owner, record) in records.items():
            destinations = {
                records[word][0] for word in tokens(record.body)
                if word in records and records[word][0] != owner
            }
            self.assertLessEqual(destinations, allowed[owner], name)

    def test_runtime_state_leaf_keeps_actual_owner_directions_acyclic(self):
        records = {
            name: (owner, record) for owner, path in enumerate(RUNTIME_SOURCES)
            for name, record in function_records(source(path)).items()
        }
        allowed = {0: {1, 2, 3}, 1: set(), 2: {1}, 3: {1}}
        for name, (owner, record) in records.items():
            body_tokens = tokens(record.body)
            destinations = {
                records[word][0] for index, word in enumerate(body_tokens)
                if word in records and records[word][0] != owner
                and (index == 0 or body_tokens[index - 1] not in ("struct", "union", "enum"))
            }
            self.assertLessEqual(destinations, allowed[owner], name)

    def test_runtime_context_has_only_two_original_fields_in_its_one_state_leaf(self):
        header = source(PRIVATE_HEADERS[0])
        expected = tokens("struct tty_handoff_context {" + "".join(RUNTIME_CONTEXT_FIELDS) + "};")
        self.assertIn(" ".join(expected), " ".join(tokens(header)))
        state = source(RUNTIME_SOURCES[1])
        self.assertEqual(
            ("static struct tty_handoff_context context_state;",), tuple(file_scope_objects(state)),
        )
        self.assertEqual(
            tokens("{ return &context_state; }"),
            tokens("{" + function_records(state)[RUNTIME_CONTEXT_GETTER].body + "}"),
        )
        for path in (RUNTIME_SOURCES[0], *RUNTIME_SOURCES[2:]):
            self.assertEqual((), tuple(file_scope_objects(source(path))), path)

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

    def test_current_reference_model_is_independent_and_state_is_local(self):
            records = {
                name: (owner, record) for owner, path in enumerate(MODEL_SOURCES)
                for name, record in function_records(source(path)).items()
            }
            check_model_owner_dependencies(records)
            for path in (MODEL_SOURCES[0], *MODEL_SOURCES[2:], *SERVICE_SOURCES):
                self.assertEqual((), file_scope_objects(source(path)), path)
            self.assertEqual(
                1, cmake_commands(source("CMakeLists.txt"), "micros_add_bootstrap_user", "micros_tty_service")[0]
                .count("${MICROS_TTY_SERVICE_CORE_SOURCES}"),
            )



if __name__ == "__main__":
    unittest.main()

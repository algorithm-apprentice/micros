"""Current RAMFS module, fixture, model and immutable-input contracts."""

from pathlib import Path
import hashlib
import json
import re
import unittest

from tests.host.build_source_records import cmake_commands, cmake_if_blocks
from tests.host.c_source_records import c_function_body, c_function_definitions, c_lexical_text, file_scope_objects
from tests.host.source_snapshot import source_bytes
from tests.host.test_vm_service_source_layout import local_include_closure
from tools.check_docs import topological_order
from tools import validation_plan


# Fixed OBJ02 #246 configuration data, not a production-function identity.
CURRENT_CONFIGURATION_DIGEST = "cda7294ec8e1e1ccf3a349e574517f3b6820c514a0d42fee8fce01e2bbe391b8"


ROOT = Path(__file__).resolve().parents[2]

STRUCTURAL_TEST_PATH = "tests/host/test_ramfs_source_layout.py"

SOURCE_SETS = {
    "MICROS_RAMFS_CORE_SOURCES": (
        "servers/ramfs/ramfs_core.c",
        "servers/ramfs/ramfs_codec.c",
        "servers/ramfs/ramfs_ownership.c",
        "servers/ramfs/ramfs_state.c",
        "servers/ramfs/ramfs_transfer.c",
        "servers/ramfs/ramfs_namespace.c",
        "servers/ramfs/ramfs_io.c",
        "servers/ramfs/ramfs_directory.c",
    ),
    "MICROS_RAMFS_HOST_TEST_SOURCES": (
        "tests/host/ramfs_test.c",
        "tests/host/ramfs_test_fixture.c",
        "tests/host/ramfs_protocol_test.c",
        "tests/host/ramfs_namespace_test.c",
        "tests/host/ramfs_io_test.c",
        "tests/host/ramfs_directory_test.c",
    ),
    "MICROS_RAMFS_SEED_HOST_TEST_SOURCES": (
        "tests/host/ramfs_seed_test.c",
        "tests/host/ramfs_seed_test_fixture.c",
        "tests/host/ramfs_seed_boundary_test.c",
        "tests/host/ramfs_seed_corruption_test.c",
    ),
    "MICROS_RAMFS_MODEL_HOST_TEST_SOURCES": (
        "tests/host/ramfs_model_test.c",
        "tests/host/ramfs_model_fixture.c",
        "tests/host/ramfs_model_reference.c",
        "tests/host/ramfs_model_projection.c",
        "tests/host/ramfs_model_protocol.c",
        "tests/host/ramfs_model_operations.c",
    ),
    "MICROS_RAMFS_QEMU_PEER_SOURCES": (
        "tests/qemu/ramfs_service_vfs.c",
        "tests/qemu/ramfs_service_vfs_fixture.c",
        "tests/qemu/ramfs_service_vfs_ramfs.c",
        "tests/qemu/ramfs_service_vfs_scenario.c",
        "tests/qemu/ramfs_service_vfs_tty.c",
    ),
    "MICROS_RAMFS_QEMU_REPORT_SOURCES": (
        'kernel/tests/ramfs/ramfs_service_test.c',
        'kernel/tests/ramfs/ramfs_service_test_fixture.c',
        'kernel/tests/ramfs/ramfs_service_test_report.c',
    ),
}

PRIVATE_HEADERS = (
    "servers/ramfs/ramfs_internal.h",
    "tests/host/ramfs_test_internal.h",
    "tests/host/ramfs_seed_test_internal.h",
    "tests/host/ramfs_model_internal.h",
    "tests/qemu/ramfs_service_vfs_internal.h",
    'kernel/tests/ramfs/ramfs_service_test_internal.h',
)

FUNCTIONAL_LOOKUP_READONLY = (
    'static const uint8_t absolute_motd[] = "/etc/motd"; '
    'static const uint8_t relative_motd[] = "etc//./motd"; '
    'static const uint8_t empty[] = ""; '
    'static const uint8_t parent_path[] = "../etc/motd"; '
    'static const uint8_t confined_parent[] = "../motd"; '
    'static const uint8_t trailing[] = "/etc/motd/"; '
    'static const uint8_t missing[] = "/missing"; '
    'static const uint8_t dot[] = "."; '
    'static const uint8_t dotdot[] = ".."; '
    'static const uint8_t parent_name[] = "../name";'
)

PEER_READONLY_STATICS = (
    'static const uint8_t motd_path[] = "/etc/motd"; '
    'static const uint8_t motd[] = MICROS_RAMFS_TEST_MOTD; '
    'static const uint8_t tmp_name[] = "tmp"; '
    'static const uint8_t note_name[] = "note"; '
    'static const uint8_t note_data[] = MICROS_RAMFS_TEST_NOTE_DATA; '
    "static const uint8_t sparse_read[] = { 0, 0, 0, 'n', 'o', 't', 'e', };"
)

MODEL_EXPECT_WITH_FILE = r"""#define MODEL_EXPECT(selected, expression) \
    do { \
        if (!(expression)) { \
            return ramfs_model_model_failure( \
                (selected), \
                #expression, \
                __FILE__, \
                __LINE__ \
            ); \
        } \
    } while (false)"""

def source(path):
    return source_bytes(ROOT, ROOT / path).decode()

def digest_bytes(value):
    return hashlib.sha256(value).hexdigest()

def body_tokens(text, renames=None):
    renames = {} if renames is None else renames
    pattern = r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[A-Za-z_]\w*|\d+|[^\s]'
    return tuple(
        renames.get(token, token) for token in re.findall(pattern, text)
        if not token.startswith(("//", "/*"))
    )

def body_digest(text, renames=None):
    return digest_bytes(json.dumps(body_tokens(text, renames), separators=(",", ":")).encode())

def raw_const_objects(text, names):
    lexical = c_lexical_text(text)
    result = []
    for name in names:
        matches = tuple(re.finditer(
            rf"(?m)^(?:static\s+)?const\s+struct\s+\w+\s+{re.escape(name)}\b"
            r"\s*(?:\[[^\]]*\]\s*)?=\s*\{",
            lexical,
        ))
        if len(matches) != 1:
            raise AssertionError(f"expected one raw const initializer: {name}")
        match = matches[0]
        depth = 0
        for end in range(match.end() - 1, len(lexical)):
            if lexical[end] == "{":
                depth += 1
            elif lexical[end] == "}":
                depth -= 1
                if depth == 0:
                    break
        else:
            raise AssertionError(f"unterminated const initializer: {name}")
        end += 1
        while end < len(lexical) and lexical[end].isspace():
            end += 1
        if end == len(lexical) or lexical[end] != ";":
            raise AssertionError(f"missing const declaration terminator: {name}")
        result.append(text[match.start():end + 1])
    return tuple(result)

def family_paths(family):
    return tuple(SOURCE_SETS.values())[family]

def owned_definitions(family):
    return {
        (path, name): lines
        for path in family_paths(family) if (ROOT / path).is_file()
        for name, lines in c_function_definitions(source(path)).items()
    }

def require_model_failure_macro(macro):
    if body_tokens(macro) != body_tokens(MODEL_EXPECT_WITH_FILE):
        raise AssertionError("model failures must carry the actual caller file/line and immediate exit")

def readonly_case_data(text, expected):
    lexical = c_lexical_text(text)
    objects = []
    for match in re.finditer(r"(?m)^\s*static\s+const\s+uint8_t\s+[A-Za-z_]\w*\s*\[\s*\]\s*=", lexical):
        if lexical.count("{", 0, match.start()) != lexical.count("}", 0, match.start()):
            continue
        end = lexical.find(";", match.end())
        if end < 0:
            raise AssertionError("unterminated readonly fixture array")
        objects.append(text[match.start():end + 1])
    data = "\n".join(objects)
    if body_tokens(data) != body_tokens(expected):
        raise AssertionError("readonly fixture array values, lifetime or membership changed")
    return data

def peer_readonly_data(text):
    return readonly_case_data(text, PEER_READONLY_STATICS)

def functional_readonly_objects(text, owner):
    if owner == 3:
        readonly_case_data(text, FUNCTIONAL_LOOKUP_READONLY)
        return file_scope_objects(FUNCTIONAL_LOOKUP_READONLY)
    return ()

class RamfsSourceLayoutTest(unittest.TestCase):
    def test_portable_private_contract_preserves_leaf_direction_and_no_new_authority(self):
        paths = family_paths(0)
        definitions = owned_definitions(0)
        owners = {name: path for path, name in definitions}
        self.assertEqual(len(definitions), len(owners))
        edges = set()
        exports = set()
        allowed = {
            0: {1, 2, 3, 5, 6, 7},
            1: set(),
            2: {1},
            3: {1, 2},
            4: set(),
            5: {1, 2, 3, 4},
            6: {1, 2, 3, 4},
            7: {1, 2, 3, 4},
        }
        for (path, name) in definitions:
            body = c_lexical_text(c_function_body(source(path), name))
            for callee in re.findall(r"\b([A-Za-z_]\w*)\s*\(", body):
                if callee not in owners or owners[callee] == path:
                    continue
                dependency = owners[callee]
                self.assertIn(paths.index(dependency), allowed[paths.index(path)])
                edges.add((dependency, path))
                if not callee.startswith("micros_"):
                    exports.add(callee)
        self.assertEqual(set(paths), set(topological_order(set(paths), edges)))
        header = source(PRIVATE_HEADERS[0])
        self.assertEqual({}, c_function_definitions(header))
        self.assertEqual((), file_scope_objects(header))
        declarations = re.findall(
            r"\b(ramfs_[A-Za-z_]\w*)\s*\([^;{}]*\)\s*;", c_lexical_text(header),
        )
        self.assertEqual(len(declarations), len(set(declarations)))
        self.assertEqual(exports, set(declarations))
        self.assertEqual(
            ["MICROS_SERVERS_RAMFS_INTERNAL_H"],
            re.findall(r"(?m)^#define\s+(\w+)", header),
        )
        fields = re.search(r"struct ramfs_result_fields\s*\{([^{}]*)\}", header)
        self.assertIsNotNone(fields)
        self.assertEqual(
            body_tokens("uint64_t node; uint64_t file_size; uint64_t position; uint32_t count; uint32_t mode;"),
            body_tokens(fields.group(1)),
        )
        self.assertIn(
            "#define MICROS_RAMFS_INITIALIZATION_MAGIC \\\n"
            "    UINT64_C(0x4d4943524f535246)",
            source(paths[3]),
        )
        for path in paths:
            self.assertEqual((), file_scope_objects(source(path)))

    def test_functional_fixture_types_lifetime_and_only_borrow_are_exact(self):
        paths = family_paths(1)
        header = source(PRIVATE_HEADERS[1])
        expected_fields = {
            "test_bus": (
                "uint8_t storage[TEST_GRANT_CAPACITY]; bool reject; bool invariant; "
                "size_t calls; enum micros_ramfs_copy_direction last_direction; "
                "uint64_t last_offset; size_t last_length;"
            ),
            "ramfs_test_fixture": (
                "uint8_t seed_image[MICROS_RAMFS_SEED_IMAGE_MAX]; size_t seed_image_size; "
                "struct micros_ramfs_state state; struct micros_ramfs_state state_snapshot; "
                "struct micros_ramfs_state corrupt_state; "
                "uint8_t storage_snapshot[TEST_GRANT_CAPACITY]; struct test_bus bus;"
            ),
        }
        for tag, fields in expected_fields.items():
            record = re.findall(rf"\bstruct\s+{tag}\s*\{{([^{{}}]*)\}}", header)
            self.assertEqual(1, len(record), tag)
            self.assertEqual(body_tokens(fields), body_tokens(record[0]), tag)
        self.assertIn("TEST_GRANT = 1,", header)
        self.assertIn("TEST_GRANT_CAPACITY = 8192,", header)


        expected_objects = (
            "static struct ramfs_test_fixture test_fixture;",
            "const micros_endpoint_t ramfs_test_vfs_endpoint = UINT32_C(0x00001005);",
            "const struct micros_ramfs_io ramfs_test_io = { "
            ".copy = test_copy, .context = &test_fixture.bus, };",
        )
        for path in paths:
            expected = (
                expected_objects if path == paths[1]
                else functional_readonly_objects(source(path), paths.index(path))
            )
            self.assertEqual(
                tuple(map(body_tokens, expected)),
                tuple(map(body_tokens, file_scope_objects(source(path)))),
                path,
            )
        self.assertEqual(
            body_tokens("{ return &test_fixture; }"),
            body_tokens(c_function_body(source(paths[1]), "ramfs_test_context")),
        )


        self.assertEqual({}, c_function_definitions(header))
        self.assertEqual(
            tuple(map(body_tokens, (
                "extern const micros_endpoint_t ramfs_test_vfs_endpoint;",
                "extern const struct micros_ramfs_io ramfs_test_io;",
            ))),
            tuple(map(body_tokens, re.findall(r"(?m)^extern[^;]*;", header))),
        )
        self.assertEqual((), file_scope_objects(header))

    def test_seed_fixture_retains_separate_images_and_automatic_rejected_plans(self):
        paths = family_paths(2)
        header = source(PRIVATE_HEADERS[2])
        fields = re.findall(r"struct ramfs_seed_test_fixture\s*\{([^{}]*)\}", header)
        self.assertEqual(1, len(fields))
        self.assertEqual(
            body_tokens(
                "uint8_t seed_image[MICROS_RAMFS_SEED_IMAGE_MAX]; "
                "uint8_t mutated_image[MICROS_RAMFS_SEED_IMAGE_MAX]; size_t seed_image_size;"
            ),
            body_tokens(fields[0]),
        )
        for path in paths:
            objects = ("static struct ramfs_seed_test_fixture seed_fixture;",) if path == paths[1] else ()
            self.assertEqual(
                tuple(map(body_tokens, objects)),
                tuple(map(body_tokens, file_scope_objects(source(path)))),
            )


        self.assertEqual(
            body_tokens("{ return &seed_fixture; }"),
            body_tokens(c_function_body(source(paths[1]), "ramfs_seed_test_context")),
        )


        self.assertEqual({}, c_function_definitions(header))
        self.assertEqual((), file_scope_objects(header))
        rejection = c_function_body(source(paths[1]), "ramfs_seed_test_expect_rejection")
        self.assertIn("struct micros_ramfs_seed_plan expected_plan;", rejection)
        self.assertIn("struct micros_ramfs_seed_plan plan;", rejection)
        self.assertNotIn("static struct micros_ramfs_seed_plan", rejection)

    def test_model_types_seed_lifetime_and_callback_identity_are_exact(self):
        paths = family_paths(3)
        header = source(PRIVATE_HEADERS[3])


        seed_fields = re.findall(r"struct ramfs_model_seed\s*\{([^{}]*)\}", header)
        self.assertEqual(1, len(seed_fields))
        self.assertEqual(
            body_tokens("uint8_t seed_image[MICROS_RAMFS_SEED_IMAGE_MAX]; size_t seed_image_size;"),
            body_tokens(seed_fields[0]),
        )
        expected_objects = (
            "static struct ramfs_model model;",
            "static struct ramfs_model_seed seed_fixture;",
            "const struct micros_ramfs_io ramfs_model_io = { .copy = model_copy, .context = &model, };",
        )
        for path in paths:
            expected = expected_objects if path == paths[1] else ()
            self.assertEqual(
                tuple(map(body_tokens, expected)),
                tuple(map(body_tokens, file_scope_objects(source(path)))),
                path,
            )
            self.assertNotIn('"servers/ramfs/ramfs_internal.h"', source(path))
        for getter, storage in (
            ("ramfs_model_context", "model"),
            ("ramfs_model_seed_context", "seed_fixture"),
        ):
            self.assertEqual(
                body_tokens("{ return &" + storage + "; }"),
                body_tokens(c_function_body(source(paths[1]), getter)),
            )


        self.assertEqual({}, c_function_definitions(header))
        self.assertEqual((), file_scope_objects(header))
        self.assertIn("const struct ramfs_model_seed *ramfs_model_seed_context(void);", header)
        self.assertIn("    const char *file_name,\n    int line", header)

    def test_independent_model_dependency_graph_has_no_protocol_backedge(self):
        paths = family_paths(3)
        definitions = owned_definitions(3)
        owners = {name: path for path, name in definitions}
        self.assertEqual(len(owners), len(definitions))
        allowed = {0: {1, 2, 3, 4, 5}, 1: set(), 2: {1}, 3: {1, 2}, 4: {1, 2, 3}, 5: {1, 2, 4}}
        edges = set()
        exports = set()
        for path, name in definitions:
            calls = set(re.findall(
                r"\b([A-Za-z_]\w*)\s*\(", c_lexical_text(c_function_body(source(path), name)),
            ))
            if "MODEL_EXPECT" in calls:
                calls.add("ramfs_model_model_failure")
            for callee in calls & set(owners):
                dependency = owners[callee]
                if dependency == path:
                    continue
                self.assertIn(paths.index(dependency), allowed[paths.index(path)], (path, callee))
                edges.add((dependency, path))
                exports.add(callee)
        self.assertEqual(set(paths), set(topological_order(set(paths), edges)))
        header = source(PRIVATE_HEADERS[3]).split("#define MODEL_EXPECT", 1)[0]
        declarations = re.findall(r"\b(ramfs_model_\w*)\s*\([^;{}]*\)\s*;", c_lexical_text(header))
        self.assertEqual(len(declarations), len(set(declarations)))
        self.assertEqual(exports, set(declarations))
        writer = c_function_body(source(paths[2]), "reference_write_record")
        self.assertIn("ramfs_model_write_u32_le", writer)
        self.assertIn("ramfs_model_write_u64_le", writer)

    def test_model_failure_payload_uses_actual_caller_location_and_preserves_trace(self):
        header = source(PRIVATE_HEADERS[3])
        macro = header[header.index("#define MODEL_EXPECT"):header.rindex("#endif")].strip()
        require_model_failure_macro(macro)
        for changed in (
            macro.replace("__FILE__", '"old-owner.c"'),
            macro.replace("#expression,", "__FILE__,").replace("                __FILE__, \\\n                __LINE__", "                #expression, \\\n                __LINE__"),
            macro.replace("return ramfs_model_model_failure", "ramfs_model_model_failure"),
            macro.replace("__LINE__", "__LINE__ + 1"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                require_model_failure_macro(changed)

    def test_peer_preserves_abi_initialized_data_and_only_four_bss_fields(self):
        paths = family_paths(4)
        header = source(PRIVATE_HEADERS[4])


        fields = re.findall(r"struct vfs_service_fixture\s*\{([^{}]*)\}", header)
        self.assertEqual(1, len(fields))
        self.assertEqual(body_tokens(
            "uint8_t vfs_ramfs_buffer[MICROS_RAMFS_TRANSFER_MAX]; uint8_t vfs_tty_buffer[256]; "
            "micros_endpoint_t vfs_ramfs_endpoint; micros_endpoint_t vfs_tty_endpoint;"
        ), body_tokens(fields[0]))
        objects = (
            "volatile struct micros_bootstrap_service_config micros_bootstrap_service_config;",
            "static struct vfs_service_fixture vfs_fixture;",
            "static volatile uint64_t vfs_initialized_data = UINT64_C(0x52414d4653564653);",
        )
        for path in paths:
            if path == paths[1]:
                expected = objects
            elif path == paths[3]:
                peer_readonly_data(source(path))
                expected = file_scope_objects(PEER_READONLY_STATICS)
            else:
                expected = ()
            self.assertEqual(
                tuple(map(body_tokens, expected)),
                tuple(map(body_tokens, file_scope_objects(source(path)))),
            )
        for getter, expression in (
            ("vfs_service_context", "&vfs_fixture"),
            ("vfs_initialized_data_value", "vfs_initialized_data"),
        ):
            self.assertEqual(
                body_tokens("{ return " + expression + "; }"),
                body_tokens(c_function_body(source(paths[1]), getter)),
            )


        self.assertIn("uint64_t vfs_initialized_data_value(void);", header)
        self.assertIn("void micros_ramfs_service_test_report(uint64_t magic);", header)

    def test_raw_const_extraction_preserves_strings_and_rejects_ambiguous_bounds(self):
        declaration = 'static const struct record profiles[] = { { .name = "quoted }; text" } };'
        self.assertEqual((declaration,), raw_const_objects(declaration, ("profiles",)))
        changed = declaration.replace("quoted }; text", "different")
        self.assertNotEqual(
            body_digest(raw_const_objects(declaration, ("profiles",))[0]),
            body_digest(raw_const_objects(changed, ("profiles",))[0]),
        )
        for invalid in (declaration[:-1], declaration + "\n" + declaration):
            with self.subTest(invalid=invalid), self.assertRaises(AssertionError):
                raw_const_objects(invalid, ("profiles",))

    def test_private_headers_admit_only_owned_contracts_and_existing_assertion_macros(self):
        guards = (
            ("MICROS_SERVERS_RAMFS_INTERNAL_H",),
            ("MICROS_TESTS_HOST_RAMFS_TEST_INTERNAL_H", "EXPECT_TRUE"),
            ("MICROS_TESTS_HOST_RAMFS_SEED_TEST_INTERNAL_H", "EXPECT_TRUE"),
            ("MICROS_TESTS_HOST_RAMFS_MODEL_INTERNAL_H", "MODEL_EXPECT"),
            ("MICROS_TESTS_QEMU_RAMFS_SERVICE_VFS_INTERNAL_H",),
            ("MICROS_KERNEL_RAMFS_SERVICE_TEST_INTERNAL_H",),
        )
        externs = (
            (),
            ("extern const micros_endpoint_t ramfs_test_vfs_endpoint;", "extern const struct micros_ramfs_io ramfs_test_io;"),
            (),
            ("extern const struct micros_ramfs_io ramfs_model_io;",),
            (),
            ("extern const struct micros_bootstrap_runtime_config ramfs_test_runtime_config;",),
        )
        private = set(PRIVATE_HEADERS)
        for family, header_path in enumerate(PRIVATE_HEADERS):
            header = source(header_path)
            self.assertEqual(guards[family], tuple(re.findall(r"(?m)^#define\s+(\w+)", header)))
            self.assertEqual(tuple(map(body_tokens, externs[family])), tuple(map(
                body_tokens, re.findall(r"(?m)^extern[^;]*;", header),
            )))
            self.assertEqual({}, c_function_definitions(header))
            self.assertEqual((), file_scope_objects(header))
            for path in (*family_paths(family), header_path):
                closure = set(local_include_closure(path))
                self.assertEqual({header_path}, closure & private, path)
                self.assertEqual(set(), {item for item in closure if item.endswith(".c") and item != path})

    def test_current_source_sets_and_complete_consumers(self):
        cmake = source("CMakeLists.txt")
        for name, paths in SOURCE_SETS.items():
            blocks = cmake_commands(cmake, "set", name)
            self.assertEqual(1, len(blocks))
            self.assertEqual(paths, tuple(re.findall(r"(?:kernel|servers|tests)/[A-Za-z0-9_/]+\.c", blocks[0])))
            self.assertTrue(all((ROOT / path).is_file() for path in paths))
        for target, names in {
            "ramfs_seed_test": ("MICROS_RAMFS_SEED_HOST_TEST_SOURCES",),
            "ramfs_test": ("MICROS_RAMFS_CORE_SOURCES", "MICROS_RAMFS_HOST_TEST_SOURCES"),
            "ramfs_model_test": ("MICROS_RAMFS_CORE_SOURCES", "MICROS_RAMFS_MODEL_HOST_TEST_SOURCES"),
        }.items():
            block, = cmake_commands(cmake, "add_executable", target)
            for name in SOURCE_SETS:
                self.assertEqual(int(name in names), block.count("${" + name + "}"))
        for target, name in (
            ("micros_ramfs_service", "MICROS_RAMFS_CORE_SOURCES"),
            ("micros_ramfs_service_vfs", "MICROS_RAMFS_QEMU_PEER_SOURCES"),
        ):
            block, = cmake_commands(cmake, "micros_add_bootstrap_user", target)
            self.assertEqual(1, block.count("${" + name + "}"))
        blocks = cmake_if_blocks(cmake, "MICROS_BUILD_RAMFS_SERVICE_TEST")
        self.assertEqual(1, sum(block.count("${MICROS_RAMFS_QEMU_REPORT_SOURCES}") for block in blocks))

    def test_current_configuration_data_has_one_readonly_owner(self):
        paths = family_paths(5)
        objects = raw_const_objects(
            source(paths[1]), ("profiles", "expected_services", "policies", "ramfs_test_runtime_config"),
        )
        self.assertEqual(CURRENT_CONFIGURATION_DIGEST, body_digest("\n".join(objects)))
        self.assertEqual(4, len(file_scope_objects(source(paths[1]))))
        self.assertEqual((), file_scope_objects(source(paths[0])))
        self.assertEqual((), file_scope_objects(source(paths[2])))

    def test_readonly_inputs_ignore_unrelated_functions_but_not_changed_values(self):
        declaration = 'static const uint8_t input[] = "quoted ; value";'
        source = "static bool unrelated(void) { return true; }\n" + declaration
        self.assertEqual(declaration, readonly_case_data(source, declaration))
        for changed in (
            source.replace("value", "other"),
            source.replace("static const", "const"),
            source + "\nstatic const uint8_t extra[] = {0};",
            "void nested(void) {\n" + declaration + "\n}",
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                readonly_case_data(changed, declaration)

    def test_current_paths_keep_broad_validation_classification(self):
        for family, paths in enumerate(SOURCE_SETS.values()):
            for path in (*paths[1:], PRIVATE_HEADERS[family]):
                for tier in ("fast", "pr"):
                    self.assertEqual(
                        validation_plan.plan([paths[0]], tier, "main"),
                        validation_plan.plan([path], tier, "main"), path,
                    )
        self.assertIn(STRUCTURAL_TEST_PATH, validation_plan.HOST_UNIT_ONLY_INPUTS)


if __name__ == "__main__":
    unittest.main()

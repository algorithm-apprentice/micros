"""Current ELF-checker interfaces, artifacts and dependency closure."""

import ast
import copy
import hashlib
import importlib
from pathlib import Path
import re
import unittest
from unittest import mock

from tests.host.ast_evidence import ast_key
from tests.host.build_source_records import cmake_command
from tests.host.source_snapshot import source_bytes
from tools import validation_plan


def current_text(path):
    return source_bytes(ROOT, path).decode()


ROOT = Path(__file__).resolve().parents[2]

ENTRY = "tools/check_user_elf.py"

TEST_ENTRY = "tests/host/test_check_user_elf.py"

FORMAT = "tools/user_elf_format.py"

PARSE = "tools/user_elf_parse.py"

STRUCTURE = "tools/user_elf_structure.py"

INSTRUCTIONS = "tools/user_elf_instructions.py"

POLICY = "tools/user_elf_policy.py"

FIXTURE = "tests/host/user_elf_test_fixture.py"

PARSE_CASES = "tests/host/user_elf_parse_cases.py"

STRUCTURE_CASES = "tests/host/user_elf_structure_cases.py"

POLICY_CASES = "tests/host/user_elf_policy_cases.py"

CLI_CASES = "tests/host/user_elf_cli_cases.py"

GATE = "tests/host/test_user_elf_source_layout.py"

PRODUCTION = (ENTRY, FORMAT, PARSE, STRUCTURE, INSTRUCTIONS, POLICY)

HOST = (TEST_ENTRY, FIXTURE, PARSE_CASES, STRUCTURE_CASES, POLICY_CASES, CLI_CASES)

FUNCTIONS = {
    ENTRY: ("validate_elf", "load_validated_elf", "main"),
    FORMAT: (
        "_bounded_slice", "_cstring", "_defined_symbols", "defined_symbols",
        "_program_file_bytes", "_section_by_name",
    ),
    PARSE: (
        "_parse_header", "_parse_programs", "_parse_raw_sections",
        "_parse_sections", "_parse_symbols", "parse_elf", "load_elf",
    ),
    STRUCTURE: ("_validate_header", "_validate_programs", "_validate_sections"),
    INSTRUCTIONS: (
        "_sign_extend", "_compressed_destination", "_instruction_destination",
        "_instruction_writes",
    ),
    POLICY: (
        "_validate_symbols", "_validate_instructions", "resident_page_count",
        "validate_resident_page_limit",
    ),
    FIXTURE: (
        "align", "make_string_table", "build_elf", "add_symbol", "validation_errors",
    ),
    PARSE_CASES: (
        "test_rejects_header_identity_flags_and_entry",
        "test_rejects_truncated_or_malformed_tables",
    ),
    STRUCTURE_CASES: (
        "test_rejects_forbidden_program_headers_and_bad_load_shape",
        "test_rejects_load_overlap_size_and_range",
        "test_rejects_forbidden_allocatable_runtime_sections",
        "test_rejects_relocation_sections",
        "test_rejects_bss_or_allocatable_section_mismatch",
        "test_rejects_shortened_text_and_file_backed_bss",
    ),
    POLICY_CASES: (
        "test_accepts_exact_freestanding_shape",
        "test_enforces_complete_resident_page_budget",
        "test_rejects_undefined_symbols",
        "test_rejects_raw_stub_startup_return_and_gp_tp_mutations",
    ),
}

FORMAT_DATA = (
    "ELF_HEADER", "PROGRAM_HEADER", "SECTION_HEADER", "SYMBOL",
    "ELF_CLASS_64", "ELF_DATA_LITTLE", "ELF_VERSION_CURRENT",
    "ELF_TYPE_EXECUTABLE", "ELF_MACHINE_RISCV", "ELF_FLAG_RISCV_RVC",
    "PROGRAM_LOAD", "PROGRAM_DYNAMIC", "PROGRAM_INTERP", "PROGRAM_TLS",
    "PROGRAM_EXECUTE", "PROGRAM_WRITE", "PROGRAM_READ", "SECTION_NULL",
    "SECTION_PROGBITS", "SECTION_SYMTAB", "SECTION_STRTAB", "SECTION_RELA",
    "SECTION_NOBITS", "SECTION_REL", "SECTION_DYNSYM", "SECTION_WRITE",
    "SECTION_ALLOC", "SECTION_EXECUTE", "SYMBOL_BIND_GLOBAL",
    "SYMBOL_UNDEFINED", "PAGE_SIZE", "USER_BASE", "USER_END",
    "MINIMUM_STACK_BOTTOM",
)

DATA = {
    FORMAT: FORMAT_DATA,
    STRUCTURE: (
        "FORBIDDEN_PROGRAM_TYPES", "FORBIDDEN_SECTION_NAMES",
        "FORBIDDEN_SECTION_PREFIXES",
    ),
    POLICY: ("REQUIRED_SYMBOLS",),
}

CLASSES = {
    FORMAT: (
        "ElfFormatError", "ElfHeader", "ProgramHeader", "Section", "ElfSymbol",
        "ElfImage",
    ),
}

CASE_CLASSES = {
    PARSE_CASES: "UserElfParseCases",
    STRUCTURE_CASES: "UserElfStructureCases",
    POLICY_CASES: "UserElfPolicyCases",
    CLI_CASES: "UserElfCliCases",
}

LAYOUT_FIELDS = (
    "base", "text_offset", "rodata_offset", "data_offset", "relocation_offset",
    "symbol_offset", "string_offset", "section_string_offset", "section_offset",
    "section_count", "symbol_section_index", "string_section_index",
    "section_string_index",
)

FIXTURE_OUTPUTS = (
    "MICROS_USER_RUNTIME_FIXTURE_SOURCE", "MICROS_PM_SERVICE_FIXTURE_SOURCE",
    "MICROS_VM_SCRATCH_PREEMPTION_FIXTURE_SOURCE",
    "MICROS_VFS_SERVICE_FIXTURE_SOURCE", "MICROS_RAMFS_SERVICE_FIXTURE_SOURCE",
    "MICROS_TTY_SERVICE_FIXTURE_SOURCE", "MICROS_VM_HANDOFF_FIXTURE_SOURCE",
    "MICROS_BOOTSTRAP_FIXTURE_SOURCE",
)

CHECKER_QEMU = (
    "test-qemu-user-runtime", "test-qemu-bootstrap-launcher",
    "test-qemu-vm-handoff", "test-qemu-pm-service",
    "test-qemu-vm-scratch-preemption", "test-qemu-tty", "test-qemu-ramfs",
    "test-qemu-vfs", "test-qemu-elf-spawn", "test-qemu-vm-ready-early", "test-qemu-vm-self-fault",
    "test-qemu-vm-self-fault-sealed", "test-qemu-bootstrap-ready-timeout",
    "test-qemu-bootstrap-manifest-panic",
)

CHECKER_IMAGES = (
    "build-tty-service-image", "build-ramfs-service-image", "build-vfs-service-image",
    "build-application-images",
)

FIXTURE_BYTES = (
    (".data", False, False, 13552, 13040, 12296,
     "2d678b43c9b6f03107827e6c447a6a80d38dec113f0ccea56ffc7cc0cd342be7"),
    (".data", False, True, 13592, 13080, 12296,
     "f9614cd78cc3cf26df3f8b6eac9e638a66cd35bab24240f56e48c9579d039789"),
    (".data", True, False, 13656, 13080, 12320,
     "b02f235ea2ac0aef4141c70d7b88a11603a306ed601323876b3ad419733aed06"),
    (".data", True, True, 13696, 13120, 12320,
     "49608d4e14c187e242f52a89355aa57f38325cf1fc8f72a15afbfd7b6f5f0b77"),
    (".got", False, False, 13552, 13040, 12296,
     "76ece86afa1d440e14d436222896fd2b0e75072043f82f08b67b591fa92c2bc4"),
    (".got", False, True, 13592, 13080, 12296,
     "7b9224c5e2a61a8f6652966e60679179dc47394577bdcec10a5a0222b56ed083"),
    (".got", True, False, 13656, 13080, 12320,
     "cc619fbe97daf1e8f806a1a4906ed458834bf3fb7676148a86b320798a1d49ba"),
    (".got", True, True, 13696, 13120, 12320,
     "ddeb102fff69d95e968140ed912df3eeedf13fcfd164746e8b87a2fba418c4fd"),
)

def parse_source(text):
    return ast.parse(text)

def definition_nodes(text, name):
    nodes = []
    for node in ast.walk(parse_source(text)):
        if isinstance(node, (ast.FunctionDef, ast.ClassDef)) and node.name == name:
            nodes.append(node)
        elif isinstance(node, ast.Assign):
            if any(isinstance(target, ast.Name) and target.id == name
                   for target in node.targets):
                nodes.append(node)
    return nodes

def one_definition(texts, path, name):
    nodes = definition_nodes(texts.get(path, ""), name)
    if len(nodes) != 1:
        raise AssertionError(f"{path}: {name} needs one definition, got {len(nodes)}")
    return copy.deepcopy(nodes[0])

def expected_owners():
    owners = {}
    for inventory in (FUNCTIONS, DATA, CLASSES):
        for path, names in inventory.items():
            for name in names:
                if name in owners:
                    raise AssertionError(f"duplicate inventory name: {name}")
                owners[name] = path
    return owners

def ownership_errors(texts):
    errors = []
    for name, owner in expected_owners().items():
        found = [path for path, text in texts.items() for _ in definition_nodes(text, name)]
        if found != [owner]:
            errors.append(f"wrong or duplicate owner: {name}: {found}")
    return errors

def import_errors(texts):
    allowed = {
        ENTRY: set(PRODUCTION[1:]), FORMAT: set(), PARSE: {FORMAT},
        STRUCTURE: {FORMAT}, INSTRUCTIONS: {FORMAT}, POLICY: {FORMAT, INSTRUCTIONS},
    }
    errors = []
    for path in PRODUCTION:
        for node in ast.walk(parse_source(texts.get(path, ""))):
            if isinstance(node, ast.Import):
                references = [alias.name for alias in node.names]
            elif isinstance(node, ast.ImportFrom):
                if any(alias.name == "*" for alias in node.names):
                    errors.append(f"wildcard import: {path}")
                module = node.module or ""
                if node.level:
                    package = path.split("/")[:-1]
                    if node.level > len(package):
                        errors.append(f"out-of-package relative import: {path}")
                        continue
                    prefix = package[:len(package) - node.level + 1]
                    module = ".".join((*prefix, *module.split("."))) if module else ".".join(prefix)
                references = (
                    ["tools." + alias.name for alias in node.names]
                    if module == "tools" else [module]
                )
            else:
                continue
            for module in references:
                referenced = module.replace(".", "/") + ".py"
                test_code = module == "tests" or module.startswith("tests.")
                wrong_owner = module.startswith("tools.") and referenced not in allowed[path]
                if test_code or wrong_owner:
                    errors.append(f"forbidden import: {path} -> {referenced}")
    return errors

def build_errors(text):
    errors = []
    try:
        block = cmake_command(text, "set", "MICROS_USER_ELF_CHECKER_SOURCES")
    except AssertionError as error:
        return [str(error)]
    expected = "set(MICROS_USER_ELF_CHECKER_SOURCES " + " ".join(
        '"${CMAKE_CURRENT_SOURCE_DIR}/' + path + '"' for path in PRODUCTION
    ) + ")"
    if "".join(block.split()) != "".join(expected.split()):
        errors.append("checker source set is not the exact ordered complete family")
    variable = "${MICROS_USER_ELF_CHECKER_SOURCES}"
    application = cmake_command(text, "set", "MICROS_APPLICATION_ELF_CHECKER_SOURCES")
    application_expected = (
        "set(MICROS_APPLICATION_ELF_CHECKER_SOURCES "
        "${MICROS_USER_ELF_CHECKER_SOURCES} "
        '"${CMAKE_CURRENT_SOURCE_DIR}/tools/check_application_elf.py" '
        '"${CMAKE_CURRENT_SOURCE_DIR}/tools/application_elf_policy.py")'
    )
    if "".join(application.split()) != "".join(application_expected.split()):
        errors.append("application checking must retain every shared and private owner")
    if text.count(variable) != 11:
        errors.append("checker needs two links, eight fixtures and one complete application reuse")
    if text.count('"${MICROS_USER_LINKER_SCRIPT};' + variable + '"') != 2:
        errors.append("both user link templates need every checker input")
    for output in FIXTURE_OUTPUTS:
        pattern = r'add_custom_command\s*\(\s*OUTPUT\s+"\$\{' + output + r'\}"'
        matches = list(re.finditer(pattern, text))
        if len(matches) != 1:
            errors.append(f"missing or duplicate fixture output: {output}")
            continue
        command = cmake_command(text[matches[0].start():], "add_custom_command", "OUTPUT")
        before, separator, dependencies = command.partition("DEPENDS")
        if not separator or variable in before or dependencies.count(variable) != 1:
            errors.append(f"incomplete checker dependencies: {output}")
    if text.count('"${CMAKE_CURRENT_SOURCE_DIR}/tools/check_user_elf.py"') != 6:
        errors.append("the entry belongs once to the set and five unchanged command templates")
    return errors

def metadata(path):
    return (
        validation_plan.requires_full_tier([path]),
        tuple(validation_plan.affected_workflows([path])),
        tuple(validation_plan.affected_slow_models([path])),
        tuple(validation_plan.affected_image_workflows([path])),
    )

class UserElfSourceLayoutTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.texts = {
            path: current_text(ROOT / path)
            for path in (*PRODUCTION, *HOST) if (ROOT / path).is_file()
        }

    def test_exact_source_families_exist(self):
        self.assertEqual(6, len(PRODUCTION))
        self.assertEqual(6, len(HOST))
        self.assertEqual(set((*PRODUCTION, *HOST)), set(self.texts))


    def test_private_import_graph_is_acyclic(self):
        self.assertEqual([], import_errors(self.texts))

    def test_facade_exports_are_the_actual_owner_objects(self):
        facade = importlib.import_module("tools.check_user_elf")
        for name, destination in expected_owners().items():
            if destination not in PRODUCTION:
                continue
            owner = destination
            module = importlib.import_module(owner[:-3].replace("/", "."))
            self.assertIs(getattr(facade, name), getattr(module, name), name)

    def test_complete_cmake_dependency_inputs(self):
        self.assertEqual([], build_errors(current_text(ROOT / "CMakeLists.txt")))

    def test_exact_four_dimensional_classification(self):
        for path in PRODUCTION:
            self.assertEqual((False, CHECKER_QEMU, (), CHECKER_IMAGES), metadata(path), path)
        for path in HOST:
            self.assertEqual((False, ("test-qemu-user-runtime",), (), ()), metadata(path), path)
        self.assertEqual((False, (), (), ()), metadata(GATE))
        self.assertEqual(1, validation_plan.HOST_UNIT_ONLY_INPUTS.count(GATE))

    def test_missing_image_expansion_fails_despite_preserved_qemu_aliases(self):
        unexpanded = {
            name: tuple(path for path in paths if path not in PRODUCTION[1:])
            for name, paths in validation_plan.IMAGE_INPUTS.items()
        }
        with mock.patch.object(validation_plan, "IMAGE_INPUTS", unexpanded):
            self.assertEqual(CHECKER_QEMU, metadata(FORMAT)[1])
            self.assertEqual((), metadata(FORMAT)[3])
            with self.assertRaises(AssertionError):
                self.test_exact_four_dimensional_classification()

    def test_fixture_bytes_and_metadata_match_immutable_baseline(self):
        module = importlib.import_module("tests.host.test_check_user_elf")
        for name, relocation, undefined, size, section_offset, symbol_offset, digest in FIXTURE_BYTES:
            with self.subTest(name=name, relocation=relocation, undefined=undefined):
                image, result = module.build_elf(
                    data_section_name=name, extra_relocation=relocation,
                    undefined_symbol=undefined,
                )
                self.assertIsInstance(image, bytearray)
                self.assertEqual(size, len(image))
                self.assertEqual(digest, hashlib.sha256(image).hexdigest())
                self.assertEqual(
                    ("program_offset", "section_offset", "symbol_offset",
                     "raw_symbol_index", "text_offset"), tuple(result),
                )
                self.assertEqual(
                    (64, section_offset, symbol_offset, 3, 4096), tuple(result.values()),
                )

    def test_original_cases_keep_one_discovery_identity(self):
        module = importlib.import_module("tests.host.test_check_user_elf")
        case = module.UserElfCheckerTest
        original = {name for path in (PARSE_CASES, STRUCTURE_CASES, POLICY_CASES)
                    for name in FUNCTIONS[path]}
        discovered = unittest.defaultTestLoader.getTestCaseNames(case)
        self.assertEqual(12, len(original))
        self.assertLessEqual(original, set(discovered))
        self.assertEqual(len(discovered), len(set(discovered)))
        self.assertEqual(
            ("UserElfParseCases", "UserElfStructureCases", "UserElfPolicyCases",
             "UserElfCliCases", "TestCase"),
            tuple(base.__name__ for base in case.__bases__),
        )
        for path, name in CASE_CLASSES.items():
            private = importlib.import_module(path[:-3].replace("/", "."))
            self.assertFalse(issubclass(getattr(private, name), unittest.TestCase))
            self.assertFalse(Path(path).name.startswith("test_"))


    def test_production_has_no_additional_module_state_or_types(self):
        owners = expected_owners()
        for path in PRODUCTION:
            for node in parse_source(self.texts.get(path, "")).body:
                names = []
                if isinstance(node, ast.Assign):
                    names = [target.id for target in node.targets
                             if isinstance(target, ast.Name)]
                elif isinstance(node, ast.ClassDef):
                    names = [node.name]
                for name in names:
                    self.assertIn(name, owners)
                    self.assertEqual(path, owners[name])

    def test_fixture_layout_is_only_immutable_scalar_transport(self):

        node = one_definition(self.texts, FIXTURE, "FixtureLayout")
        self.assertIsInstance(node, ast.ClassDef)
        self.assertEqual([], node.bases)
        self.assertEqual([], node.keywords)
        self.assertEqual(1, len(node.decorator_list))
        expected = ast.parse("@dataclass(frozen=True)\nclass Layout:\n    pass\n").body[0]
        self.assertEqual(ast_key(expected.decorator_list), ast_key(node.decorator_list))
        self.assertEqual(13, len(node.body))
        for field, name in zip(node.body, LAYOUT_FIELDS):
            self.assertIsInstance(field, ast.AnnAssign)
            self.assertIsInstance(field.target, ast.Name)
            self.assertEqual(name, field.target.id)
            self.assertIsNone(field.value)
            annotation = "Optional[int]" if name == "relocation_offset" else "int"
            self.assertEqual(ast_key(ast.parse(annotation, mode="eval").body),
                             ast_key(field.annotation))


    def test_duplicate_wrapper_type_and_policy_copies_fail(self):
        for extra in (
            "def parse_elf(data):\n    return None\n",
            "class ElfImage:\n    pass\n",
            "PAGE_SIZE = 2048\n",
        ):
            texts = dict(self.texts)
            texts[FORMAT] = texts.get(FORMAT, "") + "\n" + extra
            self.assertTrue(ownership_errors(texts))

    def test_facade_backedge_and_wildcard_fail(self):
        for extra in (
            "from tools import check_user_elf\n",
            "import tools.check_user_elf as facade\n",
            "from tools import user_elf_policy as policy\n",
            "from . import check_user_elf\n",
            "from tests.host import user_elf_test_fixture\n",
            "from tools.user_elf_parse import *\n",
        ):
            with self.subTest(extra=extra):
                texts = dict(self.texts)
                texts[FORMAT] = texts.get(FORMAT, "") + "\n" + extra
                self.assertTrue(import_errors(texts))

    def test_import_graph_accepts_equivalent_static_spellings(self):
        for extra in (
            "import tools.user_elf_format as binary_format\n",
            "from tools import user_elf_format as binary_format\n",
            "from . import user_elf_format as binary_format\n",
            "from .user_elf_format import ElfImage\n",
        ):
            with self.subTest(extra=extra):
                texts = dict(self.texts)
                texts[PARSE] += "\n" + extra
                self.assertEqual([], import_errors(texts))

    def test_incomplete_transitive_dependency_fails(self):
        cmake = current_text(ROOT / "CMakeLists.txt")
        if "${MICROS_USER_ELF_CHECKER_SOURCES}" in cmake:
            broken = cmake.replace("${MICROS_USER_ELF_CHECKER_SOURCES}", "", 1)
            self.assertTrue(build_errors(broken))
        else:
            self.assertTrue(build_errors(cmake))


if __name__ == "__main__":
    unittest.main()

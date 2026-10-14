import ast
import copy
from functools import lru_cache
import hashlib
import importlib
import json
from pathlib import Path
import re
import unittest
from unittest import mock

from tests.host.test_kernel_address_space_source_layout import cmake_command
from tools import validation_plan


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
SYMBOL_PHASES = (
    "_validate_symbol_boundaries", "_validate_symbol_load_ranges",
    "_validate_symbol_section_ranges",
)
FIXTURE_PHASES = (
    "_fixture_text", "_fixture_symbols", "_fixture_elf_header",
    "_fixture_program_headers", "_fixture_section_headers",
    "_fixture_load_sections", "_fixture_metadata_sections",
)
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
    "test-qemu-vfs", "test-qemu-vm-ready-early", "test-qemu-vm-self-fault",
    "test-qemu-vm-self-fault-sealed", "test-qemu-bootstrap-ready-timeout",
    "test-qemu-bootstrap-manifest-panic",
)
CHECKER_IMAGES = (
    "build-tty-service-image", "build-ramfs-service-image", "build-vfs-service-image",
    "build-application-images",
)

SYMBOL_OWNER_MIGRATION = ()
FUNCTION_MIGRATION = ()

FROZEN_AST = {
    "ELF_CLASS_64": "a736a124de6b9502565aa1f30ea741dfd57af693da130c39b4de302a091f4438",
    "ELF_DATA_LITTLE": "f6a0cbe2d396100373c6cc009aac7633002fd8b2d078351592f6f2f78ece3cc3",
    "ELF_FLAG_RISCV_RVC": "6911ed25d0ba59d76ca05f33d7adf26e03dae5f42477df0aaee9567725e0b091",
    "ELF_HEADER": "097d7aecb4c7e9c740980abbfc06230e55a592174bf6a1c2c13ece510ee0ac92",
    "ELF_MACHINE_RISCV": "9899226d36235b8d7c0283158a088d51ad0cdebc9d08cd011028a80e8cfcb4f9",
    "ELF_TYPE_EXECUTABLE": "89a7d6483f184f76953da797458a69c32cf1aec36af948f0e8e5fe63f6a47536",
    "ELF_VERSION_CURRENT": "f1c98868270c65a3e2a005ce7e3f035c623d3425be119746edff0f45506f1f18",
    "ElfFormatError": "487c5279376597d5eec6d80342aa58a48e5e7b12f7a95edac07a2a9aaf8ee72d",
    "ElfHeader": "5aa3fc0c8b9b15ee20b16a2972d5567dea452ac0795f5f19fc872fc88fdc8464",
    "ElfImage": "dcc98cab4f003dd4d01be98b52db812405afdc5ce202d40ee84e51c6f798e737",
    "ElfSymbol": "65202cfdc6aa84c5414c971d7d849758489c5d25f850456e6c3a6173fdf16386",
    "FORBIDDEN_PROGRAM_TYPES": "f3315b8ef0dabe1ccba06f9c5a593d982b44b66c83797bccc438d11345f24ef3",
    "FORBIDDEN_SECTION_NAMES": "200203c7758d21e6cdd2a8bc44adca280791621f794f2fb24daa1b3435387111",
    "FORBIDDEN_SECTION_PREFIXES": "4ae65d95b6f640e74d52885e8b43e2282cadc461c795d1a8bb54e080bd67e7ad",
    "MINIMUM_STACK_BOTTOM": "adb7331c00c622fc24747db227844b186d4a984b92bc0e480a83b7e66449fd3a",
    "PAGE_SIZE": "9e82c1a9534762874c877e5a1aa4b1bc94fb3c0910ca070ddb36035fe150b259",
    "PROGRAM_DYNAMIC": "d3a81ac37cb55d6925ea1e93d1440e09a02ab5efbdcdcec05533a710e205db3c",
    "PROGRAM_EXECUTE": "0c5015d66a2eb6286e2ad851eb371168b9bbb2f7404357ac7700ae0cbe2f20eb",
    "PROGRAM_HEADER": "c92ae833cd1601fab676e316b3795d4ef831848f221e3796013388de3d182a13",
    "PROGRAM_INTERP": "0933c77f324fc7673c3216b5a86373eced999fbe05131b3e57077488c1a31e8a",
    "PROGRAM_LOAD": "4762e37b4893a5660b2c2654a9e451231303d69958cffd6f8662c715a0da2eb1",
    "PROGRAM_READ": "1149698a522201f69dbc5d66bc0224d2bce8f5943d94b6f439b4c7d308e0b71f",
    "PROGRAM_TLS": "c0f9eabbb08edfce8bd55a863d967a972ccb00dd704acf6d3e909e51ac386257",
    "PROGRAM_WRITE": "16e2429b780f3c8ec11c981547652b52bdde82612f9066908232790b1028a7da",
    "ProgramHeader": "aeb705dfabd72906f1531fa148ed0dda85222960fefa1280e2cba63498376bce",
    "REQUIRED_SYMBOLS": "73b0e71046b87dba5a6a2bdc1ec054288b0ec32db7c620874482f3b8b097b99a",
    "SECTION_ALLOC": "a9489fa35251156d220e462e2c4f0ede27e374645fbcbd18be4af73bc261c44d",
    "SECTION_DYNSYM": "add92d9435b9f70c019f77fa4fa6117afd4545c3fb7a440a98ae486146853d5c",
    "SECTION_EXECUTE": "d48db9c6c9b9e0eb2e098c3ce024dc0d46eaa830c21afbee8b8ad9f8c25e9e2f",
    "SECTION_HEADER": "b7bff9c0130acca7c7353979a037c9221d2be320c657a1ccad15e0ff40370c40",
    "SECTION_NOBITS": "dc9bc74f1ba7547e042cf7ca25c978e1f9db455a7f6cc1f07f74d194fd1ac161",
    "SECTION_NULL": "9ee22dd51c8e41c9e80f4216da31eb1591c3a673d53f63020d26102ebd8fbc5b",
    "SECTION_PROGBITS": "75c4c3001747d308c41b275d1265b47546791e4d7497db555fae46956e12bd79",
    "SECTION_REL": "ef0709b7c1ec9e419958424a4a287b9d94faa84cc1db38a3ed0e7ab636108016",
    "SECTION_RELA": "5196e060bbaf659633bbce131aa78f3a0b965c3604fe1766957a8baba982c385",
    "SECTION_STRTAB": "c73d73f693d6ec06da97bc76f6ae32bb0edc1b67a21cc17447767b3c74967073",
    "SECTION_SYMTAB": "f1bde3d6e7fa56aa9d34a141ccc7178dc32671756406ab412e60d339e4eb641a",
    "SECTION_WRITE": "06c9be2c727b3077d88a883fd904fd4f5ab52169cefa599e35edd2dde6991dd5",
    "SYMBOL": "fbefc12efe69c43e25ee57bee8b1d762946a79bc5161c81fd65a18370853d1c3",
    "SYMBOL_BIND_GLOBAL": "08dac9c5c4ff0824d00df9aa54c4a4e0ff530f940245edf295967f6daec26593",
    "SYMBOL_UNDEFINED": "66d7d55752bb0e17367c651edc1524585802fc9f7d0eb610b9b59a175343c217",
    "Section": "4d7fe73d1a62d688bb2ca6e32e3eff25d1ed48785b66d415a9cd6bc1caa989cc",
    "USER_BASE": "2317eb12d37e266c4bf3d3308c754f05ddcbc6780392281c989d853589ec7399",
    "USER_END": "48f543c9e573033c11eccf17bb07cf177d123d01e6f6c8fb5d391cbc9e83cb10",
    "_bounded_slice": "e245994d26574c1f8ea5bb767c0f319f9cf295bc4a1fe5afc047608981f7e698",
    "_compressed_destination": "fa66b99db492e588bfe21a5b78adfc833c8b8a50e83c44d879caac8f8bb1498a",
    "_cstring": "e8afc52135f5017b54fa11d1fd7216d3934e108e3bd012e7527f382f63ebeb5d",
    "_defined_symbols": "eb8a27a6c99e1334575e4de3fb212a845cacf24470267d6ec8cf3e04efe43fb9",
    "_instruction_destination": "68e7edbe225b9cd4a36eec17ab1767244127f0a33f398ad74b013e8cd6082e95",
    "_instruction_writes": "ae9df8eeb3aefcc00354e094bafcb8d42bd9b3f172a63ef4469e8cb90e8564df",
    "_parse_header": "d63529edbef3993ed194b301643e3403e95c876ae653d77e4a427b33540c5f07",
    "_parse_programs": "018f70a6e231131b593ce3ecc023d0c7e6f03c9ad30077eecd7ad30e870b179e",
    "_parse_raw_sections": "0b681e8e14869b1fa5e2caa93c25ea74288cf05c9b5c0173b20a0263d81be531",
    "_parse_sections": "c6f9db97657487e9432e027e8c523179cc7dc8fb0d645c3dabadca930bac4454",
    "_parse_symbols": "1d7f500a2a38188e533e76b425fa4cb395158bd709bb37555ba0ae860c8effaa",
    "_program_file_bytes": "fb29cd3c162f9c5ad4ef7178c13f9f4b7e10382b532c260242cc2002fd955ca0",
    "_section_by_name": "34177fa27028251e93b9eeb01b31de0da20d2fe8c6f8cf6e1bb6064bd33d6f9b",
    "_sign_extend": "b56502ed7172715e4b09ef55abcbae1e4819f613ec5f220d47c5c830d4ee5674",
    "_validate_header": "7aa9698d96319aa8683f58ae28d2a9bba7dce689f1fd1e5a4df47af1a55cd910",
    "_validate_instructions": "0afdcafc3f494c8014cfba444317b8e6344100fb4a2338a81d1e54024b9648b0",
    "_validate_programs": "ca642fe1bd5030ba5da72611385ce9f69a105d131505b562b197c8b4a2130316",
    "_validate_sections": "89169c3b8804ddd419c223b6b3eb6990a8b9b655acb54d18d47e387d57643bd4",
    "_validate_symbols": "3db52579df79c512d68ffdde82d17537ba7001273b33eec0d03316ad3e28d9e3",
    "add_symbol": "09f82916f2bc3f521ab703eec88bc46b6a33b20f137d4691d30f852a0ab0aee1",
    "align": "f4a5f9c0ada3876f1638377e6175aea17dcfec7ff29e5fbc3ddf6d4df89811f7",
    "build_elf": "1636b5f547b749762ad92da4bad2ef85d80aaf225edbf95caee3ec3dc5c72e82",
    "defined_symbols": "04e7244dc890823899e1c51a012fa298e2f13a66b569536f441e46583bb6cf37",
    "load_elf": "005542782333228016d8bd95063ffa442b8dece179aaf9990a552c531ab6b2d2",
    "load_validated_elf": "222572c58c3119ecae48e0350292e9e8303edb9634d1755855f42d7ebbbb3580",
    "main": "7b0e6b5b350ad6100abe813cb401a25145f76c002d938653fe3b544c2d5c27be",
    "make_string_table": "e83df48e6631854e44ddf737b89bd76f16848693122ae5773ee13061aa1b9870",
    "parse_elf": "1ee30a55aadf2050b5d8fb63c7f7d992437905af37cdd4b02847e996c818a9e4",
    "resident_page_count": "0defd9a5533b24fd6cfb9239cc203e434ef342441235d47750d73b4652a45eec",
    "test_accepts_exact_freestanding_shape": "049e07014065e269471520fa1b8a79197f79337f59d1520786d649a7c7f797a0",
    "test_enforces_complete_resident_page_budget": "37a0903696251a20d30aa9bd4d5aa860fc8eee8ed187a49b0e3473a83fc367a0",
    "test_rejects_bss_or_allocatable_section_mismatch": "3c63e030c60cd60a7d0a358a5870c3715921d7aa1ebf9f5af79285965621b5c7",
    "test_rejects_forbidden_allocatable_runtime_sections": "dd1a72d49403eefc179a033842be6d72d77ed91d30b39681abfb1681ec2163ef",
    "test_rejects_forbidden_program_headers_and_bad_load_shape": "73ba04b2fc8a8e470e1fb0a0e2d3800730a4890d68fd6536e9e450a7e7756153",
    "test_rejects_header_identity_flags_and_entry": "cddfc179af1cb7da36760c14d79265a46ccf66d4d678f38114e7d1a3d0a6fafe",
    "test_rejects_load_overlap_size_and_range": "a8be61e21c7086329f308f7a63dda7922804dab7bb6d48bcd95f90721506263f",
    "test_rejects_raw_stub_startup_return_and_gp_tp_mutations": "cac41289cf64b0e7e133b229444738932186e8b65a73259efe2287951aca904a",
    "test_rejects_relocation_sections": "713f24042cb7ef4ea9faefe2b68d3cbacd7b5ec913a0399e831d4f8be5814389",
    "test_rejects_shortened_text_and_file_backed_bss": "213c7798df54c52cf42c61ce52e93846c46491869ffc86aa4181759d7d380914",
    "test_rejects_truncated_or_malformed_tables": "2fad2d1b81ec399786225ff3995ae5f2fb9262ad29cf87fda8e722b94384f2f7",
    "test_rejects_undefined_symbols": "91877e85c96a37f526ad481452be5f932c6e31b3f1638118bdea1186d7cf7b2f",
    "validate_elf": "9da275897b28d1be31920a60f43aa75c38c8f01d25bf247905b414cffbe8bbb5",
    "validate_resident_page_limit": "f11ebd01ed3b3a86b7277769a47a6165fe968d00362b1c14110f6b1d384f37b3",
    "validation_errors": "dc1e19371d4c606289fa175b3680dc29b2a5807557402776908fc625acedcc14",
}

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


def ast_key(value):
    if isinstance(value, ast.AST):
        if getattr(value, "type_params", ()):
            raise AssertionError("unreviewed nonempty type parameters")
        if type(value).__name__ == "Index":
            return ast_key(value.value)
        return (
            type(value).__name__,
            tuple(
                (name, ast_key(child))
                for name, child in ast.iter_fields(value)
                if name != "type_params"
            ),
        )
    if isinstance(value, (list, tuple)):
        return tuple(ast_key(child) for child in value)
    if isinstance(value, bytes):
        return ("bytes", value.hex())
    if value is Ellipsis:
        return ("ellipsis",)
    return value


@lru_cache(maxsize=64)
def parse_source(text):
    return ast.parse(text)


def ast_digest(node):
    encoded = json.dumps(ast_key(node), separators=(",", ":"), ensure_ascii=True)
    return hashlib.sha256(encoded.encode("ascii")).hexdigest()


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


def resolved_owner(name, destination):
    if destination not in SYMBOL_OWNER_MIGRATION:
        return destination
    return ENTRY if destination.startswith("tools/") else TEST_ENTRY


def helper_body(texts, path, name, parameters, returned=None):
    node = one_definition(texts, path, name)
    if not isinstance(node, ast.FunctionDef):
        raise AssertionError(f"{name}: not a function")
    arguments = node.args
    actual = tuple(argument.arg for argument in arguments.args)
    if (
        actual != parameters or arguments.posonlyargs or arguments.defaults
        or arguments.kwonlyargs or arguments.vararg or arguments.kwarg
        or node.decorator_list
    ):
        raise AssertionError(f"{name}: wrong helper signature")
    returns = [item for item in ast.walk(node) if isinstance(item, ast.Return)]
    body = node.body
    if returned is None:
        if returns:
            raise AssertionError(f"{name}: unexpected control exit")
    else:
        expected = ast.parse(f"return {returned}").body[0]
        if len(returns) != 1 or ast_key(body[-1]) != ast_key(expected):
            raise AssertionError(f"{name}: wrong or nonlocal return")
        body = body[:-1]
    return copy.deepcopy(body)


def require_statement(node, text):
    expected = ast.parse(text).body[0]
    if ast_key(node) != ast_key(expected):
        raise AssertionError(f"wrong phase boundary: {text}")


def expanded_symbols(texts, path, node):
    body = node.body
    if len(body) < 5:
        raise AssertionError("symbol root lost its preflight")
    tail = body[-4:]
    statements = (
        "boundaries = _validate_symbol_boundaries(image, symbols, errors)",
        "_validate_symbol_load_ranges(loads, boundaries, errors)",
        "_validate_symbol_section_ranges(image, boundaries, errors)",
        "return errors, symbols",
    )
    for actual, expected in zip(tail, statements):
        require_statement(actual, expected)
    expanded = body[:-4]
    expanded += helper_body(
        texts, path, SYMBOL_PHASES[0], ("image", "symbols", "errors"), "boundaries",
    )
    expanded += helper_body(
        texts, path, SYMBOL_PHASES[1], ("loads", "boundaries", "errors"),
    )
    expanded += helper_body(
        texts, path, SYMBOL_PHASES[2], ("image", "boundaries", "errors"),
    )
    node.body = expanded + [tail[-1]]
    return node


FIXTURE_CALLS = {
    "_fixture_text": ("text = _fixture_text()", (), "text"),
    "_fixture_symbols": (
        "symbol_strings, symbol_table = _fixture_symbols(base, undefined_symbol)",
        ("base", "undefined_symbol"), "(symbol_strings, symbol_table)",
    ),
    "_fixture_elf_header": (
        "_fixture_elf_header(image, layout)", ("image", "layout"), None,
    ),
    "_fixture_program_headers": (
        "_fixture_program_headers(image, layout, data_bytes)",
        ("image", "layout", "data_bytes"), None,
    ),
    "_fixture_section_headers": (
        "_fixture_section_headers(image, layout, data_section_name, "
        "extra_relocation, data_bytes, symbol_table, symbol_strings, "
        "section_strings, section_name_offsets)",
        ("image", "layout", "data_section_name", "extra_relocation", "data_bytes",
         "symbol_table", "symbol_strings", "section_strings", "section_name_offsets"),
        None,
    ),
    "_fixture_load_sections": (
        "sections = _fixture_load_sections(layout, data_section_name, "
        "data_bytes, section_name_offsets)",
        ("layout", "data_section_name", "data_bytes", "section_name_offsets"),
        "sections",
    ),
    "_fixture_metadata_sections": (
        "_fixture_metadata_sections(sections, layout, extra_relocation, "
        "symbol_table, symbol_strings, section_strings, section_name_offsets)",
        ("sections", "layout", "extra_relocation", "symbol_table", "symbol_strings",
         "section_strings", "section_name_offsets"),
        None,
    ),
}

FIXTURE_OUTLINE_SAMPLE = """
def _fixture_text():
    text = bytearray(b"T")
    return text
def _fixture_symbols(base, undefined_symbol):
    symbol_strings = b"S"
    symbol_table = b"Y"
    return symbol_strings, symbol_table
def _fixture_elf_header(image, layout):
    image.append(layout.base)
def _fixture_program_headers(image, layout, data_bytes):
    image.extend(data_bytes)
def _fixture_load_sections(layout, data_section_name, data_bytes, section_name_offsets):
    sections = [layout.data_offset]
    return sections
def _fixture_metadata_sections(
    sections, layout, extra_relocation, symbol_table, symbol_strings,
    section_strings, section_name_offsets,
):
    if extra_relocation:
        sections.append(layout.relocation_offset)
def _fixture_section_headers(
    image, layout, data_section_name, extra_relocation, data_bytes,
    symbol_table, symbol_strings, section_strings, section_name_offsets,
):
    sections = _fixture_load_sections(layout, data_section_name, data_bytes, section_name_offsets)
    _fixture_metadata_sections(sections, layout, extra_relocation, symbol_table, symbol_strings, section_strings, section_name_offsets)
    for index, section in enumerate(sections):
        image.append(section + layout.section_offset)
def build_elf(*, data_section_name=".data", extra_relocation=False, undefined_symbol=False):
    base = text_offset = rodata_offset = data_offset = relocation_offset = symbol_offset = string_offset = section_string_offset = section_offset = section_count = symbol_section_index = string_section_index = section_string_index = 0
    text = _fixture_text()
    rodata = b"R"
    data_bytes = b"D"
    symbol_strings, symbol_table = _fixture_symbols(base, undefined_symbol)
    section_name_offsets = {}
    section_strings = b"N"
    image = bytearray()
    layout = FixtureLayout(
        base=base, text_offset=text_offset, rodata_offset=rodata_offset,
        data_offset=data_offset, relocation_offset=relocation_offset,
        symbol_offset=symbol_offset, string_offset=string_offset,
        section_string_offset=section_string_offset, section_offset=section_offset,
        section_count=section_count, symbol_section_index=symbol_section_index,
        string_section_index=string_section_index, section_string_index=section_string_index,
    )
    _fixture_elf_header(image, layout)
    _fixture_program_headers(image, layout, data_bytes)
    _fixture_section_headers(image, layout, data_section_name, extra_relocation, data_bytes, symbol_table, symbol_strings, section_strings, section_name_offsets)
    metadata = {}
    return image, metadata
"""
FIXTURE_EXPANDED_SAMPLE = """
def build_elf(*, data_section_name=".data", extra_relocation=False, undefined_symbol=False):
    base = text_offset = rodata_offset = data_offset = relocation_offset = symbol_offset = string_offset = section_string_offset = section_offset = section_count = symbol_section_index = string_section_index = section_string_index = 0
    text = bytearray(b"T")
    rodata = b"R"
    data_bytes = b"D"
    symbol_strings = b"S"
    symbol_table = b"Y"
    section_name_offsets = {}
    section_strings = b"N"
    image = bytearray()
    image.append(base)
    image.extend(data_bytes)
    sections = [data_offset]
    if extra_relocation:
        sections.append(relocation_offset)
    for index, section in enumerate(sections):
        image.append(section + section_offset)
    metadata = {}
    return image, metadata
"""


class LayoutFields(ast.NodeTransformer):
    def visit_Attribute(self, node):
        if isinstance(node.value, ast.Name) and node.value.id == "layout":
            if node.attr not in LAYOUT_FIELDS or not isinstance(node.ctx, ast.Load):
                raise AssertionError("invalid or writable fixture layout field")
            return ast.copy_location(ast.Name(id=node.attr, ctx=ast.Load()), node)
        return self.generic_visit(node)


def expanded_fixture(texts, path, node):
    expected_layout = ast.parse(
        "layout = FixtureLayout("
        + ", ".join(f"{name}={name}" for name in LAYOUT_FIELDS) + ")"
    ).body[0]
    seen = []
    layout_count = 0

    def expand(body, parent):
        nonlocal layout_count
        output = []
        for index, statement in enumerate(body):
            if ast_key(statement) == ast_key(expected_layout):
                if parent != "build_elf" or index + 1 >= len(body):
                    raise AssertionError("fixture layout outside its live root")
                require_statement(body[index + 1], FIXTURE_CALLS["_fixture_elf_header"][0])
                layout_count += 1
                continue
            call = statement.value if isinstance(statement, (ast.Assign, ast.Expr)) else None
            if isinstance(call, ast.Call) and isinstance(call.func, ast.Name):
                name = call.func.id
                if name.startswith("_fixture_"):
                    if name not in FIXTURE_CALLS or name in seen:
                        raise AssertionError("unknown or duplicate fixture phase")
                    child = name in ("_fixture_load_sections", "_fixture_metadata_sections")
                    if child != (parent == "_fixture_section_headers"):
                        raise AssertionError("fixture phase belongs to the wrong caller")
                    source, parameters, returned = FIXTURE_CALLS[name]
                    require_statement(statement, source)
                    seen.append(name)
                    selected = helper_body(texts, path, name, parameters, returned)
                    output.extend(expand(selected, name))
                    continue
            output.append(statement)
        return output

    node.body = expand(node.body, "build_elf")
    if tuple(seen) != FIXTURE_PHASES or layout_count != 1:
        raise AssertionError("incomplete or reordered fixture phases")
    return LayoutFields().visit(node)


def ownership_errors(texts):
    errors = []
    for name, destination in expected_owners().items():
        owner = resolved_owner(name, destination)
        found = [(path, node) for path, text in texts.items()
                 for node in definition_nodes(text, name)]
        if len(found) != 1 or found[0][0] != owner:
            errors.append(f"wrong or duplicate owner: {name}: {[path for path, _ in found]}")
            continue
        node = copy.deepcopy(found[0][1])
        if name not in FUNCTION_MIGRATION:
            if name == "_validate_symbols":
                node = expanded_symbols(texts, owner, node)
            elif name == "build_elf":
                node = expanded_fixture(texts, owner, node)
        if ast_digest(node) != FROZEN_AST[name]:
            errors.append(f"changed original AST: {owner}: {name}")
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
            path: (ROOT / path).read_text()
            for path in (*PRODUCTION, *HOST) if (ROOT / path).is_file()
        }

    def test_exact_source_families_exist(self):
        self.assertEqual(6, len(PRODUCTION))
        self.assertEqual(6, len(HOST))
        self.assertEqual(set((*PRODUCTION, *HOST)), set(self.texts))

    def test_original_inventory_and_bodies(self):
        self.assertEqual(88, len(expected_owners()))
        self.assertEqual(44, sum(map(len, FUNCTIONS.values())))
        self.assertEqual(38, sum(map(len, DATA.values())))
        self.assertEqual(6, sum(map(len, CLASSES.values())))
        self.assertEqual(set(expected_owners()), set(FROZEN_AST))
        self.assertEqual([], ownership_errors(self.texts))

    def test_literal_migrations_are_bounded(self):
        tree = ast.parse(Path(__file__).read_text())
        initial = set(PRODUCTION[1:]) | {FIXTURE, PARSE_CASES, STRUCTURE_CASES, POLICY_CASES}
        for name, permitted in (
            ("SYMBOL_OWNER_MIGRATION", initial),
            ("FUNCTION_MIGRATION", {"_validate_symbols", "build_elf"}),
        ):
            node = next(node for node in tree.body if isinstance(node, ast.Assign)
                        and any(isinstance(target, ast.Name) and target.id == name
                                for target in node.targets))
            self.assertIsInstance(node.value, ast.Tuple)
            self.assertTrue(all(isinstance(item, ast.Constant)
                                and isinstance(item.value, str) for item in node.value.elts))
            values = ast.literal_eval(node.value)
            self.assertEqual(len(values), len(set(values)))
            self.assertLessEqual(set(values), permitted)
            self.assertEqual((), values, "the reviewed source correction is complete")

    def test_private_import_graph_is_acyclic(self):
        self.assertEqual([], import_errors(self.texts))

    def test_facade_exports_are_the_actual_owner_objects(self):
        facade = importlib.import_module("tools.check_user_elf")
        for name, destination in expected_owners().items():
            if destination not in PRODUCTION:
                continue
            owner = resolved_owner(name, destination)
            module = importlib.import_module(owner[:-3].replace("/", "."))
            self.assertIs(getattr(facade, name), getattr(module, name), name)

    def test_complete_cmake_dependency_inputs(self):
        self.assertEqual([], build_errors((ROOT / "CMakeLists.txt").read_text()))

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
        if not SYMBOL_OWNER_MIGRATION:
            self.assertEqual(
                ("UserElfParseCases", "UserElfStructureCases", "UserElfPolicyCases",
                 "UserElfCliCases", "TestCase"),
                tuple(base.__name__ for base in case.__bases__),
            )
            for path, name in CASE_CLASSES.items():
                private = importlib.import_module(path[:-3].replace("/", "."))
                self.assertFalse(issubclass(getattr(private, name), unittest.TestCase))
                self.assertFalse(Path(path).name.startswith("test_"))

    def test_no_unreviewed_long_function_or_empty_final_owner(self):
        for path, text in self.texts.items():
            definitions = [node for node in ast.walk(parse_source(text))
                           if isinstance(node, ast.FunctionDef)]
            if not SYMBOL_OWNER_MIGRATION:
                if path == TEST_ENTRY:
                    self.assertIsInstance(
                        one_definition(self.texts, path, "UserElfCheckerTest"), ast.ClassDef,
                    )
                else:
                    self.assertTrue(definitions, path)
            for node in definitions:
                if node.name not in FUNCTION_MIGRATION:
                    self.assertLessEqual(node.end_lineno - node.lineno + 1, 120,
                                         f"{path}: {node.name}")

    def test_only_named_responsibilities_define_functions(self):
        for path, text in self.texts.items():
            permitted = {
                name for name, destination in expected_owners().items()
                if resolved_owner(name, destination) == path
            }
            if path == POLICY and "_validate_symbols" not in FUNCTION_MIGRATION:
                permitted.update(SYMBOL_PHASES)
            if path == FIXTURE and "build_elf" not in FUNCTION_MIGRATION:
                permitted.update(FIXTURE_PHASES)
            for node in ast.walk(parse_source(text)):
                if isinstance(node, ast.FunctionDef):
                    cli = path == CLI_CASES and node.name.startswith(("test_", "_cli_"))
                    self.assertTrue(node.name in permitted or cli, f"{path}: {node.name}")

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
                    self.assertEqual(path, resolved_owner(name, owners[name]))

    def test_fixture_layout_is_only_immutable_scalar_transport(self):
        if "build_elf" in FUNCTION_MIGRATION:
            self.assertNotIn("FixtureLayout", self.texts.get(FIXTURE, ""))
            return
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

    def test_version_normalization_retains_semantic_fields(self):
        first = ast.parse("value = source[1:3]")
        second = ast.parse("\n\nvalue = source[1:3]")
        self.assertEqual(ast_digest(first), ast_digest(second))
        self.assertNotEqual(ast_digest(first), ast_digest(ast.parse("value = source[1:4]")))
        node = ast.parse("def action():\n    return 1\n").body[0]
        node.type_params = [ast.Name(id="T", ctx=ast.Load())]
        with self.assertRaisesRegex(AssertionError, "type parameters"):
            ast_digest(node)

    def test_fixture_transport_does_not_fold_members_or_allow_writes(self):
        original = ast.parse("value = layout.base + other.layout.base")
        expected = ast.parse("value = base + other.layout.base")
        self.assertEqual(ast_key(expected), ast_key(LayoutFields().visit(original)))
        for source in ("layout.base = 1", "value = layout.unknown"):
            with self.assertRaisesRegex(AssertionError, "fixture layout field"):
                LayoutFields().visit(ast.parse(source))

    def test_symbol_phase_expansion_preserves_guard_transport_and_order(self):
        source = """
def _validate_symbol_boundaries(image, symbols, errors):
    boundaries = {"start": image}
    errors.append("boundaries")
    return boundaries
def _validate_symbol_load_ranges(loads, boundaries, errors):
    errors.append("loads")
def _validate_symbol_section_ranges(image, boundaries, errors):
    errors.append("sections")
def _validate_symbols(image, loads):
    errors = []
    symbols = {}
    if image is None:
        return errors, symbols
    boundaries = _validate_symbol_boundaries(image, symbols, errors)
    _validate_symbol_load_ranges(loads, boundaries, errors)
    _validate_symbol_section_ranges(image, boundaries, errors)
    return errors, symbols
"""
        original = """
def _validate_symbols(image, loads):
    errors = []
    symbols = {}
    if image is None:
        return errors, symbols
    boundaries = {"start": image}
    errors.append("boundaries")
    errors.append("loads")
    errors.append("sections")
    return errors, symbols
"""
        texts = {"sample": source}
        node = one_definition(texts, "sample", "_validate_symbols")
        result = expanded_symbols(texts, "sample", node)
        self.assertEqual(
            ast_key(ast.parse(original).body[0]), ast_key(result),
        )
        mutations = (
            (
                "\n    _validate_symbol_load_ranges(loads, boundaries, errors)\n",
                "\n    _validate_symbol_load_ranges(loads, boundaries, errors.copy())\n",
            ),
            (
                "\n    return boundaries\n",
                "\n    if image:\n        return {}\n    return boundaries\n",
            ),
            (
                "\n    return boundaries\n",
                "\n    return dict(boundaries)\n",
            ),
            (
                "\n    boundaries = _validate_symbol_boundaries(image, symbols, errors)\n",
                "\n    boundaries = _validate_symbol_boundaries(image, errors, symbols)\n",
            ),
        )
        for old, new in mutations:
            with self.subTest(mutation=new):
                self.assertEqual(1, source.count(old))
                texts = {"sample": source.replace(old, new)}
                with self.assertRaises(AssertionError):
                    expanded_symbols(
                        texts, "sample", one_definition(texts, "sample", "_validate_symbols"),
                    )

    def test_fixture_phase_expansion_has_exact_borrows_and_live_layout(self):
        texts = {"sample": FIXTURE_OUTLINE_SAMPLE}
        root = one_definition(texts, "sample", "build_elf")
        expected = ast.parse(FIXTURE_EXPANDED_SAMPLE).body[0]
        self.assertEqual(
            ast_key(expected), ast_key(expanded_fixture(texts, "sample", root)),
        )
        mutations = (
            ("base=base", "base=data_offset"),
            ("image.append(layout.base)", "return image"),
            ("image.append(layout.base)", "layout.base = 1"),
            (
                "\n    _fixture_program_headers(image, layout, data_bytes)\n",
                "\n    _fixture_program_headers(image, layout, symbol_table)\n",
            ),
            ("return symbol_strings, symbol_table", "return symbol_table, symbol_strings"),
        )
        for old, new in mutations:
            with self.subTest(mutation=new):
                self.assertEqual(1, FIXTURE_OUTLINE_SAMPLE.count(old))
                changed = {"sample": FIXTURE_OUTLINE_SAMPLE.replace(old, new)}
                with self.assertRaises(AssertionError):
                    expanded_fixture(
                        changed, "sample", one_definition(changed, "sample", "build_elf"),
                    )
        root = one_definition(texts, "sample", "build_elf")
        index = next(
            index for index, node in enumerate(root.body)
            if isinstance(node, ast.Assign)
            and any(isinstance(target, ast.Name) and target.id == "layout"
                    for target in node.targets)
        )
        root.body[index], root.body[index + 1] = root.body[index + 1], root.body[index]
        with self.assertRaises(AssertionError):
            expanded_fixture(texts, "sample", root)
        changed = {
            "sample": FIXTURE_OUTLINE_SAMPLE.replace(
                "layout.data_offset", "other.layout.data_offset",
            ),
        }
        folded = expanded_fixture(
            changed, "sample", one_definition(changed, "sample", "build_elf"),
        )
        self.assertNotEqual(ast_key(expected), ast_key(folded))

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
        cmake = (ROOT / "CMakeLists.txt").read_text()
        if "${MICROS_USER_ELF_CHECKER_SOURCES}" in cmake:
            broken = cmake.replace("${MICROS_USER_ELF_CHECKER_SOURCES}", "", 1)
            self.assertTrue(build_errors(broken))
        else:
            self.assertTrue(build_errors(cmake))

    def test_helper_signature_and_nonlocal_return_fail(self):
        texts = {"sample": "def phase(image, errors):\n    errors.append(image)\n"}
        self.assertEqual(1, len(helper_body(texts, "sample", "phase", ("image", "errors"))))
        with self.assertRaisesRegex(AssertionError, "signature"):
            helper_body(texts, "sample", "phase", ("errors", "image"))
        texts["sample"] = "def phase(image, errors):\n    return errors\n"
        with self.assertRaisesRegex(AssertionError, "control exit"):
            helper_body(texts, "sample", "phase", ("image", "errors"))


if __name__ == "__main__":
    unittest.main()

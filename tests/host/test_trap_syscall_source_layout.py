"""Current trap/syscall boundaries and runtime fixture lifetime/ABI contracts."""

from collections import Counter
from pathlib import Path
import re
import unittest

from tests.host.build_source_records import cmake_command, cmake_commands, cmake_if_blocks
from tests.host.c_source_records import (
    c_declaration_text, c_function_body, c_function_definitions, c_lexical_text,
    file_scope_objects, function_records, tokens,
)
from tests.host.source_snapshot import source_bytes
from tests.host import test_vm_service_source_layout as vm_layout
from tests.host.test_kernel_bootstrap_source_layout import compact, compact_hash, has_function_reference
from tests.host.test_kernel_pm_control_source_layout import macro_definition_text
from tests.host.test_pm_service_source_layout import static_local_storage
from tools import validation_plan


# Fixed OBJ02 #246 static-local data; no historical function bodies.
CURRENT_STATIC_LOCAL_OBJECTS = {'kernel/tests/runtime/user_runtime_test.c': ('static const struct micros_privilege_profile profiles [ ] = { '
                                              '{ . id = 1 , . name = , . operations = '
                                              'MICROS_PRIVILEGE_OPERATION_RECEIVE | '
                                              'MICROS_PRIVILEGE_OPERATION_SEND | '
                                              'MICROS_PRIVILEGE_OPERATION_CALL , . call_targets = UINT32_C ( '
                                              '1 ) << 2 , . send_targets = UINT32_C ( 1 ) << 2 , } , { . id '
                                              '= 2 , . name = , . operations = '
                                              'MICROS_PRIVILEGE_OPERATION_RECEIVE | '
                                              'MICROS_PRIVILEGE_OPERATION_SEND | '
                                              'MICROS_PRIVILEGE_OPERATION_REPLY | '
                                              'MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE , . send_targets = ( '
                                              'UINT32_C ( 1 ) << 1 ) | ( UINT32_C ( 1 ) << 3 ) , } , { . id '
                                              '= 3 , . name = , . operations = '
                                              'MICROS_PRIVILEGE_OPERATION_RECEIVE | '
                                              'MICROS_PRIVILEGE_OPERATION_NOTIFY , . notify_targets = '
                                              'UINT32_C ( 1 ) << 2 , } , } ;',),
 'kernel/tests/runtime/user_runtime_test_cleanup.c': ('static const uint64_t role_status [ '
                                                      'USER_RUNTIME_PROCESS_COUNT ] = { '
                                                      'MICROS_USER_RUNTIME_TEST_STATUS_STARTUP | '
                                                      'MICROS_USER_RUNTIME_TEST_STATUS_STACK | '
                                                      'MICROS_USER_RUNTIME_TEST_STATUS_MEMORY | '
                                                      'MICROS_USER_RUNTIME_TEST_STATUS_REGISTERS | '
                                                      'MICROS_USER_RUNTIME_TEST_STATUS_IPC | '
                                                      'MICROS_USER_RUNTIME_TEST_STATUS_GRANTS | '
                                                      'MICROS_USER_RUNTIME_TEST_STATUS_COMPLETE , '
                                                      'MICROS_USER_RUNTIME_TEST_STATUS_STARTUP | '
                                                      'MICROS_USER_RUNTIME_TEST_STATUS_STACK | '
                                                      'MICROS_USER_RUNTIME_TEST_STATUS_MEMORY | '
                                                      'MICROS_USER_RUNTIME_TEST_STATUS_IPC | '
                                                      'MICROS_USER_RUNTIME_TEST_STATUS_GRANTS | '
                                                      'MICROS_USER_RUNTIME_TEST_STATUS_COMPLETE , '
                                                      'MICROS_USER_RUNTIME_TEST_STATUS_STARTUP | '
                                                      'MICROS_USER_RUNTIME_TEST_STATUS_STACK | '
                                                      'MICROS_USER_RUNTIME_TEST_STATUS_MEMORY | '
                                                      'MICROS_USER_RUNTIME_TEST_STATUS_IPC | '
                                                      'MICROS_USER_RUNTIME_TEST_STATUS_COMPLETE , } ;',),
 'tests/qemu/user_runtime_client.c': ('static const uint8_t read_expected [ TEST_TRANSFER_SIZE ] = { 0x10 , '
                                      '0x11 , 0x12 , 0x13 , 0x14 , 0x15 , 0x16 , 0x17 , 0x18 , 0x19 , 0x1a , '
                                      '0x1b , 0x1c , 0x1d , 0x1e , 0x1f , 0x20 , 0x21 , 0x22 , 0x23 , 0x24 , '
                                      '0x25 , 0x26 , 0x27 , 0x28 , 0x29 , 0x2a , 0x2b , 0x2c , 0x2d , 0x2e , '
                                      '0x2f , } ;',),
 'tests/qemu/user_runtime_responders.c': ('static const uint8_t read_expected [ TEST_TRANSFER_SIZE ] = { '
                                          '0x10 , 0x11 , 0x12 , 0x13 , 0x14 , 0x15 , 0x16 , 0x17 , 0x18 , '
                                          '0x19 , 0x1a , 0x1b , 0x1c , 0x1d , 0x1e , 0x1f , 0x20 , 0x21 , '
                                          '0x22 , 0x23 , 0x24 , 0x25 , 0x26 , 0x27 , 0x28 , 0x29 , 0x2a , '
                                          '0x2b , 0x2c , 0x2d , 0x2e , 0x2f , } ;',)}


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]

STRUCTURAL_TEST_PATH = "tests/host/test_trap_syscall_source_layout.py"

SOURCE_SETS = {
    "MICROS_TRAP_SOURCES": (
        'kernel/trap/trap.c',
        'kernel/trap/trap_context.c',
        'kernel/trap/trap_user.c',
        'kernel/trap/trap_interrupt.c',
        'kernel/trap/trap_syscall.c',
        'kernel/trap/trap_fault.c',
        'kernel/tests/trap/trap_test.c',
        'kernel/trap/trap_test_hooks.c',
    ),
    "MICROS_SYSCALL_SOURCES": (
        'kernel/trap/syscall.c',
        'kernel/trap/syscall_decode.c',
        'kernel/trap/syscall_context.c',
    ),
    "MICROS_USER_RUNTIME_KERNEL_TEST_SOURCES": (
        'kernel/tests/runtime/user_runtime_test.c',
        'kernel/tests/runtime/user_runtime_test_state.c',
        'kernel/tests/runtime/user_runtime_test_image.c',
        'kernel/tests/runtime/user_runtime_test_setup.c',
        'kernel/tests/runtime/user_runtime_test_trap.c',
        'kernel/tests/runtime/user_runtime_test_cleanup.c',
        'kernel/tests/runtime/user_runtime_test_report.c',
    ),
    "MICROS_USER_RUNTIME_SERVICE_TEST_SOURCES": (
        "tests/qemu/user_runtime_service.c",
        "tests/qemu/user_runtime_service_common.c",
        "tests/qemu/user_runtime_client.c",
        "tests/qemu/user_runtime_responders.c",
    ),
    "MICROS_RUNTIME_HOST_TEST_SOURCES": (
        "tests/host/runtime_test.c",
        "tests/host/runtime_test_fixture.c",
        "tests/host/runtime_wrapper_test.c",
    ),
}

FAMILY_HEADERS = {
    "MICROS_TRAP_SOURCES": 'kernel/trap/trap_internal.h',
    "MICROS_SYSCALL_SOURCES": 'kernel/trap/syscall_internal.h',
    "MICROS_USER_RUNTIME_KERNEL_TEST_SOURCES": 'kernel/tests/runtime/user_runtime_test_internal.h',
    "MICROS_USER_RUNTIME_SERVICE_TEST_SOURCES": "tests/qemu/user_runtime_service_internal.h",
    "MICROS_RUNTIME_HOST_TEST_SOURCES": "tests/host/runtime_test_internal.h",
}

PRIVATE_HEADERS = tuple(FAMILY_HEADERS.values())

PUBLIC_PATHS = ('include/micros/trap.h',
 'include/micros/runtime.h',
 'include/micros/syscall_abi.h',
 'kernel/trap/syscall.h',
 'kernel/trap/trap_route_core.h',
 'kernel/tests/runtime/user_runtime_test.h',
 'kernel/tests/runtime/user_runtime_image.h',
 'tests/qemu/user_runtime_protocol.h',
 'lib/runtime/raw_syscall.h',
 'lib/runtime/memory.h',
 'arch/riscv64/trap_context.h',
 'kernel/trap/trap_route_core.c',
 'lib/runtime/runtime.c',
 'lib/runtime/memory.c',
 'lib/runtime/memory_symbols.c',
 'arch/riscv64/trap.S',
 'arch/riscv64/trap_test.S',
 'arch/riscv64/nested_trap_test.S',
 'arch/riscv64/user_runtime_test.S',
 'tests/qemu/user_runtime_probe.S',
 'lib/runtime/start.S',
 'lib/runtime/raw_syscall.S',
 'lib/runtime/user.ld')

EXISTING_INLINE_HEADERS = (
    "arch/riscv64/interrupt.h", 'kernel/boot/bootstrap_control_internal.h',
)

TRAP_FIELDS = (
    "volatile enum trap_test_state trap_test_state;",
    "volatile uint32_t trap_test_count;",
    "uint64_t trap_test_entry_sstatus;",
)

KERNEL_FIELDS = (
    "struct micros_process_handle processes[USER_RUNTIME_PROCESS_COUNT];",
    "struct micros_thread_handle threads[USER_RUNTIME_PROCESS_COUNT];",
    "micros_endpoint_t endpoints[USER_RUNTIME_PROCESS_COUNT];",
    "struct process_image images[USER_RUNTIME_PROCESS_COUNT];",
    "unsigned char writable_snapshots[USER_RUNTIME_PROCESS_COUNT]"
    "[USER_RUNTIME_MAX_IMAGE_PAGES][MICROS_SV39_PAGE_SIZE];",
    "bool writable_snapshot_valid[USER_RUNTIME_PROCESS_COUNT][USER_RUNTIME_MAX_IMAGE_PAGES];",
    "struct micros_endpoint_registry *endpoint_registry;",
    "struct micros_grant_registry *grant_registry;",
    "uint64_t baseline_owned;",
    "uint64_t baseline_free;",
    "size_t baseline_processes;",
    "size_t baseline_threads;",
    "size_t baseline_harts;",
    "bool rodata_fault_seen;",
    "bool loader_validated;",
    "bool final_client_send;",
    "size_t returned_actor;",
)

RAW_FIELDS = (
    "uint64_t arguments[8];",
    "int64_t result;",
    "size_t calls;",
    "int write_argument;",
    "const struct micros_ipc_message *expected_input;",
    "struct micros_ipc_message input_snapshot;",
    "struct micros_ipc_message output;",
    "bool input_observed;",
)

SAVED_REGISTER_OBJECTS = tuple(
    f"uintptr_t micros_user_runtime_test_saved_{register};"
    for register in ("sp", "gp", "tp")
)

IMAGE_OBJECTS = (
    "volatile struct micros_user_runtime_test_config micros_user_runtime_test_config = "
    "{ .version = MICROS_USER_RUNTIME_TEST_VERSION, };",
    "const volatile uint64_t micros_user_runtime_test_rodata = "
    "MICROS_USER_RUNTIME_TEST_RODATA_SENTINEL;",
    "volatile uint64_t micros_user_runtime_test_data_sentinel = "
    "MICROS_USER_RUNTIME_TEST_DATA_SENTINEL;",
    "volatile struct test_region micros_user_runtime_test_read_region = {"
    ".before = MICROS_USER_RUNTIME_TEST_CANARY_BEFORE, .bytes = {"
    "0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,"
    "0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,"
    "0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,"
    "0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,"
    "}, .after = MICROS_USER_RUNTIME_TEST_CANARY_AFTER, };",
    "volatile struct test_region micros_user_runtime_test_write_region;",
    "volatile struct test_region micros_user_runtime_test_server_region;",
    "volatile uint8_t micros_user_runtime_test_bss_probe[TEST_BSS_PROBE_SIZE];",
)

RECORD_FIELDS = {
    "mapped_page": (
        "uint64_t virtual_address;", "uint64_t physical_address;",
        "uint32_t permissions;", "size_t segment_index;",
    ),
    "process_image": (
        "size_t page_count;", "struct mapped_page pages[USER_RUNTIME_MAX_IMAGE_PAGES];",
        "uint64_t stack_physical;",
    ),
    "test_region": (
        "uint64_t before;", "uint8_t bytes[TEST_TRANSFER_SIZE];", "uint64_t after;",
    ),
    "raw_capture": RAW_FIELDS,
    "micros_vm_handoff_decoded_request": (
        "struct micros_vm_handoff_request ready;",
        "struct micros_vm_tty_mapping_request tty_mapping;",
        "struct micros_vm_load_begin_request load_begin;",
        "struct micros_vm_scratch_map_request scratch_map;",
        "struct micros_vm_scratch_commit_request scratch_commit;",
        "struct micros_vm_load_freeze_request load_freeze;",
        "struct micros_vm_load_abort_request load_abort;",
        "struct micros_vm_load_commit_request load_commit;",
        "struct micros_vm_exit_release_request exit_release;",
    ),
}

NATIVE_CASE_ORDER = (
    "test_ipc_wrappers", "test_grant_wrappers",
    "test_bootstrap_launcher_console_begin", "test_memory_support",
)

RUNTIME_MARKER = (
    "MICROS_USER_RUNTIME_TEST_PASS elf=freestanding startup=validated syscalls=1-10 "
    "registers=preserved stack=external data=initialized bss=zero rodata=protected "
    "return=trapped cleanup=complete\n"
)

def read_source(relative: str) -> str:
    return source_bytes(REPOSITORY_ROOT, REPOSITORY_ROOT / relative).decode()

def all_sources() -> tuple[str, ...]:
    return tuple(path for paths in SOURCE_SETS.values() for path in paths)

def family_for(path: str) -> str:
    for family, paths in SOURCE_SETS.items():
        if path in paths or path == FAMILY_HEADERS[family]:
            return family
    raise AssertionError(f"not an accepted source/private owner: {path}")

def family_paths(path: str) -> tuple[str, ...]:
    return SOURCE_SETS[family_for(path)]

def local_include_closure(relative: str) -> tuple[str, ...]:
    pending = [relative]
    observed = set()
    while pending:
        path = pending.pop()
        if path in observed:
            continue
        observed.add(path)
        text = read_source(path)
        c_lexical_text(text)
        if re.search(r"(?m)^\s*#\s*include\s+(?![<\"\s])", text):
            raise AssertionError(f"computed local include: {path}")
        includes = re.findall(r'(?m)^\s*#\s*include\s*([<"])([^">]+)[">]', text)
        for delimiter, include in includes:
            if Path(include).suffix in {".c", ".s", ".S", ".py"}:
                raise AssertionError(f"textual source include: {path}:{include}")
            resolved = vm_layout.resolve_local_include(REPOSITORY_ROOT / path, include)
            if resolved is None:
                if delimiter == '"':
                    raise AssertionError(f"unresolved local include: {path}:{include}")
                continue
            if REPOSITORY_ROOT not in resolved.parents:
                raise AssertionError(f"external local include: {path}:{include}")
            pending.append(str(resolved.relative_to(REPOSITORY_ROOT)))
        if re.search(r"\b(?:static|extern)\b", c_lexical_text(macro_definition_text(text))):
            raise AssertionError(f"macro-hidden storage: {path}")
    return tuple(sorted(observed))

def header_executable_text(relative: str) -> str:
    selected = []
    for dependency in local_include_closure(relative):
        if dependency == relative:
            continue
        text = read_source(dependency)
        selected.append(macro_definition_text(text))
        selected.extend(c_function_body(text, name) for name in c_function_definitions(text))
    return "\n".join(selected)

def check_header(relative: str, text: str, private: bool) -> None:
    definitions = c_function_definitions(text)
    if file_scope_objects(text):
        raise AssertionError(f"header object definitions: {relative}")
    if definitions:
        if private or relative not in EXISTING_INLINE_HEADERS:
            raise AssertionError(f"new header algorithm: {relative}")

    if not private:
        return
    if re.search(r"\b(?:static|inline|_Thread_local)\b", c_declaration_text(text)):
        raise AssertionError(f"private storage/inline declaration: {relative}")
    macros = re.sub(r"\\\n\s*", " ", macro_definition_text(text))
    for macro in re.finditer(
        r"(?m)^\s*#\s*define\s+([A-Za-z_][A-Za-z0-9_]*)([^\n]*)", macros,
    ):
        name, body = macro.groups()
        if name == "EXPECT_TRUE" and relative == FAMILY_HEADERS["MICROS_RUNTIME_HOST_TEST_SOURCES"]:
            if compact_hash(macro.group()) != EXPECT_TRUE_HASH:
                raise AssertionError("the existing native assertion macro changed")
            continue
        if body.startswith("(") or re.search(r"[;{}=]|\b(?:do|if|return|while)\b", body):
            raise AssertionError(f"private executable macro: {relative}:{name}")
        identifiers = re.findall(r"\b[A-Za-z_][A-Za-z0-9_]*\b", c_lexical_text(body))
        if any(
            not token.isupper() and token not in {
                "sizeof", "_Alignof", "uintptr_t", "size_t", "uint8_t", "uint16_t",
                "uint32_t", "uint64_t", "int8_t", "int16_t", "int32_t", "int64_t",
                "unsigned", "signed", "int", "long", "short", "char", "bool", "true", "false",
            }
            for token in identifiers
        ):
            raise AssertionError(f"private macro hides a callable dependency: {relative}:{name}")

def record_fields(text: str, name: str) -> tuple[str, ...]:
    matches = re.findall(
        rf"\b(?:struct|union)\s+{re.escape(name)}\s*\{{([^{{}}]*)\}}",
        c_declaration_text(text),
    )
    if len(matches) != 1:
        raise AssertionError(f"expected one exact record type: {name}")
    return tuple(compact(field + ";") for field in matches[0].split(";") if field.strip())

PERMITTED_OWNER_DEPENDENCIES = {
    'kernel/trap/trap.c': set(SOURCE_SETS["MICROS_TRAP_SOURCES"]),
    'kernel/trap/trap_context.c': set(),
    'kernel/trap/trap_user.c': {
        'kernel/trap/trap_context.c', 'kernel/trap/trap_interrupt.c', 'kernel/trap/trap_syscall.c',
        'kernel/trap/trap_fault.c', 'kernel/trap/trap_test_hooks.c',
    },
    'kernel/trap/trap_interrupt.c': {
        'kernel/trap/trap_context.c', 'kernel/trap/trap_fault.c', 'kernel/trap/trap_test_hooks.c',
    },
    'kernel/trap/trap_syscall.c': {
        'kernel/trap/trap_context.c', 'kernel/trap/trap_fault.c', 'kernel/trap/trap_test_hooks.c',
    },
    'kernel/trap/trap_fault.c': {'kernel/trap/trap_context.c'},
    'kernel/tests/trap/trap_test.c': {'kernel/trap/trap_context.c'},
    'kernel/trap/trap_test_hooks.c': {'kernel/trap/trap_context.c', 'kernel/trap/trap_fault.c'},
    'kernel/trap/syscall.c': {'kernel/trap/syscall_decode.c', 'kernel/trap/syscall_context.c'},
    'kernel/trap/syscall_decode.c': set(),
    'kernel/trap/syscall_context.c': set(),
    'kernel/tests/runtime/user_runtime_test.c': {
        'kernel/tests/runtime/user_runtime_test_state.c', 'kernel/tests/runtime/user_runtime_test_image.c',
        'kernel/tests/runtime/user_runtime_test_setup.c', 'kernel/tests/runtime/user_runtime_test_report.c',
    },
    'kernel/tests/runtime/user_runtime_test_state.c': set(),
    'kernel/tests/runtime/user_runtime_test_image.c': {'kernel/tests/runtime/user_runtime_test_state.c'},
    'kernel/tests/runtime/user_runtime_test_setup.c': {
        'kernel/tests/runtime/user_runtime_test_state.c', 'kernel/tests/runtime/user_runtime_test_image.c',
    },
    'kernel/tests/runtime/user_runtime_test_trap.c': {
        'kernel/tests/runtime/user_runtime_test_state.c', 'kernel/tests/runtime/user_runtime_test_image.c',
    },
    'kernel/tests/runtime/user_runtime_test_cleanup.c': {
        'kernel/tests/runtime/user_runtime_test_state.c', 'kernel/tests/runtime/user_runtime_test_image.c',
        'kernel/tests/runtime/user_runtime_test_report.c',
    },
    'kernel/tests/runtime/user_runtime_test_report.c': {
        'kernel/tests/runtime/user_runtime_test_state.c', 'kernel/tests/runtime/user_runtime_test_image.c',
    },
    "tests/qemu/user_runtime_service.c": {
        "tests/qemu/user_runtime_service_common.c", "tests/qemu/user_runtime_client.c",
        "tests/qemu/user_runtime_responders.c",
    },
    "tests/qemu/user_runtime_service_common.c": set(),
    "tests/qemu/user_runtime_client.c": {"tests/qemu/user_runtime_service_common.c"},
    "tests/qemu/user_runtime_responders.c": {"tests/qemu/user_runtime_service_common.c"},
    "tests/host/runtime_test.c": {"tests/host/runtime_wrapper_test.c"},
    "tests/host/runtime_test_fixture.c": set(),
    "tests/host/runtime_wrapper_test.c": {"tests/host/runtime_test_fixture.c"},
}

def check_dependency_text(texts, path, name, executable):
    allowed = PERMITTED_OWNER_DEPENDENCIES[path] | {path}
    references = c_lexical_text(executable)
    words = set(re.findall(r"\b[A-Za-z_]\w*\b", references))
    local = set(function_records(texts[path]))
    for source in family_paths(path):
        for called in set(function_records(texts[source])) & words:
            if source != path and called in local:
                continue
            if source not in allowed and has_function_reference(references, called):
                raise AssertionError(f"upward/sibling helper dependency: {path}:{name} -> {source}:{called}")

def check_storage(texts):
    inventory = {path: list(file_scope_objects(text)) for path, text in texts.items()}

    def consume(path, declaration):
        matches = [item for item in inventory.get(path, []) if compact(item) == compact(declaration)]
        if len(matches) != 1:
            raise AssertionError(f"expected one exact storage declaration: {path}:{declaration}")
        inventory[path].remove(matches[0])

    def context(owner, fields, initializer=""):
        candidates = [
            declaration for declaration in inventory.get(owner, [])
            if re.fullmatch(
                r"static\s+struct\s+[A-Za-z_][A-Za-z0-9_]*\s+"
                r"[A-Za-z_][A-Za-z0-9_]*\s*" + initializer + r"\s*;",
                declaration, re.DOTALL,
            )
        ]
        if len(candidates) != 1:
            raise AssertionError(f"expected one scalar file-local context: {owner}")
        declaration = candidates[0]
        kind = re.search(r"struct\s+([A-Za-z_][A-Za-z0-9_]*)", declaration).group(1)
        family_text = "\n".join(texts[path] for path in family_paths(owner))
        family_text += "\n" + read_source(FAMILY_HEADERS[family_for(owner)])
        if Counter(record_fields(family_text, kind)) != Counter(map(compact, fields)):
            raise AssertionError(f"changed context field types/count/bounds: {owner}")
        consume(owner, declaration)

    context('kernel/tests/trap/trap_test.c', TRAP_FIELDS)
    context(
        'kernel/tests/runtime/user_runtime_test_state.c', KERNEL_FIELDS,
        r"=\s*\{\s*\.returned_actor\s*=\s*USER_RUNTIME_PROCESS_COUNT\s*,?\s*\}",
    )
    for declaration in SAVED_REGISTER_OBJECTS:
        consume('kernel/tests/runtime/user_runtime_test.c', declaration)
    consume('kernel/tests/runtime/user_runtime_test.c', "static const uint64_t USER_RUNTIME_TIMER_INTERVAL = UINT64_C(0x000000003b9aca00);")
    for declaration in IMAGE_OBJECTS:
        consume("tests/qemu/user_runtime_service_common.c", declaration)
    consume("tests/host/runtime_test_fixture.c", "static struct raw_capture capture;")
    remaining = {path: declarations for path, declarations in inventory.items() if declarations}
    if remaining:
        raise AssertionError(f"additional/duplicated file-scope state: {remaining}")

EXPECT_TRUE_HASH = "3ef0a1204f1dfeb937f4dc99626fcbda4e9a8e1c387b703679c34a798e91c31c"

class TrapSyscallSourceLayoutTest(unittest.TestCase):
    def required_text(self, path: str) -> str:
        self.assertTrue((REPOSITORY_ROOT / path).is_file(), f"missing required owner: {path}")
        return read_source(path)

    def test_all_current_consumers_expand_the_complete_sets_once(self) -> None:
        cmake = read_source("CMakeLists.txt")
        for target, names in (
            ("micros", ("MICROS_TRAP_SOURCES", "MICROS_SYSCALL_SOURCES")),
            ("micros_user_runtime_service", ("MICROS_USER_RUNTIME_SERVICE_TEST_SOURCES",)),
            ("runtime_test", ("MICROS_RUNTIME_HOST_TEST_SOURCES",)),
        ):
            with self.subTest(target=target):
                blocks = cmake_commands(cmake, "add_executable", target)
                self.assertEqual(1, len(blocks))
                block = blocks[0]
                self.assertEqual(
                    {name: int(name in names) for name in SOURCE_SETS},
                    {name: block.count("${" + name + "}") for name in SOURCE_SETS},
                )
                self.assertFalse(set(all_sources()) & set(re.findall(r"[\w/]+\.c", block)))
        branches = cmake_if_blocks(cmake, "MICROS_BUILD_USER_RUNTIME_TEST")
        evidence = [branch for branch in branches if re.search(r"\btarget_sources\s*\(\s*micros\b", branch)]
        self.assertEqual(1, len(evidence))
        block = cmake_command(evidence[0], "target_sources", "micros")
        self.assertEqual(
            compact('target_sources(micros PRIVATE "${MICROS_USER_RUNTIME_FIXTURE_SOURCE}" '
                    'arch/riscv64/user_runtime_test.S ${MICROS_USER_RUNTIME_KERNEL_TEST_SOURCES})'),
            compact(block),
        )

    def test_new_private_headers_are_declarations_only(self) -> None:
        for path in PRIVATE_HEADERS:
            with self.subTest(header=path):
                check_header(path, self.required_text(path), private=True)

    def test_recursive_includes_cannot_smuggle_algorithms_storage_or_source(self) -> None:
        for root in (*all_sources(), *PRIVATE_HEADERS):
            with self.subTest(root=root):
                self.required_text(root)
                for path in local_include_closure(root):
                    if path.endswith(".h"):
                        check_header(path, read_source(path), path in PRIVATE_HEADERS)

    def test_cross_file_helpers_have_one_private_declaration_and_local_helpers_stay_static(self) -> None:
        headers = {
            path: self.required_text(path) for path in PRIVATE_HEADERS
        }
        stable = "\n".join(read_source(path) for path in PUBLIC_PATHS if path.endswith(".h"))
        for path, definitions in self.definitions.items():
            for name in definitions:
                lexical = c_declaration_text(self.texts[path])
                if re.search(rf"(?m)^static\b[^;{{}}]*\b{re.escape(name)}\s*\(", lexical):
                    continue
                if name in {"main", "micros_service_main"} or re.search(rf"\b{re.escape(name)}\s*\(", stable):
                    continue
                expected_header = FAMILY_HEADERS[family_for(path)]
                declared = [
                    header for header, text in headers.items()
                    if re.search(rf"\b{re.escape(name)}\s*\(", c_declaration_text(text))
                ]
                self.assertEqual([expected_header], declared, (path, name))

    def test_production_and_evidence_private_include_boundaries_are_recursive(self) -> None:
        production = (*SOURCE_SETS["MICROS_TRAP_SOURCES"], *SOURCE_SETS["MICROS_SYSCALL_SOURCES"])
        forbidden = set(PRIVATE_HEADERS[2:])
        for path in all_sources():
            with self.subTest(path=path):
                self.required_text(path)
                closure = set(local_include_closure(path))
                private = closure & set(PRIVATE_HEADERS)
                self.assertLessEqual(private, {FAMILY_HEADERS[family_for(path)]})
                if path in production:
                    self.assertFalse(closure & forbidden)

    def test_helper_dependency_direction_includes_transitive_header_code_and_macros(self) -> None:
        self.assertEqual(set(all_sources()), set(PERMITTED_OWNER_DEPENDENCIES))
        for path, definitions in self.definitions.items():
            dependency_text = header_executable_text(path)
            for name in definitions:
                with self.subTest(path=path, helper=name):
                    check_dependency_text(
                        self.texts, path, name,
                        c_function_body(self.texts[path], name) + "\n" + dependency_text,
                    )

    def test_exact_existing_context_storage_and_all_ten_abi_globals(self) -> None:
        self.assertEqual((3, 17, 8, 7, 3), (
            len(TRAP_FIELDS), len(KERNEL_FIELDS), len(RAW_FIELDS),
            len(IMAGE_OBJECTS), len(SAVED_REGISTER_OBJECTS),
        ))
        check_storage(self.texts)

    def test_existing_record_fields_union_and_native_capture_shape_are_exact(self) -> None:
        combined = "\n".join(self.texts.values()) + "\n" + "\n".join(
            self.required_text(path) for path in PRIVATE_HEADERS
        )
        for name, fields in RECORD_FIELDS.items():
            with self.subTest(record=name):
                self.assertEqual(tuple(map(compact, fields)), record_fields(combined, name))

    def test_image_actor_stack_transfer_and_bss_bounds_remain_named_and_exact(self) -> None:
        kernel = c_declaration_text("\n".join(
            self.texts.get(path, "") for path in SOURCE_SETS["MICROS_USER_RUNTIME_KERNEL_TEST_SOURCES"]
        ) + self.required_text('kernel/tests/runtime/user_runtime_test_internal.h'))
        service = c_declaration_text("\n".join(
            self.texts.get(path, "") for path in SOURCE_SETS["MICROS_USER_RUNTIME_SERVICE_TEST_SOURCES"]
        ) + self.required_text("tests/qemu/user_runtime_service_internal.h"))
        self.assertIn(
            compact("USER_RUNTIME_CLIENT = 0, USER_RUNTIME_SERVER, USER_RUNTIME_PEER, "
                    "USER_RUNTIME_PROCESS_COUNT, USER_RUNTIME_MAX_IMAGE_PAGES = 8,"),
            compact(kernel),
        )
        self.assertEqual(1, len(re.findall(r"\bUSER_RUNTIME_MAX_IMAGE_PAGES\s*=\s*8\b", kernel)))
        self.assertEqual(1, len(re.findall(r"\bTEST_TRANSFER_SIZE\s*=\s*32\b", service)))
        self.assertEqual(1, len(re.findall(r"\bTEST_BSS_PROBE_SIZE\s*=\s*64\b", service)))

    def test_original_nonreturning_entry_owns_the_inline_supervisor_register_capture(self) -> None:
        path = 'kernel/tests/runtime/user_runtime_test.c'
        entry = self.body(path, "micros_user_runtime_runtime_run_self_test")
        capture = (
            '__asm__ volatile("mv %0, sp\\n" "mv %1, gp\\n" "mv %2, tp" '
            ': "=r"(micros_user_runtime_test_saved_sp), '
            '"=r"(micros_user_runtime_test_saved_gp), "=r"(micros_user_runtime_test_saved_tp));'
        )
        self.assertEqual(1, compact(entry).count(compact(capture)))
        self.assertIn(compact(capture + "(void)saved_status; (void)micros_scheduler_start();"), compact(entry))
        for owner, text in self.texts.items():
            self.assertEqual(
                int(owner == path),
                text.count('"mv %0, sp\\n"'),
                "a returning setup helper cannot own the supervisor continuation",
            )
        restore = read_source("arch/riscv64/user_runtime_test.S")
        for register in ("sp", "gp", "tp"):
            self.assertIn("micros_user_runtime_test_saved_" + register, restore)
        startup = self.body("tests/qemu/user_runtime_service.c", "startup_checks")
        for register, variable in (("sp", "stack_pointer"), ("gp", "global_pointer"), ("tp", "thread_pointer")):
            self.assertIn(compact(f'__asm__ volatile("mv %0, {register}" : "=r"({variable}));'), compact(startup))

    def test_native_cases_and_raw_capture_keep_their_current_owners(self):
        main = c_function_body(self.texts["tests/host/runtime_test.c"], "main")
        self.assertEqual(NATIVE_CASE_ORDER, tuple(re.findall(r"\b(test_[A-Za-z0-9_]+)\s*\(", main)))
        wrappers = function_records(self.texts["tests/host/runtime_wrapper_test.c"])
        self.assertLessEqual(set(NATIVE_CASE_ORDER), set(wrappers))
        raw = c_function_body(self.texts["tests/host/runtime_test_fixture.c"], "micros_runtime_raw_syscall")
        self.assertEqual(tuple(str(index) for index in range(8)), tuple(re.findall(r"capture\.arguments\[(\d)\]\s*=", raw)))
        self.assertIn("capture.input_snapshot = *capture.expected_input;", raw)

    def test_existing_native_elf_ctest_qemu_timeout_and_source_bound_panic_identities(self) -> None:
        cmake = read_source("CMakeLists.txt")
        self.assertEqual(1, len(re.findall(r"\badd_test\s*\(\s*NAME\s+user-runtime\s+COMMAND\s+runtime_test\s*\)", cmake)))
        for target in ("test-unit-fast", "test-unit"):
            block = cmake_command(cmake, "add_custom_target", target)
            self.assertEqual(1, len(re.findall(r"\bruntime_test\b", block)))
        self.assertNotRegex(cmake, r"\bset_tests_properties\s*\([^)]*\buser-runtime\b[^)]*\bLABELS\s+slow\b")
        for target in ("test-qemu-user-runtime", "test-qemu-trap-panic"):
            block = cmake_command(cmake, "add_custom_target", target)
            expected_timeout = "12" if target == "test-qemu-user-runtime" else "8"
            self.assertRegex(block, rf"--timeout\s+{expected_timeout}\b")
            if target.endswith("trap-panic"):
                self.assertIn("MICROS_PANIC_SOURCE file=kernel/trap[.]c line=0x[0-9a-f]{16}", block)
                self.assertIn(
                    "MICROS_TRAP_CONTEXT origin=S sstatus=0x[0-9a-f]{16} "
                    "scause=0x0000000000000002 stval=0x[0-9a-f]{16} sepc=0x[0-9a-f]{16} "
                    "ra=0x[0-9a-f]{16} sp=0x[0-9a-f]{16}", block,
                )
                self.assertIn("--require-trap-context", block)
                self.assertIn("micros_trap_panic_test_fault", block)
                self.assertIn("unexpected-exception", block)
            else:
                self.assertIn(RUNTIME_MARKER.rstrip(), block)

    @classmethod
    def setUpClass(cls):
        cls.texts = {path: read_source(path) for path in all_sources()}
        cls.definitions = {path: tuple(function_records(text)) for path, text in cls.texts.items()}

    def body(self, path, name):
        return c_function_body(self.texts[path], name)

    def test_current_source_sets_and_path_classification(self):
        cmake = read_source("CMakeLists.txt")
        for name, paths in SOURCE_SETS.items():
            block = cmake_command(cmake, "set", name)
            self.assertEqual(paths, tuple(re.findall(r"(?:kernel|tests)/[A-Za-z0-9_/]+\.c", block)))
            self.assertEqual(len(paths), len(set(paths)))
            for path in (*paths[1:], FAMILY_HEADERS[name]):
                self.required_text(path)
                for tier in ("fast", "pr"):
                    self.assertEqual(
                        validation_plan.plan([paths[0]], tier, "main"),
                        validation_plan.plan([path], tier, "main"), path,
                    )
        self.assertIn(STRUCTURAL_TEST_PATH, validation_plan.HOST_UNIT_ONLY_INPUTS)

    def test_current_static_local_tables_keep_their_owners(self):
        observed = {}
        for path, text in self.texts.items():
            declarations = static_local_storage(text)
            if declarations:
                self.assertTrue(all(re.match(r"static\s+const\b", value) for value in declarations), path)
                observed[path] = tuple(" ".join(tokens(value)) for value in declarations)
        self.assertEqual(CURRENT_STATIC_LOCAL_OBJECTS, observed)

    def test_private_header_and_context_rejections_remain_fail_closed(self):
        header = FAMILY_HEADERS["MICROS_TRAP_SOURCES"]
        for bad in (
            "static inline void hidden(void) {}",
            "static unsigned int hidden;",
            "#define HIDDEN_STATE static int hidden;\n",
            "#define HIDDEN_CALL invoke()\n",
        ):
            with self.subTest(bad=bad), self.assertRaises(AssertionError):
                check_header(header, bad, private=True)
        texts = dict(self.texts)
        texts["tests/host/runtime_test_fixture.c"] += "\nstatic unsigned int hidden;"
        with self.assertRaisesRegex(AssertionError, "additional/duplicated"):
            check_storage(texts)


if __name__ == "__main__":
    unittest.main()

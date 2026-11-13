"""Current bootstrap target observation placement and closed recipe checks."""

from pathlib import Path
import re
import shlex

from tests.host.build_source_records import cmake_command, cmake_if_blocks
from tests.host.c_source_records import function_records, tokens
from tests.host.native_observations import direct_calls
from tests.host.selected_ipc_validation_source import AUDIT_SITES, validate_target_source as validate_ipc_source
from tests.host.source_snapshot import source_bytes


SITES = (
    ("kernel/boot/bootstrap_control_core.c", "micros_bootstrap_control_validate", "control", "KERNEL_BOOT", "COMPLETE_VALIDATOR_CALL"),
    ("kernel/boot/bootstrap_transitions.c", "bootstrap_runtime_validate_index", "runtime-index", "KERNEL_BOOT", "COMPLETE_VALIDATOR_CALL"),
    ("kernel/boot/bootstrap_runtime_validation.c", "micros_bootstrap_runtime_validate", "runtime", "KERNEL_BOOT", "COMPLETE_VALIDATOR_CALL"),
    ("kernel/memory/bootstrap_memory.c", "micros_frame_ownership_runtime_validate_metadata", "owner-runtime", "KERNEL_MEMORY", "INDEX_PROBE"),
    ("kernel/memory/frame_ownership_validation.c", "micros_frame_ownership_validate", "owner-complete", "KERNEL_MEMORY", "COMPLETE_VALIDATOR_CALL"),
    ("kernel/grant/grant.c", "micros_grant_registry_validate", "grant-registry", "KERNEL_GRANT", "COMPLETE_VALIDATOR_CALL"),
    ("kernel/grant/grant_runtime.c", "micros_grant_runtime_validate", "grant-runtime", "KERNEL_GRANT", "COMPLETE_VALIDATOR_CALL"),
    ("kernel/vm/vm_handoff_core.c", "micros_vm_handoff_state_validate", "vm-state", "KERNEL_VM", "COMPLETE_VALIDATOR_CALL"),
    ("kernel/vm/vm_handoff_runtime.c", "micros_vm_handoff_runtime_validate", "vm-runtime", "KERNEL_VM", "COMPLETE_VALIDATOR_CALL"),
    ("kernel/tty/tty_handoff_runtime_validate.c", "micros_tty_handoff_runtime_validate", "tty-runtime", "KERNEL_TTY", "COMPLETE_VALIDATOR_CALL"),
    ("kernel/memory/user_address_space_validation.c", "micros_user_address_space_validate_locked", "root-locked", "KERNEL_MEMORY", "COMPLETE_VALIDATOR_CALL"),
    ("kernel/memory/user_address_space_validation.c", "micros_user_address_space_validate", "root", "KERNEL_MEMORY", "COMPLETE_VALIDATOR_CALL"),
    ("kernel/memory/user_address_space_tty.c", "micros_user_address_space_prepare_tty_uart_mapping", "uart-prepare", "KERNEL_MEMORY", "COMPLETE_VALIDATOR_CALL"),
    ("kernel/memory/user_address_space_tty.c", "micros_user_address_space_validate_tty_uart_mapping", "uart-full", "KERNEL_MEMORY", "COMPLETE_VALIDATOR_CALL"),
    ("kernel/memory/user_address_space_tty.c", "micros_user_address_space_inspect_bootstrap_tty_uart", "uart-point", "KERNEL_MEMORY", "RANGE_VISIT"),
    ("kernel/execution/user_execution_validation.c", "micros_user_execution_validate_context", "context", "KERNEL_EXECUTION", "INDEX_PROBE"),
)
REPORTS = (
    ("kernel/tests/boot/bootstrap_test.c", "micros_bootstrap_test_handle_trap", "uart_flush"),
    ("kernel/tests/tty/tty_service_test.c", "micros_tty_service_test_handle_trap", "micros_diagnostic_test_finish"),
)
EXTRA_SOURCES = (
    "kernel/trap/syscall.c", *(row[0] for row in REPORTS),
    "tests/qemu/bootstrap_command_work.c", "tests/work/work_counts.c",
)


def validate_site_source(path, text):
    if path in AUDIT_SITES:
        validate_ipc_source(path, text)
    records = function_records(text)
    for unit, name, label, owner, kind in SITES:
        if unit != path:
            continue
        if name not in records:
            raise ValueError("BOOT02 target lost an observed function")
        body = records[name].body
        pattern = (
            r'#ifdef\s+MICROS_TEST_BOOTSTRAP_COMMAND_COUNTS\s+'
            r'MICROS_TEST_WORK_HIT\("boot02-' + re.escape(label) + r'",\s*'
            r'MICROS_WORK_OWNER_' + owner + r',\s*MICROS_WORK_KIND_' + kind + r',\s*1\);\s*#endif'
        )
        matches = tuple(re.finditer(pattern, body))
        if len(matches) != 1 or tokens(body).count("MICROS_TEST_WORK_HIT") != 1:
            raise ValueError("BOOT02 target lost or duplicated a primitive observation")
        prefix = body[:matches[0].start()]
        if direct_calls(prefix) or set(tokens(prefix)) & {"if", "for", "while", "return", "goto"}:
            raise ValueError("BOOT02 target observation moved after executed work")


def validate_target_observations(root):
    root = Path(root)
    for path in sorted({*AUDIT_SITES, *(row[0] for row in SITES)}):
        validate_site_source(path, source_bytes(root, root / path).decode())
    for path, name, predecessor in REPORTS:
        body = tokens(function_records(source_bytes(root, root / path).decode())[name].body)
        if (
            body.count("MICROS_BOOT02_REPORT") != 1
            or not body.index(predecessor) < body.index("MICROS_TEST_STACK_REPORT")
            < body.index("MICROS_BOOT02_REPORT") < body.index("sbi_system_reset")
        ):
            raise ValueError("BOOT02 reporting left the original terminal semantic boundary")
    dispatch = tokens(function_records(source_bytes(root, root / "kernel/trap/syscall.c").decode())["syscall_dispatch_operation"].body)
    if dispatch.count("MICROS_BOOT02_DISPATCH") != 1 or "micros_bootstrap_handle_captured_user_ecall" in dispatch:
        raise ValueError("BOOT02 capture no longer encloses the actual command")
    observer = function_records(source_bytes(root, root / "tests/qemu/bootstrap_command_work.c").decode())
    body = tokens(observer["micros_boot02_dispatch"].body)
    if (
        body.count("micros_bootstrap_handle_captured_user_ecall") != 1
        or body.count("return") != 1
        or not body.index("begin_capture") < body.index("micros_bootstrap_handle_captured_user_ecall")
        < body.index("finish_capture") < body.index("return")
    ):
        raise ValueError("BOOT02 command escapes its measured scope")
    cmake = source_bytes(root, root / "CMakeLists.txt").decode()
    blocks = cmake_if_blocks(cmake, "MICROS_BUILD_BOOTSTRAP_LAUNCHER_TEST OR MICROS_BUILD_TTY_SERVICE_TEST")
    if len(blocks) != 1:
        raise ValueError("BOOT02 target recipe has ambiguous scope")
    recipe = cmake_command(blocks[0], "set_source_files_properties", "kernel/objects/kernel_objects.c")
    arguments = shlex.split(recipe[recipe.index("(") + 1:-1])
    expected_sources = {*AUDIT_SITES, *(row[0] for row in SITES), *EXTRA_SOURCES}
    if (
        arguments[-3:] != ["PROPERTIES", "COMPILE_DEFINITIONS", "MICROS_TEST_WORK_COUNTS=1;MICROS_TEST_BOOTSTRAP_COMMAND_COUNTS=1"]
        or len(arguments[:-3]) != len(expected_sources) or set(arguments[:-3]) != expected_sources
    ):
        raise ValueError("BOOT02 target omitted a measured object or its collector definition")

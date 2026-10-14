from __future__ import annotations

import ast
import hashlib
from pathlib import Path
import re
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

from tools import validation_plan
from tests.host.test_frame_ownership_source_layout import (
    c_function_definitions,
    c_lexical_text,
    cmake_command,
    cmake_if_blocks,
    static_definition_paths,
)
from tests.host.test_kernel_pm_control_source_layout import macro_definition_text


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
CMAKE_PATH = REPOSITORY_ROOT / "CMakeLists.txt"
FUNCTION_REVIEW_LINE_LIMIT = 120
STRUCTURAL_TEST_PATH = "tests/host/test_vm_service_source_layout.py"
GENERATOR_PATH = "tools/generate_vm_scratch_preemption_fixture.py"

SOURCE_SETS = {
    "MICROS_VM_BOOT_VALIDATION_SOURCES": (
        "lib/vm/vm_boot_validate.c",
    ),
    "MICROS_VM_IMAGE_SOURCES": (
        "servers/vm/vm_image.c",
        "servers/vm/vm_stack.c",
        "servers/vm/vm_elf.c",
    ),
    "MICROS_VM_CORE_SOURCES": (
        "servers/vm/vm_core.c",
        "servers/vm/vm_validation.c",
        "servers/vm/vm_frames.c",
        "servers/vm/vm_load.c",
        "servers/vm/vm_exit.c",
    ),
    "MICROS_VM_CORE_HOST_TEST_SOURCES": (
        "tests/host/vm_core_test.c",
        "tests/host/vm_core_test_fixture.c",
        "tests/host/vm_core_boot_test.c",
        "tests/host/vm_core_load_test.c",
        "tests/host/vm_core_abort_test.c",
        "tests/host/vm_core_validation_test.c",
    ),
    "MICROS_VM_SERVICE_CORE_SOURCES": (
        "servers/vm/vm_service_core.c",
        "servers/vm/vm_service_protocol.c",
        "servers/vm/vm_service_load.c",
        "servers/vm/vm_service_release.c",
    ),
    "MICROS_VM_SERVICE_BACKEND_SOURCES": (
        "servers/vm/vm_service_backend.c",
    ),
    "MICROS_VM_SERVICE_HOST_TEST_SOURCES": (
        "tests/host/vm_service_test_fixture.c",
        "tests/host/vm_service_test.c",
        "tests/host/vm_service_protocol_test.c",
        "tests/host/vm_service_backend_test.c",
    ),
    "MICROS_VM_SERVICE_QEMU_SOURCES": (
        "tests/qemu/vm_server.c",
        "tests/qemu/vm_server_fixture.c",
        "tests/qemu/vm_server_handoff.c",
        "tests/qemu/vm_server_probe.c",
        "tests/qemu/vm_server_tty.c",
        "tests/qemu/vm_server_scratch.c",
        "tests/qemu/vm_server_pm.c",
        "tests/qemu/vm_server_control.c",
    ),
    "MICROS_VM_SCRATCH_PREEMPTION_QEMU_TEST_SOURCES": (
        "kernel/vm_scratch_preemption_test.c",
        "kernel/vm_scratch_preemption_test_fixture.c",
        "kernel/vm_scratch_preemption_test_runtime.c",
        "kernel/vm_scratch_preemption_test_report.c",
        "kernel/vm_scratch_preemption_test_trap.c",
    ),
}

PRIVATE_HEADERS = (
    "servers/vm/vm_internal.h",
    "servers/vm/vm_image_internal.h",
    "servers/vm/vm_service_internal.h",
    "tests/host/vm_core_test_internal.h",
    "tests/host/vm_service_test_internal.h",
    "tests/qemu/vm_server_internal.h",
    "kernel/vm_scratch_preemption_test_internal.h",
)

TEST_PRIVATE_HEADERS = (
    "tests/host/vm_core_test_internal.h",
    "tests/host/vm_service_test_internal.h",
    "tests/qemu/vm_server_internal.h",
    "kernel/vm_scratch_preemption_test_internal.h",
)

STABLE_HEADER_HASHES = {
    "include/micros/vm_bootstrap.h":
        "a37f0a611a0e688478dfb5dfbee487b31a04fa9ff534538c4cb23c5c4967e866",
    "servers/vm/vm_core.h":
        "97c64900fcf40cb7dcfdba8a61ddabcad290feded7224710c1f2d29d99125bba",
    "servers/vm/vm_image.h":
        "aeed2227d2817ebde729302df12fe71a3f25dffc774058f2660304ebd0a1f1ae",
    "kernel/vm_scratch_preemption_test.h":
        "bd86b1b80ed1a9cfa5c8cba0809678c3c2b88d259d5a678f50c4f913c68e18bc",
    "kernel/vm_scratch_preemption_test_fixture.h":
        "d30f8be9e3a36721f7182f31f700f82fac9ffb945526381df446dc554b9bc02f",
}

DECLARATION_ONLY_HEADERS = (
    *PRIVATE_HEADERS,
    *STABLE_HEADER_HASHES,
    "include/micros/runtime.h",
    "include/micros/vm.h",
    "include/micros/vm_service.h",
    "servers/vm/vm_service_core.h",
)

VM_SERVICE_PATH_CLASSIFICATION_EQUIVALENTS = (
    (
        "servers/vm/vm_core.c",
        (
            "servers/vm/vm_validation.c",
            "servers/vm/vm_frames.c",
            "servers/vm/vm_load.c",
            "servers/vm/vm_exit.c",
            "servers/vm/vm_internal.h",
            "servers/vm/vm_service_core.h",
            "servers/vm/vm_service_core.c",
            "servers/vm/vm_service_protocol.c",
            "servers/vm/vm_service_load.c",
            "servers/vm/vm_service_release.c",
            "servers/vm/vm_service_backend.c",
            "servers/vm/vm_service_internal.h",
        ),
    ),
    (
        "servers/vm/vm_image.c",
        (
            "servers/vm/vm_stack.c",
            "servers/vm/vm_elf.c",
            "servers/vm/vm_image_internal.h",
        ),
    ),
    (
        "tests/host/vm_core_test.c",
        (
            "tests/host/vm_core_test_fixture.c",
            "tests/host/vm_core_boot_test.c",
            "tests/host/vm_core_load_test.c",
            "tests/host/vm_core_abort_test.c",
            "tests/host/vm_core_validation_test.c",
            "tests/host/vm_core_test_internal.h",
            "tests/host/vm_service_test_fixture.c",
            "tests/host/vm_service_test.c",
            "tests/host/vm_service_protocol_test.c",
            "tests/host/vm_service_backend_test.c",
            "tests/host/vm_service_test_internal.h",
        ),
    ),
    (
        "tests/qemu/vm_server.c",
        (
            "tests/qemu/vm_server_fixture.c",
            "tests/qemu/vm_server_handoff.c",
            "tests/qemu/vm_server_probe.c",
            "tests/qemu/vm_server_tty.c",
            "tests/qemu/vm_server_scratch.c",
            "tests/qemu/vm_server_pm.c",
            "tests/qemu/vm_server_control.c",
            "tests/qemu/vm_server_internal.h",
        ),
    ),
    (
        "kernel/vm_scratch_preemption_test.c",
        (
            "kernel/vm_scratch_preemption_test_fixture.c",
            "kernel/vm_scratch_preemption_test_runtime.c",
            "kernel/vm_scratch_preemption_test_report.c",
            "kernel/vm_scratch_preemption_test_trap.c",
            "kernel/vm_scratch_preemption_test_internal.h",
        ),
    ),
)

CORE_FINAL_OWNERS = {
    "micros_vm_state_initialize": "servers/vm/vm_core.c",
    "micros_vm_state_validate": "servers/vm/vm_validation.c",
    "micros_vm_load_prepare": "servers/vm/vm_load.c",
    "micros_vm_load_bind": "servers/vm/vm_load.c",
    "micros_vm_load_next_page": "servers/vm/vm_load.c",
    "micros_vm_load_scratch_mapped": "servers/vm/vm_load.c",
    "micros_vm_load_fill_scratch": "servers/vm/vm_load.c",
    "micros_vm_load_scratch_committed": "servers/vm/vm_load.c",
    "micros_vm_load_freeze_plan": "servers/vm/vm_load.c",
    "micros_vm_load_frozen": "servers/vm/vm_load.c",
    "micros_vm_load_aborted": "servers/vm/vm_load.c",
    "micros_vm_load_committed": "servers/vm/vm_load.c",
    "micros_vm_exit_release_begin": "servers/vm/vm_exit.c",
    "micros_vm_exit_release_complete": "servers/vm/vm_exit.c",
}

IMAGE_FINAL_OWNERS = {
    "micros_application_stack_plan": "servers/vm/vm_stack.c",
    "micros_application_stack_build": "servers/vm/vm_stack.c",
    "micros_vm_elf_plan": "servers/vm/vm_elf.c",
}

TEST_FINAL_OWNERS = {
    "test_record_abi": "tests/host/vm_core_boot_test.c",
    "test_boot_import": "tests/host/vm_core_boot_test.c",
    "test_load_lifecycle": "tests/host/vm_core_load_test.c",
    "test_abort_paths": "tests/host/vm_core_abort_test.c",
    "test_capacity_and_invariants":
        "tests/host/vm_core_validation_test.c",
    "test_staged_inputs_and_immutable_database":
        "tests/host/vm_core_validation_test.c",
}

PRIVATE_FUNCTION_OWNERS = {
    "servers/vm/vm_core.c": (
        "vm_core_zero_bytes", "vm_core_copy_bytes", "vm_core_bytes_are_zero",
        "vm_core_bytes_equal", "vm_core_ranges_overlap",
        "vm_core_endpoint_is_canonical", "vm_core_process_is_canonical",
        "vm_core_user_permissions_are_canonical", "vm_core_stack_plan_equal",
        "vm_core_initial_frame_is_free", "vm_core_load_page_digest",
    ),
    "servers/vm/vm_frames.c": (
        "vm_core_physical_to_frame_index", "vm_core_frame_index_to_physical",
        "vm_core_record_is_zero", "vm_core_record_contains_physical",
        "vm_core_physical_is_in_record_other_than", "vm_core_select_lowest_frames",
        "vm_core_selected_frames_match",
    ),
    "servers/vm/vm_validation.c": ("vm_core_validate_state",),
    "servers/vm/vm_image.c": (
        "vm_image_read_u16_le", "vm_image_read_u32_le", "vm_image_read_u64_le",
        "vm_image_write_u64_le", "vm_image_zero_bytes", "vm_image_copy_bytes",
        "vm_image_bytes_are_zero", "vm_image_add_u64", "vm_image_ranges_overlap",
        "vm_image_address_ranges_overlap",
    ),
}

TARGET_FUNCTION_OWNERS = {
    "servers/vm/vm_service_core.c": (
        "service_endpoint_is_concrete", "service_bindings_are_valid",
        "micros_vm_service_initialize", "service_instance_is_valid",
        "service_backend_is_complete", "micros_vm_service_handle_message",
    ),
    "servers/vm/vm_service_protocol.c": (
        "vm_service_clear_bytes", "service_read_word", "service_write_word",
        "service_request_shape_is_valid", "vm_service_decode_request", "vm_service_build_reply",
    ),
    "servers/vm/vm_service_load.c": (
        "service_copy_inputs", "service_bind_load", "service_fill_pages",
        "service_freeze_load", "service_prepare_result", "vm_service_load",
    ),
    "servers/vm/vm_service_release.c": (
        "service_release_frozen", "vm_service_release",
    ),
    "servers/vm/vm_service_backend.c": (
        "vm_backend_copy", "vm_backend_begin", "vm_backend_map",
        "vm_backend_scratch", "vm_backend_scratch_commit", "vm_backend_freeze",
        "vm_backend_abort", "vm_backend_commit", "vm_backend_exit",
        "micros_vm_service_runtime_backend",
    ),
    "tests/qemu/vm_server.c": ("micros_service_main",),
    "tests/qemu/vm_server_fixture.c": (
        "vm_server_context_get", "vm_server_clear_bytes",
        "vm_server_bytes_are_zero", "vm_server_write_u32_le",
        "vm_server_read_u32_le", "vm_server_read_u64_le",
        "vm_server_find_service", "vm_server_validate_configuration",
    ),
    "tests/qemu/vm_server_handoff.c": (
        "vm_server_send_ready", "vm_server_complete_handoff",
    ),
    "tests/qemu/vm_server_probe.c": ("vm_server_serve_probe",),
    "tests/qemu/vm_server_tty.c": (
        "vm_server_notification_is_canonical", "vm_server_map_tty_uart",
    ),
    "tests/qemu/vm_server_scratch.c": (
        "vm_server_find_free_frame", "vm_server_run_scratch_preemption",
    ),
    "tests/qemu/vm_server_pm.c": (
        "pm_test_write_u64_le", "pm_test_collect_free_frames",
        "pm_test_digest_byte", "pm_test_digest_u32", "pm_test_digest_u64",
        "pm_test_page_plan_digest", "pm_test_write_child_page",
        "pm_test_initialize_pages", "pm_test_build_freeze_plan",
        "pm_test_load_child", "pm_test_commit_child", "pm_test_release_child",
        "pm_test_serve_request",
    ),
    "tests/qemu/vm_server_control.c": (
        "vm_server_configure_control", "vm_server_serve_control",
    ),
    "kernel/vm_scratch_preemption_test.c": (
        "micros_vm_scratch_preemption_test_launch",
        "micros_vm_scratch_preemption_test_before_user_timer",
        "micros_vm_scratch_preemption_test_after_user_timer",
        "micros_vm_scratch_preemption_test_after_syscall_return",
    ),
    "kernel/vm_scratch_preemption_test_fixture.c": (
        "vm_scratch_test_context_get",
    ),
    "kernel/vm_scratch_preemption_test_runtime.c": (
        "vm_scratch_test_failure", "vm_scratch_test_read_satp",
        "vm_scratch_test_expected_satp", "vm_scratch_test_current_is",
        "vm_scratch_test_runtime_is_valid",
    ),
    "kernel/vm_scratch_preemption_test_report.c": (
        "acquire_report_authority", "report_authority_is_complete",
        "report_fault_context_is_valid", "report_load_record_is_valid",
        "report_removed_mapping_is_valid", "vm_scratch_test_report_success",
    ),
    "kernel/vm_scratch_preemption_test_trap.c": (
        "peer_hold_trap_matches", "vm_hook_trap_matches", "peer_run_trap_matches",
        "hold_peer", "arm_vm_preemption",
        "micros_vm_scratch_preemption_test_handle_trap",
    ),
}

PERMITTED_OWNER_DEPENDENCIES = {
    "lib/vm/vm_boot_validate.c": (),
    "servers/vm/vm_image.c": (),
    "servers/vm/vm_stack.c": ("servers/vm/vm_image.c",),
    "servers/vm/vm_elf.c": ("servers/vm/vm_image.c",),
    "servers/vm/vm_core.c": ("lib/vm/vm_boot_validate.c",),
    "servers/vm/vm_frames.c": ("servers/vm/vm_core.c",),
    "servers/vm/vm_validation.c": (
        "servers/vm/vm_core.c", "servers/vm/vm_frames.c",
        "servers/vm/vm_stack.c", "servers/vm/vm_elf.c",
    ),
    "servers/vm/vm_load.c": (
        "servers/vm/vm_core.c", "servers/vm/vm_frames.c",
        "servers/vm/vm_validation.c",
        "servers/vm/vm_stack.c", "servers/vm/vm_elf.c",
    ),
    "servers/vm/vm_exit.c": (
        "servers/vm/vm_core.c", "servers/vm/vm_frames.c",
        "servers/vm/vm_validation.c",
    ),
    "servers/vm/vm_service_core.c": (
        "servers/vm/vm_service_protocol.c", "servers/vm/vm_service_load.c",
        "servers/vm/vm_service_release.c", "servers/vm/vm_validation.c",
    ),
    "servers/vm/vm_service_protocol.c": (),
    "servers/vm/vm_service_load.c": ("servers/vm/vm_load.c",),
    "servers/vm/vm_service_release.c": (
        "servers/vm/vm_load.c", "servers/vm/vm_exit.c",
    ),
    "servers/vm/vm_service_backend.c": (),
    "tests/qemu/vm_server.c": (
        "tests/qemu/vm_server_fixture.c", "tests/qemu/vm_server_handoff.c",
        "tests/qemu/vm_server_probe.c", "tests/qemu/vm_server_tty.c",
        "tests/qemu/vm_server_scratch.c", "tests/qemu/vm_server_pm.c",
        "lib/vm/vm_boot_validate.c", "servers/vm/vm_image.c",
        "servers/vm/vm_core.c", "servers/vm/vm_frames.c",
        "servers/vm/vm_validation.c", "servers/vm/vm_load.c",
        "servers/vm/vm_exit.c", "servers/vm/vm_stack.c", "servers/vm/vm_elf.c",
        "tests/qemu/vm_server_control.c",
    ),
    "tests/qemu/vm_server_fixture.c": (),
    "tests/qemu/vm_server_handoff.c": ("tests/qemu/vm_server_fixture.c",),
    "tests/qemu/vm_server_probe.c": ("tests/qemu/vm_server_fixture.c",),
    "tests/qemu/vm_server_tty.c": ("tests/qemu/vm_server_fixture.c",),
    "tests/qemu/vm_server_scratch.c": ("tests/qemu/vm_server_fixture.c",),
    "tests/qemu/vm_server_pm.c": ("tests/qemu/vm_server_fixture.c",),
    "tests/qemu/vm_server_control.c": (
        "tests/qemu/vm_server_fixture.c", "servers/vm/vm_service_core.c",
        "servers/vm/vm_service_backend.c",
    ),
    "kernel/vm_scratch_preemption_test.c": (
        "kernel/vm_scratch_preemption_test_fixture.c",
        "kernel/vm_scratch_preemption_test_runtime.c",
    ),
    "kernel/vm_scratch_preemption_test_fixture.c": (),
    "kernel/vm_scratch_preemption_test_runtime.c": (),
    "kernel/vm_scratch_preemption_test_report.c": (
        "kernel/vm_scratch_preemption_test_runtime.c",
    ),
    "kernel/vm_scratch_preemption_test_trap.c": (
        "kernel/vm_scratch_preemption_test_fixture.c",
        "kernel/vm_scratch_preemption_test_runtime.c",
        "kernel/vm_scratch_preemption_test_report.c",
    ),
}

FILE_SCOPE_OBJECT_OWNERS = {
    "tests/host/vm_core_test_fixture.c": (
        r"static\s+struct\s+vm_core_test_context\s+test_context\s*(?:=|;)",
    ),
    "tests/qemu/vm_server_fixture.c": (
        r"volatile\s+struct\s+micros_bootstrap_service_config\s+"
        r"micros_bootstrap_service_config\b",
        r"_Alignas\(MICROS_VM_BOOT_INFO_ALIGNMENT\)\s+"
        r"__attribute__\(\(section\(\s*\),\s*used\)\)\s+"
        r"struct\s+micros_vm_boot_info\s+micros_vm_boot_info\b",
        r"static\s+struct\s+vm_server_context\s+service_context\s*(?:=|;)",
    ),
    "kernel/vm_scratch_preemption_test_fixture.c": (
        r"static\s+struct\s+vm_scratch_preemption_test_context\s+"
        r"test_context\s*(?:=|;)",
        r"static\s+const\s+struct\s+micros_bootstrap_expected_service\s+"
        r"expected_services\b",
        r"static\s+const\s+struct\s+micros_privilege_profile\s+profiles\b",
        r"static\s+const\s+struct\s+micros_bootstrap_scheduler_policy\s+policies\b",
        r"const\s+struct\s+micros_bootstrap_runtime_config\s+"
        r"vm_scratch_test_runtime_config\b",
    ),
}

FINAL_SYMBOL_OWNERS = {
    **CORE_FINAL_OWNERS,
    **IMAGE_FINAL_OWNERS,
    "micros_vm_scratch_preemption_test_handle_trap":
        "kernel/vm_scratch_preemption_test_trap.c",
    **TEST_FINAL_OWNERS,
}

CURRENT_SYMBOL_OWNERS = {
    **{
        symbol: "servers/vm/vm_core.c"
        for symbol in CORE_FINAL_OWNERS
    },
    **{
        symbol: "servers/vm/vm_image.c"
        for symbol in IMAGE_FINAL_OWNERS
    },
    "micros_vm_scratch_preemption_test_handle_trap":
        "kernel/vm_scratch_preemption_test.c",
    **{
        symbol: "tests/host/vm_core_test.c"
        for symbol in TEST_FINAL_OWNERS
    },
}

INITIAL_SYMBOL_OWNER_MIGRATION = tuple(
    (
        CURRENT_SYMBOL_OWNERS[symbol],
        final_owner,
        symbol,
    )
    for symbol, final_owner in FINAL_SYMBOL_OWNERS.items()
    if CURRENT_SYMBOL_OWNERS[symbol] != final_owner
)

SYMBOL_OWNER_MIGRATION = ()

INITIAL_LONG_FUNCTION_MIGRATION = (
    ("servers/vm/vm_core.c", "vm_core_validate_state"),
    ("servers/vm/vm_core.c", "micros_vm_load_prepare"),
    ("servers/vm/vm_core.c", "micros_vm_load_bind"),
    ("servers/vm/vm_image.c", "micros_vm_elf_plan"),
    ("lib/vm/vm_boot_validate.c", "micros_vm_boot_validate"),
    (
        "kernel/vm_scratch_preemption_test.c",
        "micros_vm_scratch_preemption_test_handle_trap",
    ),
    ("kernel/vm_scratch_preemption_test.c", "report_success"),
    ("tests/host/vm_core_test.c", "initialize_boot_info"),
    ("tests/host/vm_core_test.c", "test_load_lifecycle"),
    ("tests/qemu/vm_server.c", "pm_test_load_child"),
    (GENERATOR_PATH, "render_fixture"),
)

LONG_FUNCTION_MIGRATION = ()

VM_CORE_CASE_ORDER = (
    "test_record_abi",
    "test_boot_import",
    "test_load_lifecycle",
    "test_abort_paths",
    "test_capacity_and_invariants",
    "test_staged_inputs_and_immutable_database",
)

VM_SERVICE_CASE_ORDER = (
    "test_vm_service_configuration",
    "test_vm_service_load_and_release",
    "test_vm_service_abort_and_failure",
    "test_vm_service_protocol_boundaries",
    "test_vm_service_release_identity",
    "test_vm_service_expected_load_rejection",
    "test_vm_service_kernel_disagreement",
    "test_vm_service_runtime_backend",
)

VM_SYMBOLS = (
    "micros_vm_scratch_preemption_vm_hook_trap",
    "micros_vm_scratch_preemption_vm_hook_spin",
    "micros_vm_scratch_preemption_vm_hook_resume",
    "micros_vm_scratch_preemption_stale_load_fault",
    "micros_vm_scratch_preemption_stale_load_unexpected",
)

PEER_SYMBOLS = (
    "micros_vm_scratch_preemption_peer_hold_trap",
    "micros_vm_scratch_preemption_peer_hold_resume",
    "micros_vm_scratch_preemption_peer_run_trap",
    "micros_vm_scratch_preemption_peer_run_resume",
)


def all_source_set_paths() -> tuple[str, ...]:
    return tuple(
        dict.fromkeys(
            path
            for sources in SOURCE_SETS.values()
            for path in sources
        )
    )


def production_sources() -> tuple[str, ...]:
    return (
        *SOURCE_SETS["MICROS_VM_BOOT_VALIDATION_SOURCES"],
        *SOURCE_SETS["MICROS_VM_IMAGE_SOURCES"],
        *SOURCE_SETS["MICROS_VM_CORE_SOURCES"],
        *SOURCE_SETS["MICROS_VM_SERVICE_CORE_SOURCES"],
        *SOURCE_SETS["MICROS_VM_SERVICE_BACKEND_SOURCES"],
    )


def accepted_family_paths() -> tuple[str, ...]:
    return tuple(
        dict.fromkeys(
            (
                *all_source_set_paths(),
                *PRIVATE_HEADERS,
                *STABLE_HEADER_HASHES,
                "servers/vm/vm_service_core.h",
                GENERATOR_PATH,
            )
        )
    )


def discovered_family_paths() -> tuple[str, ...]:
    patterns = (
        "servers/vm/vm*.c",
        "servers/vm/vm*.h",
        "lib/vm/vm_boot*.c",
        "lib/vm/vm_boot*.h",
        "kernel/vm_scratch_preemption_test*.c",
        "kernel/vm_scratch_preemption_test*.h",
        "tests/host/vm_core*.c",
        "tests/host/vm_core*.h",
        "tests/host/vm_service*.c",
        "tests/host/vm_service*.h",
        "tests/qemu/vm_server*.c",
        "tests/qemu/vm_server*.h",
    )
    discovered = {
        str(path.relative_to(REPOSITORY_ROOT))
        for pattern in patterns
        for path in REPOSITORY_ROOT.glob(pattern)
    }
    discovered.add("include/micros/vm_bootstrap.h")
    discovered.add(GENERATOR_PATH)
    return tuple(sorted(discovered))


def resolve_local_include(source: Path, include: str) -> Path | None:
    candidates = (
        source.parent / include,
        REPOSITORY_ROOT / include,
        REPOSITORY_ROOT / "include" / include,
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    return None


def compiled_source_header_closure() -> tuple[str, ...]:
    observed = {
        dependency
        for relative in accepted_family_paths()
        if Path(relative).suffix in {".c", ".h"}
        for dependency in local_include_closure(relative)
    }
    return tuple(sorted(observed))


def local_include_closure(relative: str) -> tuple[str, ...]:
    pending = [(REPOSITORY_ROOT / relative).resolve()]
    observed = set()
    include_pattern = re.compile(
        r'(?m)^\s*#\s*include\s*"([^"]+)"'
    )

    while pending:
        path = pending.pop()
        if path in observed:
            continue
        observed.add(path)
        for include in include_pattern.findall(
            path.read_text(encoding="utf-8")
        ):
            resolved = resolve_local_include(path, include)
            if (
                resolved is not None
                and REPOSITORY_ROOT in resolved.parents
            ):
                pending.append(resolved)

    return tuple(
        sorted(str(path.relative_to(REPOSITORY_ROOT)) for path in observed)
    )


def owner_dependency_text(relative: str) -> str:
    selected = []
    for dependency in local_include_closure(relative):
        text = (REPOSITORY_ROOT / dependency).read_text(encoding="utf-8")
        if dependency in DECLARATION_ONLY_HEADERS and dependency != relative:
            text = macro_definition_text(text)
        lexical = c_lexical_text(text)
        selected.append(re.sub(
            r"\b(?:struct|union|enum)\s+[A-Za-z_][A-Za-z0-9_]*",
            "", lexical,
        ))
    return "\n".join(selected)


def c_declaration_text(text: str) -> str:
    lines = c_lexical_text(text).splitlines(keepends=True)
    output = []
    continuation = False
    for line in lines:
        if continuation or line.lstrip().startswith("#"):
            output.append("".join("\n" if char == "\n" else " " for char in line))
            continuation = line.rstrip().endswith("\\")
        else:
            output.append(line)
    return "".join(output)


def require_single_object_declaration(declaration: str) -> None:
    depth = 0
    for char in declaration:
        if char in "({[":
            depth += 1
        elif char in ")}]":
            depth -= 1
            if depth < 0:
                raise AssertionError("unbalanced object declaration")
        elif char == "," and depth == 0:
            raise AssertionError("multiple objects in one file-scope declaration")
    if depth != 0:
        raise AssertionError("unbalanced object declaration")


def file_scope_objects(text: str) -> tuple[str, ...]:
    lexical = c_declaration_text(text)
    objects = []
    depth = 0
    start = 0
    function_body = False
    for index, char in enumerate(lexical):
        if char == "{":
            if depth == 0:
                prefix = lexical[start:index].strip()
                function_body = prefix.endswith(")") and "=" not in prefix
            depth += 1
        elif char == "}":
            depth -= 1
            if depth < 0:
                raise AssertionError("unexpected closing brace")
            if depth == 0 and function_body:
                start = index + 1
                function_body = False
        elif char == ";" and depth == 0:
            declaration = lexical[start:index + 1].strip()
            start = index + 1
            if (
                not declaration
                or declaration.startswith("typedef ")
                or declaration.startswith("_Static_assert")
                or (
                    declaration.startswith("extern ")
                    and "=" not in declaration and "{" not in declaration
                )
                or re.fullmatch(
                    r"[A-Za-z_][A-Za-z0-9_\s*]*"
                    r"\b[A-Za-z_][A-Za-z0-9_]*\s*\([^;]*\)\s*;",
                    declaration, re.DOTALL,
                )
                or (
                    "{" in declaration
                    and re.fullmatch(
                        r"(?:struct|union|enum)(?:\s+[A-Za-z_][A-Za-z0-9_]*)?",
                        declaration.split("{", 1)[0].strip(),
                    )
                    and declaration.rsplit("}", 1)[-1].strip() == ";"
                )
                or re.fullmatch(
                    r"(?:struct|union|enum)\s+[A-Za-z_][A-Za-z0-9_]*\s*;",
                    declaration,
                )
                or re.fullmatch(
                    r"MICROS_(?:TRAP|HART_TRAP|USER_CONTEXT)_ASSERT_OFFSET"
                    r"\([^;]*\)\s*;", declaration, re.DOTALL,
                )
            ):
                continue
            require_single_object_declaration(declaration)
            objects.append(declaration)
    if depth != 0 or lexical[start:].strip():
        raise AssertionError("unterminated file-scope declaration")
    return tuple(objects)


def python_function_definitions(text: str) -> dict[str, int]:
    definitions = {}

    for node in ast.walk(ast.parse(text)):
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            definitions[node.name] = node.end_lineno - node.lineno + 1
    return definitions


class VmServiceSourceLayoutTest(unittest.TestCase):
    def test_expected_paths_and_family_inventory_are_exact(self) -> None:
        missing = [
            relative
            for relative in (*all_source_set_paths(), *PRIVATE_HEADERS)
            if not (REPOSITORY_ROOT / relative).is_file()
        ]
        self.assertEqual([], missing)
        self.assertEqual(
            tuple(sorted(accepted_family_paths())),
            discovered_family_paths(),
        )

    def test_stable_headers_are_byte_exact(self) -> None:
        observed = {
            path: hashlib.sha256(
                (REPOSITORY_ROOT / path).read_bytes()
            ).hexdigest()
            for path in STABLE_HEADER_HASHES
        }
        self.assertEqual(STABLE_HEADER_HASHES, observed)

    def test_cmake_source_sets_and_consumers_are_exact(self) -> None:
        cmake = CMAKE_PATH.read_text(encoding="utf-8")

        for name, expected in SOURCE_SETS.items():
            with self.subTest(source_set=name):
                block = cmake_command(cmake, "set", name)
                listed = tuple(
                    re.findall(
                        r"(?:kernel|lib/vm|servers/vm|tests/host|"
                        r"tests/qemu)/[A-Za-z0-9_]+\.c",
                        block,
                    )
                )
                self.assertEqual(expected, listed)
        for source in all_source_set_paths():
            self.assertEqual(1, cmake.count(source), source)

        vm_bootstrap = cmake_command(
            cmake,
            "add_executable",
            "vm_bootstrap_test",
        )
        spawn_abi = cmake_command(
            cmake,
            "add_executable",
            "spawn_abi_test",
        )
        vm_core = cmake_command(cmake, "add_executable", "vm_core_test")
        micros = cmake_command(cmake, "add_executable", "micros")
        vm_server = cmake_command(
            cmake,
            "micros_add_bootstrap_user",
            "micros_vm_server",
        )
        scratch = "\n".join(
            cmake_if_blocks(
                cmake,
                "MICROS_BUILD_VM_SCRATCH_PREEMPTION_TEST",
            )
        )
        consumers = {
            "vm-bootstrap": (
                vm_bootstrap,
                ("MICROS_VM_BOOT_VALIDATION_SOURCES",),
            ),
            "spawn-abi": (
                spawn_abi,
                ("MICROS_VM_IMAGE_SOURCES",),
            ),
            "vm-core": (
                vm_core,
                (
                    "MICROS_VM_BOOT_VALIDATION_SOURCES",
                    "MICROS_VM_IMAGE_SOURCES",
                    "MICROS_VM_CORE_SOURCES",
                    "MICROS_VM_CORE_HOST_TEST_SOURCES",
                    "MICROS_VM_SERVICE_CORE_SOURCES",
                    "MICROS_VM_SERVICE_BACKEND_SOURCES",
                    "MICROS_VM_SERVICE_HOST_TEST_SOURCES",
                ),
            ),
            "vm-server": (
                vm_server,
                (
                    "MICROS_VM_BOOT_VALIDATION_SOURCES",
                    "MICROS_VM_IMAGE_SOURCES",
                    "MICROS_VM_CORE_SOURCES",
                    "MICROS_VM_SERVICE_QEMU_SOURCES",
                    "MICROS_VM_SERVICE_CORE_SOURCES",
                    "MICROS_VM_SERVICE_BACKEND_SOURCES",
                ),
            ),
            "micros": (
                micros,
                ("MICROS_VM_BOOT_VALIDATION_SOURCES",),
            ),
            "scratch": (
                scratch,
                ("MICROS_VM_SCRATCH_PREEMPTION_QEMU_TEST_SOURCES",),
            ),
        }
        for consumer, (block, selected) in consumers.items():
            for source_set in SOURCE_SETS:
                with self.subTest(
                    consumer=consumer,
                    source_set=source_set,
                ):
                    self.assertEqual(
                        int(source_set in selected),
                        block.count(f"${{{source_set}}}"),
                    )
                    for relative in SOURCE_SETS[source_set]:
                        self.assertNotIn(relative, block)

    def test_private_headers_are_declarations_only(self) -> None:
        definitions = {}
        objects = {}
        static = {}

        for relative in DECLARATION_ONLY_HEADERS:
            path = REPOSITORY_ROOT / relative
            if not path.is_file():
                continue
            text = path.read_text(encoding="utf-8")
            selected = c_function_definitions(text)
            if selected:
                definitions[relative] = selected
            object_definitions = file_scope_objects(text)
            if object_definitions:
                objects[relative] = object_definitions
            static_matches = re.findall(
                r"(?m)^\s*(?:_Alignas\([^\n]+\)\s*)?"
                r"static\b[^\n;]*(?:;|\{)",
                c_lexical_text(text),
            )
            if static_matches:
                static[relative] = tuple(static_matches)
        self.assertEqual({}, definitions)
        self.assertEqual({}, objects)
        self.assertEqual({}, static)

    def test_sources_do_not_include_source_or_test_private_headers(self) -> None:
        source_include = re.compile(
            r"(?m)^\s*#\s*include\s*[<\"][^>\"]+\.(?:c|py)[>\"]"
        )
        source_violations = []
        private_violations = []

        for relative in compiled_source_header_closure():
            path = REPOSITORY_ROOT / relative
            if not path.is_file():
                continue
            text = path.read_text(encoding="utf-8")
            if source_include.search(text):
                source_violations.append(relative)
        for relative in production_sources():
            for dependency in local_include_closure(relative):
                if dependency in TEST_PRIVATE_HEADERS:
                    private_violations.append((relative, dependency))
        self.assertEqual([], source_violations)
        self.assertEqual([], private_violations)

    def test_symbol_owners_follow_exact_migration_map(self) -> None:
        initial = set(INITIAL_SYMBOL_OWNER_MIGRATION)
        migration = set(SYMBOL_OWNER_MIGRATION)
        expected_current = {}
        definitions = {}

        self.assertEqual(23, len(INITIAL_SYMBOL_OWNER_MIGRATION))
        self.assertEqual(len(migration), len(SYMBOL_OWNER_MIGRATION))
        self.assertTrue(migration <= initial)
        self.assertEqual((), SYMBOL_OWNER_MIGRATION)
        for current, final, symbol in migration:
            self.assertEqual(FINAL_SYMBOL_OWNERS[symbol], final)
            expected_current[symbol] = current
        for relative in all_source_set_paths():
            path = REPOSITORY_ROOT / relative
            if not path.is_file():
                continue
            for symbol in c_function_definitions(
                path.read_text(encoding="utf-8")
            ):
                definitions.setdefault(symbol, []).append(relative)
        for symbol, final in FINAL_SYMBOL_OWNERS.items():
            with self.subTest(symbol=symbol):
                self.assertEqual(
                    [expected_current.get(symbol, final)],
                    definitions.get(symbol, []),
                )

    def test_private_and_target_helper_owners_are_exact(self) -> None:
        definitions = {}
        for relative in compiled_source_header_closure():
            for name in c_function_definitions(
                (REPOSITORY_ROOT / relative).read_text(encoding="utf-8")
            ):
                definitions.setdefault(name, []).append(relative)
        for relative, names in {
            **PRIVATE_FUNCTION_OWNERS, **TARGET_FUNCTION_OWNERS,
        }.items():
            for name in names:
                with self.subTest(symbol=name):
                    self.assertEqual([relative], definitions.get(name, []))
        for relative, expected in TARGET_FUNCTION_OWNERS.items():
            with self.subTest(owner=relative):
                self.assertEqual(
                    expected,
                    tuple(c_function_definitions(
                        (REPOSITORY_ROOT / relative).read_text(encoding="utf-8")
                    )),
                )

    def test_dependency_direction_is_exact_across_local_includes(self) -> None:
        symbol_owners = {}
        for relative in PERMITTED_OWNER_DEPENDENCIES:
            for name in c_function_definitions(
                (REPOSITORY_ROOT / relative).read_text(encoding="utf-8")
            ):
                symbol_owners.setdefault(name, set()).add(relative)
        for relative, dependencies in PERMITTED_OWNER_DEPENDENCIES.items():
            text = owner_dependency_text(relative)
            allowed = {relative, *dependencies}
            violations = {
                name: sorted(owners - allowed)
                for name, owners in symbol_owners.items()
                if owners - allowed
                and re.search(rf"\b{re.escape(name)}\b", text)
            }
            with self.subTest(owner=relative):
                self.assertEqual({}, violations)

    def test_file_scope_state_and_abi_objects_have_one_owner(self) -> None:
        observed = {}
        for relative in compiled_source_header_closure():
            objects = file_scope_objects(
                (REPOSITORY_ROOT / relative).read_text(encoding="utf-8")
            )
            if objects:
                observed[relative] = objects
        self.assertEqual(set(FILE_SCOPE_OBJECT_OWNERS), set(observed))
        for relative, patterns in FILE_SCOPE_OBJECT_OWNERS.items():
            objects = observed[relative]
            with self.subTest(owner=relative):
                self.assertEqual(len(patterns), len(objects))
                for pattern in patterns:
                    self.assertEqual(
                        1, sum(bool(re.match(pattern, item)) for item in objects),
                        pattern,
                    )

    def test_file_scope_scan_rejects_hidden_header_definitions(self) -> None:
        interface = (
            "#define FLAGS \\\n"
            "    (FIRST | SECOND)\n"
            "struct context { int member; };\n"
            "typedef struct context context_t;\n"
            "extern struct context external;\n"
            "void helper(struct context *context);\n"
        )
        self.assertEqual((), file_scope_objects(interface))
        for definition in (
            "int hidden;",
            "extern int hidden = 1;",
            "struct context hidden = {0};",
            "struct inline_context { int member; } hidden;",
            "context_t hidden;",
            "int (*hidden)(void);",
        ):
            with self.subTest(definition=definition):
                self.assertEqual(
                    1, len(file_scope_objects(interface + definition)),
                )
        self.assertEqual(
            (),
            file_scope_objects(
                "static int helper(void) { static int local; return local; }\n"
            ),
        )

    def test_recursive_include_scan_keeps_hidden_dependency_references(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            (root / "owner.c").write_text(
                '#include "forward.h"\nvoid owner(void) {}\n',
                encoding="utf-8",
            )
            (root / "forward.h").write_text(
                '#include "nested.h"\n', encoding="utf-8",
            )
            (root / "nested.h").write_text(
                "static inline void hidden(void) {\n"
                "    micros_vm_load_aborted(0);\n}\n"
                "static struct vm_server_context duplicate = {0};\n",
                encoding="utf-8",
            )
            with patch(f"{__name__}.REPOSITORY_ROOT", root):
                self.assertEqual(
                    ("forward.h", "nested.h", "owner.c"),
                    local_include_closure("owner.c"),
                )
                self.assertIn(
                    "micros_vm_load_aborted", owner_dependency_text("owner.c"),
                )
                self.assertEqual(
                    1, len(file_scope_objects(
                        (root / "nested.h").read_text(encoding="utf-8")
                    )),
                )

    def test_file_scope_scan_rejects_multiple_objects_in_one_declaration(self) -> None:
        for declaration in (
            "static struct vm_server_context service_context, hidden;",
            "static struct vm_server_context service_context = {0}, hidden = {0};",
            "static struct vm_server_context service_context[2], hidden[2];",
        ):
            with self.subTest(declaration=declaration):
                with self.assertRaisesRegex(AssertionError, "multiple objects"):
                    file_scope_objects(declaration)
        scalar_pattern = FILE_SCOPE_OBJECT_OWNERS["tests/qemu/vm_server_fixture.c"][-1]
        self.assertIsNone(re.match(
            scalar_pattern, "static struct vm_server_context service_context[2];",
        ))

    def test_current_mutable_state_owners_are_exact(self) -> None:
        sources = all_source_set_paths()
        expected = {
            "boot_info": [],
            "state": [],
            "executable": [],
            "request": [],
            "scratch_page": [],
            "vm_state": [],
            "test_phase": [],
            "timer_observed": [],
            "scratch_physical_address": [],
            "scratch_load_token": [],
            "scratch_process": [],
        }

        for name, owners in expected.items():
            self.assertEqual(
                owners,
                static_definition_paths(sources, name),
                name,
            )
        contexts = []
        pattern = re.compile(
            r"\bstatic\s+(?:(?:const|volatile)\s+)*struct\s+"
            r"(vm_core_test_context|vm_server_context|"
            r"vm_scratch_preemption_test_context|micros_vm_state|"
            r"micros_vm_boot_info)\b"
        )
        for relative in compiled_source_header_closure():
            text = c_lexical_text(
                (REPOSITORY_ROOT / relative).read_text(encoding="utf-8")
            )
            contexts.extend(
                (relative, match.group(1))
                for match in pattern.finditer(text)
            )
        self.assertEqual(
            [
                (
                    "kernel/vm_scratch_preemption_test_fixture.c",
                    "vm_scratch_preemption_test_context",
                ),
                ("tests/host/vm_core_test_fixture.c", "vm_core_test_context"),
                ("tests/qemu/vm_server_fixture.c", "vm_server_context"),
            ],
            contexts,
        )

    def test_evidence_identities_and_orders_remain_stable(self) -> None:
        cmake = CMAKE_PATH.read_text(encoding="utf-8")
        host = (
            REPOSITORY_ROOT / "tests/host/vm_core_test.c"
        ).read_text(encoding="utf-8")
        service = (
            REPOSITORY_ROOT / "tests/qemu/vm_server.c"
        ).read_text(encoding="utf-8")
        generator = (
            REPOSITORY_ROOT / GENERATOR_PATH
        ).read_text(encoding="utf-8")
        scratch_marker = (
            "MICROS_VM_SCRATCH_PREEMPTION_TEST_PASS "
            "map=real timer=preempted roots=distinct "
            "inactive-validation=accepted reactivation=fenced "
            "sentinel=preserved commit=real "
            "stale-translation=faulted pte=absent "
            "ownership=transferable scratch=clear"
        )

        self.assertEqual(1, cmake.count("NAME vm-core"))
        self.assertEqual(1, cmake.count("NAME vm-bootstrap"))
        self.assertEqual(1, cmake.count("NAME spawn-abi"))
        for target in (
            "test-qemu-vm-handoff",
            "test-qemu-pm-service",
            "test-qemu-vm-scratch-preemption",
            "test-qemu-tty",
            "test-qemu-ramfs",
            "test-qemu-vfs",
            "test-qemu-vm-ready-early",
            "test-qemu-vm-self-fault",
            "test-qemu-vm-self-fault-sealed",
        ):
            cmake_command(cmake, "add_custom_target", target)
        scratch_target = cmake_command(
            cmake,
            "add_custom_target",
            "test-qemu-vm-scratch-preemption",
        )
        self.assertIn("--timeout\n            45", scratch_target)
        self.assertEqual(1, cmake.count(scratch_marker))
        self.assertIn("--resident-page-limit\n                160", cmake)

        main_body = host[host.index("int main"):]
        positions = [main_body.index(name) for name in VM_CORE_CASE_ORDER]
        self.assertEqual(sorted(positions), positions)
        self.assertEqual(
            (*VM_CORE_CASE_ORDER, *VM_SERVICE_CASE_ORDER),
            tuple(re.findall(r"\b(test_[A-Za-z0-9_]+)\s*\(", main_body)),
        )

        service_body = service[service.index("void micros_service_main"):]
        validate_position = service_body.index(
            "vm_server_validate_configuration"
        )
        initialize_position = service_body.index(
            "micros_vm_state_initialize"
        )
        first_ready = service_body.index("vm_server_send_ready")
        handoff_position = service_body.index(
            "vm_server_complete_handoff"
        )
        second_ready = service_body.index(
            "vm_server_send_ready",
            first_ready + 1,
        )
        self.assertEqual(2, service_body.count("vm_server_send_ready"))
        self.assertTrue(
            validate_position
            < initialize_position
            < first_ready
            < handoff_position
            < second_ready
        )
        for symbol in (*VM_SYMBOLS, *PEER_SYMBOLS):
            self.assertEqual(1, generator.count(f'"{symbol}"'), symbol)

    def test_ordinary_service_control_is_wired_before_readiness(self) -> None:
        facade = (REPOSITORY_ROOT / "tests/qemu/vm_server.c").read_text(encoding="utf-8")
        self.assertIn("vm_server_configure_control(context)", facade)
        handoff = facade.index("vm_server_complete_handoff")
        configure = facade.index("vm_server_configure_control")
        ready = facade.index("vm_server_send_ready", handoff)
        self.assertLess(handoff, configure)
        self.assertLess(configure, ready)
        self.assertIn("vm_server_serve_control(context, &message)", facade)
        public = (REPOSITORY_ROOT / "servers/vm/vm_service_core.h").read_text(encoding="utf-8")
        self.assertIn("struct micros_vm_state *state;", public)
        self.assertNotIn("executable_bytes[", public)
        self.assertNotIn("request_bytes[", public)
        for path in (*SOURCE_SETS["MICROS_VM_SERVICE_CORE_SOURCES"],
                     *SOURCE_SETS["MICROS_VM_SERVICE_BACKEND_SOURCES"]):
            self.assertNotIn(
                '#include "servers/vm/vm_internal.h"',
                (REPOSITORY_ROOT / path).read_text(encoding="utf-8"),
            )

    def test_validation_plan_inherits_path_classification(self) -> None:
        baselines = {
            baseline
            for baseline, _ in VM_SERVICE_PATH_CLASSIFICATION_EQUIVALENTS
        }
        observed = tuple(
            item
            for item in validation_plan.PATH_CLASSIFICATION_EQUIVALENTS
            if item[0] in baselines
        )
        self.assertEqual(
            VM_SERVICE_PATH_CLASSIFICATION_EQUIVALENTS,
            observed,
        )
        for baseline, equivalents in VM_SERVICE_PATH_CLASSIFICATION_EQUIVALENTS:
            for equivalent in equivalents:
                for tier in ("fast", "pr"):
                    self.assertEqual(
                        validation_plan.plan(
                            [baseline],
                            tier,
                            "main",
                        ),
                        validation_plan.plan(
                            [equivalent],
                            tier,
                            "main",
                        ),
                        equivalent,
                    )
        self.assertIn(
            STRUCTURAL_TEST_PATH,
            validation_plan.HOST_UNIT_ONLY_INPUTS,
        )

    def test_long_function_migration_matches_review_signal(self) -> None:
        initial = set(INITIAL_LONG_FUNCTION_MIGRATION)
        migration = set(LONG_FUNCTION_MIGRATION)
        oversized = {}

        self.assertEqual(11, len(INITIAL_LONG_FUNCTION_MIGRATION))
        self.assertEqual(len(migration), len(LONG_FUNCTION_MIGRATION))
        self.assertTrue(migration <= initial)
        self.assertEqual((), LONG_FUNCTION_MIGRATION)
        for relative in compiled_source_header_closure():
            path = REPOSITORY_ROOT / relative
            for name, line_count in c_function_definitions(
                path.read_text(encoding="utf-8")
            ).items():
                if line_count > FUNCTION_REVIEW_LINE_LIMIT:
                    oversized[(relative, name)] = line_count
        generator = (REPOSITORY_ROOT / GENERATOR_PATH).read_text(
            encoding="utf-8"
        )
        for name, line_count in python_function_definitions(
            generator
        ).items():
            if line_count > FUNCTION_REVIEW_LINE_LIMIT:
                oversized[(GENERATOR_PATH, name)] = line_count
        self.assertEqual(migration, set(oversized), oversized)


if __name__ == "__main__":
    unittest.main()

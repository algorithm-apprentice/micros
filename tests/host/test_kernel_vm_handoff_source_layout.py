from __future__ import annotations

import hashlib
from pathlib import Path
import re
import unittest

from tools import validation_plan
from tests.host.test_frame_ownership_source_layout import (
    c_function_definitions,
    cmake_command,
    cmake_if_blocks,
    static_definition_paths,
)
from tests.host import kernel_layout_source


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
CMAKE_PATH = REPOSITORY_ROOT / "CMakeLists.txt"
FUNCTION_REVIEW_LINE_LIMIT = 120

SOURCE_SETS = {
    "MICROS_VM_HANDOFF_CORE_SOURCES": (
        "kernel/vm_handoff_core.c",
        "kernel/vm_handoff_abi.c",
        "kernel/vm_handoff_boot.c",
        "kernel/vm_handoff_load.c",
        "kernel/vm_handoff_scratch.c",
        "kernel/vm_handoff_freeze.c",
        "kernel/vm_handoff_commit.c",
        "kernel/vm_handoff_release.c",
    ),
    "MICROS_VM_HANDOFF_SYSCALL_SOURCES": (
        "kernel/vm_handoff_syscall.c",
        "kernel/vm_handoff_syscall_runtime.c",
        "kernel/vm_handoff_syscall_memory.c",
        "kernel/vm_handoff_syscall_boot.c",
        "kernel/vm_handoff_syscall_load.c",
        "kernel/vm_handoff_syscall_scratch.c",
        "kernel/vm_handoff_syscall_freeze.c",
        "kernel/vm_handoff_syscall_release.c",
    ),
    "MICROS_VM_SNAPSHOT_SOURCES": (
        "kernel/vm_snapshot.c",
        "kernel/vm_snapshot_validation.c",
    ),
    "MICROS_VM_HANDOFF_CORE_HOST_TEST_SOURCES": (
        "tests/host/vm_handoff_test.c",
        "tests/host/vm_handoff_test_fixture.c",
        "tests/host/vm_handoff_abi_test.c",
        "tests/host/vm_handoff_boot_test.c",
        "tests/host/vm_handoff_load_test.c",
    ),
    "MICROS_VM_BOOTSTRAP_HOST_TEST_SOURCES": (
        "tests/host/vm_bootstrap_test.c",
        "tests/host/vm_bootstrap_test_fixture.c",
        "tests/host/vm_bootstrap_abi_test.c",
        "tests/host/vm_bootstrap_build_test.c",
    ),
    "MICROS_VM_HANDOFF_SYSCALL_HOST_TEST_SOURCES": (
        "tests/host/vm_handoff_syscall_test.c",
        "tests/host/vm_handoff_syscall_test_fixture.c",
        "tests/host/vm_handoff_syscall_test_boot.c",
        "tests/host/vm_handoff_syscall_test_load.c",
        "tests/host/vm_handoff_syscall_test_runtime_backend.c",
        "tests/host/vm_handoff_syscall_test_boot_backend.c",
        "tests/host/vm_handoff_syscall_test_load_backend.c",
    ),
    "MICROS_ADDRESS_SPACE_HANDOFF_QEMU_TEST_SOURCES": (
        "kernel/address_space_handoff_test.c",
        "kernel/address_space_handoff_test_fixture.c",
        "kernel/address_space_handoff_test_setup.c",
        "kernel/address_space_handoff_test_snapshot.c",
        "kernel/address_space_handoff_test_handoff.c",
        "kernel/address_space_handoff_test_runtime.c",
        "kernel/address_space_handoff_test_syscall.c",
        "kernel/address_space_handoff_test_tty.c",
        "kernel/address_space_handoff_test_trap.c",
    ),
}

PRIVATE_HEADERS = (
    "kernel/vm_handoff_internal.h",
    "kernel/vm_handoff_syscall_internal.h",
    "kernel/vm_snapshot_internal.h",
    "tests/host/vm_handoff_test_internal.h",
    "tests/host/vm_bootstrap_test_internal.h",
    "tests/host/vm_handoff_syscall_test_internal.h",
    "kernel/address_space_handoff_test_internal.h",
)

UNAFFECTED_FAMILY_PATHS = (
    "kernel/vm_handoff_runtime.c",
    "kernel/vm_handoff_runtime.h",
    "kernel/vm_handoff_test.c",
    "kernel/vm_handoff_test.h",
    "kernel/vm_handoff_test_fixture.h",
)

TEST_PRIVATE_HEADERS = (
    "tests/host/vm_handoff_test_internal.h",
    "tests/host/vm_bootstrap_test_internal.h",
    "tests/host/vm_handoff_syscall_test_internal.h",
    "kernel/address_space_handoff_test_internal.h",
)

STABLE_HEADER_HASHES = {
    "kernel/vm_handoff_core.h":
        "0fa181de7f323a18a84be941a5b2b96d76998b607fc719216b737b235d63fdd2",
    "kernel/vm_handoff_syscall.h":
        "65cd9aa2c3089f495a6f8d0219eed2ba44a2dc57fd2b7d170bd5d3c17e05791b",
    "kernel/vm_snapshot.h":
        "d2bddf7d46addb5ee768a92ca356f4cd552e756a871b861131c002dd54792942",
    "kernel/address_space_handoff_test.h":
        "a228b45fb43b9516f279e9fecbc675f98b4c29eb2e285faa58d52d82e844cc84",
}

VM_PATH_CLASSIFICATION_EQUIVALENTS = (
    (
        "kernel/vm_handoff_core.c",
        (
            "kernel/vm_handoff_abi.c",
            "kernel/vm_handoff_boot.c",
            "kernel/vm_handoff_load.c",
            "kernel/vm_handoff_scratch.c",
            "kernel/vm_handoff_freeze.c",
            "kernel/vm_handoff_commit.c",
            "kernel/vm_handoff_release.c",
            "kernel/vm_handoff_internal.h",
        ),
    ),
    (
        "kernel/vm_handoff_syscall.c",
        (
            "kernel/vm_handoff_syscall_runtime.c",
            "kernel/vm_handoff_syscall_memory.c",
            "kernel/vm_handoff_syscall_boot.c",
            "kernel/vm_handoff_syscall_load.c",
            "kernel/vm_handoff_syscall_scratch.c",
            "kernel/vm_handoff_syscall_freeze.c",
            "kernel/vm_handoff_syscall_release.c",
            "kernel/vm_handoff_syscall_internal.h",
        ),
    ),
    (
        "kernel/vm_snapshot.c",
        (
            "kernel/vm_snapshot_validation.c",
            "kernel/vm_snapshot_internal.h",
        ),
    ),
    (
        "kernel/address_space_handoff_test.c",
        (
            "kernel/address_space_handoff_test_fixture.c",
            "kernel/address_space_handoff_test_setup.c",
            "kernel/address_space_handoff_test_snapshot.c",
            "kernel/address_space_handoff_test_handoff.c",
            "kernel/address_space_handoff_test_runtime.c",
            "kernel/address_space_handoff_test_syscall.c",
            "kernel/address_space_handoff_test_tty.c",
            "kernel/address_space_handoff_test_trap.c",
            "kernel/address_space_handoff_test_internal.h",
        ),
    ),
    (
        "tests/host/vm_handoff_syscall_test.c",
        (
            "tests/host/vm_handoff_syscall_test_fixture.c",
            "tests/host/vm_handoff_syscall_test_boot.c",
            "tests/host/vm_handoff_syscall_test_load.c",
            "tests/host/vm_handoff_syscall_test_runtime_backend.c",
            "tests/host/vm_handoff_syscall_test_boot_backend.c",
            "tests/host/vm_handoff_syscall_test_load_backend.c",
            "tests/host/vm_handoff_syscall_test_internal.h",
        ),
    ),
    (
        "tests/host/vm_handoff_test.c",
        (
            "tests/host/vm_handoff_test_fixture.c",
            "tests/host/vm_handoff_abi_test.c",
            "tests/host/vm_handoff_boot_test.c",
            "tests/host/vm_handoff_load_test.c",
            "tests/host/vm_handoff_test_internal.h",
        ),
    ),
    (
        "tests/host/vm_bootstrap_test.c",
        (
            "tests/host/vm_bootstrap_test_fixture.c",
            "tests/host/vm_bootstrap_abi_test.c",
            "tests/host/vm_bootstrap_build_test.c",
            "tests/host/vm_bootstrap_test_internal.h",
        ),
    ),
)

PORTABLE_FINAL_OWNERS = {
    "micros_vm_handoff_state_validate":
        "kernel/vm_handoff_core.c",
    "micros_vm_load_page_plan_digest":
        "kernel/vm_handoff_core.c",
    "micros_vm_handoff_decode":
        "kernel/vm_handoff_abi.c",
    "micros_vm_handoff_decode_command":
        "kernel/vm_handoff_abi.c",
    "micros_vm_handoff_decode_tty_mapping":
        "kernel/vm_handoff_abi.c",
    "micros_vm_load_begin_decode":
        "kernel/vm_handoff_abi.c",
    "micros_vm_scratch_map_decode":
        "kernel/vm_handoff_abi.c",
    "micros_vm_scratch_commit_decode":
        "kernel/vm_handoff_abi.c",
    "micros_vm_load_freeze_decode":
        "kernel/vm_handoff_abi.c",
    "micros_vm_load_abort_decode":
        "kernel/vm_handoff_abi.c",
    "micros_vm_load_commit_decode":
        "kernel/vm_handoff_abi.c",
    "micros_vm_exit_release_decode":
        "kernel/vm_handoff_abi.c",
    "micros_vm_tty_mapping_matches":
        "kernel/vm_handoff_boot.c",
    "micros_vm_handoff_state_prepare":
        "kernel/vm_handoff_boot.c",
    "micros_vm_handoff_summary_matches":
        "kernel/vm_handoff_boot.c",
    "micros_vm_handoff_commit_prevalidated":
        "kernel/vm_handoff_boot.c",
    "micros_vm_self_fault_classify":
        "kernel/vm_handoff_boot.c",
    "micros_vm_load_begin_preflight":
        "kernel/vm_handoff_load.c",
    "micros_vm_load_begin_commit_prevalidated":
        "kernel/vm_handoff_load.c",
    "micros_vm_scratch_map_preflight":
        "kernel/vm_handoff_scratch.c",
    "micros_vm_scratch_map_commit_prevalidated":
        "kernel/vm_handoff_scratch.c",
    "micros_vm_scratch_map_bind_prevalidated":
        "kernel/vm_handoff_scratch.c",
    "micros_vm_scratch_map_finish_prevalidated":
        "kernel/vm_handoff_scratch.c",
    "micros_vm_scratch_commit_preflight":
        "kernel/vm_handoff_scratch.c",
    "micros_vm_scratch_commit_commit_prevalidated":
        "kernel/vm_handoff_scratch.c",
    "micros_vm_load_freeze_preflight":
        "kernel/vm_handoff_freeze.c",
    "micros_vm_load_freeze_commit_prevalidated":
        "kernel/vm_handoff_freeze.c",
    "micros_vm_load_mark_prepared":
        "kernel/vm_handoff_commit.c",
    "micros_vm_load_mark_unprepared":
        "kernel/vm_handoff_commit.c",
    "micros_vm_load_mark_activated":
        "kernel/vm_handoff_commit.c",
    "micros_vm_load_commit_preflight":
        "kernel/vm_handoff_commit.c",
    "micros_vm_load_commit_commit_prevalidated":
        "kernel/vm_handoff_commit.c",
    "micros_vm_load_abort_preflight":
        "kernel/vm_handoff_release.c",
    "micros_vm_load_abort_commit_prevalidated":
        "kernel/vm_handoff_release.c",
    "micros_vm_load_mark_exit_detached":
        "kernel/vm_handoff_release.c",
    "micros_vm_exit_release_preflight":
        "kernel/vm_handoff_release.c",
    "micros_vm_exit_release_commit_prevalidated":
        "kernel/vm_handoff_release.c",
}

RUNTIME_BACKEND_SYMBOLS = (
    "micros_bootstrap_runtime_state",
    "micros_bootstrap_runtime_authoritative_state",
    "micros_bootstrap_runtime_validate",
    "micros_bootstrap_runtime_validate_vm_prepared",
    "micros_bootstrap_runtime_fail",
    "micros_bootstrap_control_find_binding",
    "micros_vm_handoff_runtime_authoritative_state",
    "micros_vm_handoff_runtime_state",
    "micros_vm_handoff_runtime_validate",
    "micros_pm_control_runtime_state",
    "micros_pm_control_state_validate",
    "micros_pm_control_runtime_validate",
    "micros_ipc_runtime_authoritative_registry",
    "micros_frame_ownership_runtime_ledger",
    "micros_process_resolve",
    "micros_thread_resolve",
    "micros_thread_ipc_state_is_clear",
    "micros_ipc_thread_has_staged_kernel_notification",
    "micros_endpoint_resolve_active",
    "micros_privilege_profile_allows_kernel_operation",
    "micros_trap_panic_entry",
)

BOOT_BACKEND_SYMBOLS = (
    "micros_tty_handoff_runtime_authoritative_state",
    "micros_tty_handoff_runtime_prepare_mapped",
    "micros_tty_handoff_runtime_commit_mapped_prevalidated",
    "micros_tty_handoff_runtime_validate",
    "micros_plic_validate",
    "uart_console_handoff_is_quiesced",
    "micros_user_address_space_complete_wired_handoff",
    "micros_user_address_space_validate",
    "micros_user_address_space_prepare_tty_uart_mapping",
    "micros_user_address_space_commit_tty_uart_mapping",
    "micros_ipc_prepare_kernel_notification",
    "micros_ipc_commit_kernel_notification_prevalidated",
)

LOAD_BACKEND_SYMBOLS = (
    "micros_user_address_space_translate",
    "micros_frame_ownership_runtime_vm_claim_preflight",
    "micros_frame_ownership_runtime_vm_claim_commit_prevalidated",
    "micros_frame_ownership_runtime_vm_release_transferable_preflight",
    "micros_frame_ownership_runtime_vm_release_transferable_commit_prevalidated",
    "micros_frame_ownership_runtime_vm_freeze_preflight",
    "micros_frame_ownership_runtime_vm_freeze_commit_prevalidated",
    "micros_frame_ownership_runtime_vm_release_process_preflight",
    "micros_frame_ownership_runtime_vm_release_process_commit_prevalidated",
    "micros_user_address_space_prepare_vm_scratch_mapping",
    "micros_user_address_space_commit_vm_scratch_mapping",
    "micros_user_address_space_prepare_vm_scratch_removal",
    "micros_user_address_space_commit_vm_scratch_removal",
    "micros_user_address_space_prepare_vm_freeze",
    "micros_user_address_space_construct_vm_freeze_prevalidated",
    "micros_user_address_space_install_vm_freeze_prevalidated",
    "micros_user_address_space_prepare_vm_teardown",
    "micros_user_address_space_commit_vm_teardown_prevalidated",
)

FINAL_SYMBOL_OWNERS = {
    **PORTABLE_FINAL_OWNERS,
    "micros_vm_handoff_handle_captured_user_ecall":
        "kernel/vm_handoff_syscall.c",
    "micros_vm_snapshot_prepare":
        "kernel/vm_snapshot.c",
    "micros_vm_snapshot_validate_current":
        "kernel/vm_snapshot_validation.c",
    "micros_address_space_handoff_runtime_run_self_test":
        "kernel/address_space_handoff_test.c",
    "micros_address_space_handoff_test_before_ecall":
        "kernel/address_space_handoff_test_syscall.c",
    "micros_address_space_handoff_test_after_dispatch":
        "kernel/address_space_handoff_test_syscall.c",
    "micros_address_space_handoff_test_after_return":
        "kernel/address_space_handoff_test_syscall.c",
    "micros_address_space_handoff_test_enter_production":
        "kernel/address_space_handoff_test_trap.c",
    "micros_address_space_handoff_test_handle_trap":
        "kernel/address_space_handoff_test_trap.c",
    "micros_vm_handoff_test_run":
        "tests/host/vm_handoff_test.c",
    **{
        symbol: "tests/host/vm_handoff_syscall_test_runtime_backend.c"
        for symbol in RUNTIME_BACKEND_SYMBOLS
    },
    **{
        symbol: "tests/host/vm_handoff_syscall_test_boot_backend.c"
        for symbol in BOOT_BACKEND_SYMBOLS
    },
    **{
        symbol: "tests/host/vm_handoff_syscall_test_load_backend.c"
        for symbol in LOAD_BACKEND_SYMBOLS
    },
}

CURRENT_SYMBOL_OWNERS = {
    **{
        symbol: "kernel/vm_handoff_core.c"
        for symbol in PORTABLE_FINAL_OWNERS
    },
    "micros_vm_handoff_handle_captured_user_ecall":
        "kernel/vm_handoff_syscall.c",
    "micros_vm_snapshot_prepare":
        "kernel/vm_snapshot.c",
    "micros_vm_snapshot_validate_current":
        "kernel/vm_snapshot.c",
    "micros_address_space_handoff_runtime_run_self_test":
        "kernel/address_space_handoff_test.c",
    "micros_address_space_handoff_test_before_ecall":
        "kernel/address_space_handoff_test.c",
    "micros_address_space_handoff_test_after_dispatch":
        "kernel/address_space_handoff_test.c",
    "micros_address_space_handoff_test_after_return":
        "kernel/address_space_handoff_test.c",
    "micros_address_space_handoff_test_enter_production":
        "kernel/address_space_handoff_test.c",
    "micros_address_space_handoff_test_handle_trap":
        "kernel/address_space_handoff_test.c",
    "micros_vm_handoff_test_run":
        "tests/host/vm_handoff_test.c",
    **{
        symbol: "tests/host/vm_handoff_syscall_test.c"
        for symbol in (
            *RUNTIME_BACKEND_SYMBOLS,
            *BOOT_BACKEND_SYMBOLS,
            *LOAD_BACKEND_SYMBOLS,
        )
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

SYMBOL_OWNER_MIGRATION = tuple(
    migration
    for migration in INITIAL_SYMBOL_OWNER_MIGRATION
    if migration[1] not in {
        "kernel/vm_handoff_abi.c",
        "kernel/vm_handoff_boot.c",
        "kernel/vm_handoff_load.c",
        "kernel/vm_handoff_scratch.c",
        "kernel/vm_handoff_freeze.c",
        "kernel/vm_handoff_commit.c",
        "kernel/vm_handoff_release.c",
        "kernel/vm_snapshot_validation.c",
        "tests/host/vm_handoff_syscall_test_runtime_backend.c",
        "tests/host/vm_handoff_syscall_test_boot_backend.c",
        "tests/host/vm_handoff_syscall_test_load_backend.c",
        "kernel/address_space_handoff_test_syscall.c",
        "kernel/address_space_handoff_test_trap.c",
    }
)

INITIAL_LONG_FUNCTION_MIGRATION = (
    (
        "kernel/vm_handoff_core.c",
        "micros_vm_handoff_state_validate",
    ),
    (
        "kernel/vm_handoff_syscall.c",
        "handle_tty_mapping_command",
    ),
    (
        "kernel/vm_handoff_syscall.c",
        "handle_load_freeze_command",
    ),
    (
        "kernel/vm_handoff_syscall.c",
        "handle_load_abort_command",
    ),
    (
        "kernel/vm_handoff_syscall.c",
        "micros_vm_handoff_handle_captured_user_ecall",
    ),
    ("kernel/vm_snapshot.c", "micros_vm_snapshot_prepare"),
    ("kernel/address_space_handoff_test.c", "arm_handoff_syscall"),
    (
        "kernel/address_space_handoff_test.c",
        "transition_handoff_syscall",
    ),
    (
        "kernel/address_space_handoff_test.c",
        "micros_address_space_handoff_runtime_run_self_test",
    ),
    ("tests/host/vm_handoff_syscall_test.c", "reset_fixture"),
    ("tests/host/vm_handoff_test.c", "test_decodes_load_commands"),
    ("tests/host/vm_bootstrap_test.c", "initialize_build_fixture"),
    (
        "tests/host/vm_bootstrap_test.c",
        "test_build_failure_clears_candidate",
    ),
)

LONG_FUNCTION_MIGRATION = tuple(
    migration
    for migration in INITIAL_LONG_FUNCTION_MIGRATION
    if migration not in {
        (
            "kernel/vm_handoff_core.c",
            "micros_vm_handoff_state_validate",
        ),
        (
            "tests/host/vm_handoff_test.c",
            "test_decodes_load_commands",
        ),
        ("kernel/vm_snapshot.c", "micros_vm_snapshot_prepare"),
        (
            "tests/host/vm_bootstrap_test.c",
            "initialize_build_fixture",
        ),
        (
            "tests/host/vm_bootstrap_test.c",
            "test_build_failure_clears_candidate",
        ),
        (
            "kernel/vm_handoff_syscall.c",
            "handle_tty_mapping_command",
        ),
        (
            "kernel/vm_handoff_syscall.c",
            "handle_load_freeze_command",
        ),
        (
            "kernel/vm_handoff_syscall.c",
            "handle_load_abort_command",
        ),
        (
            "kernel/vm_handoff_syscall.c",
            "micros_vm_handoff_handle_captured_user_ecall",
        ),
        (
            "tests/host/vm_handoff_syscall_test.c",
            "reset_fixture",
        ),
        (
            "kernel/address_space_handoff_test.c",
            "arm_handoff_syscall",
        ),
        (
            "kernel/address_space_handoff_test.c",
            "transition_handoff_syscall",
        ),
        (
            "kernel/address_space_handoff_test.c",
            "micros_address_space_handoff_runtime_run_self_test",
        ),
    }
)
STRUCTURAL_TEST_PATH = "tests/host/test_kernel_vm_handoff_source_layout.py"

VM_HANDOFF_CASE_ORDER = (
    "vm_handoff_test_test_decodes_exact_shape",
    "vm_handoff_test_test_decodes_exact_tty_mapping",
    "vm_handoff_test_test_decodes_load_commands",
    "vm_handoff_test_test_prepares_matches_and_commits",
    "vm_handoff_test_test_rejects_invalid_state_without_mutation",
    "vm_handoff_test_test_load_transaction_lifecycle",
    "vm_handoff_test_test_load_abort_before_and_after_freeze",
    "vm_handoff_test_test_load_failures_preserve_state",
    "vm_handoff_test_test_freeze_stack_boundaries",
    "vm_handoff_test_test_rejects_oversized_later_mapping_record",
    "vm_handoff_test_test_classifies_vm_self_fault_phase",
)

VM_BOOTSTRAP_CASE_ORDER = (
    "vm_bootstrap_test_test_abi_contract",
    "vm_bootstrap_test_test_valid_snapshot",
    "vm_bootstrap_test_test_finalize_is_failure_atomic",
    "vm_bootstrap_test_test_rejections",
    "vm_bootstrap_test_test_misaligned_storage",
    "vm_bootstrap_test_test_builds_from_authoritative_state",
    "vm_bootstrap_test_test_build_failure_clears_candidate",
    "micros_vm_handoff_test_run",
)

VM_SYSCALL_CASE_ORDER = (
    "vm_handoff_syscall_test_load_begin_dispatches",
    "vm_handoff_syscall_test_load_begin_failures",
    "vm_handoff_syscall_test_phase_and_authority",
    "vm_handoff_syscall_test_freeze_failure",
    "vm_handoff_syscall_test_invariants_are_fatal",
    "vm_handoff_syscall_test_lifecycle_commit_and_exit",
    "vm_handoff_syscall_test_abort_loading_scratch",
    "vm_handoff_syscall_test_abort_loading_clear",
    "vm_handoff_syscall_test_abort_frozen",
    "vm_handoff_syscall_test_successful_mapping",
    "vm_handoff_syscall_test_unauthorized_mapping",
    "vm_handoff_syscall_test_wrong_phase",
    "vm_handoff_syscall_test_notification_atomicity",
    "vm_handoff_syscall_test_uncleared_console_event",
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
        *SOURCE_SETS["MICROS_VM_HANDOFF_CORE_SOURCES"],
        *SOURCE_SETS["MICROS_VM_HANDOFF_SYSCALL_SOURCES"],
        *SOURCE_SETS["MICROS_VM_SNAPSHOT_SOURCES"],
    )


def symbol_scan_sources() -> tuple[str, ...]:
    return all_source_set_paths()


def accepted_family_paths() -> tuple[str, ...]:
    return tuple(
        dict.fromkeys(
            (
                *all_source_set_paths(),
                *PRIVATE_HEADERS,
                *STABLE_HEADER_HASHES,
                *UNAFFECTED_FAMILY_PATHS,
            )
        )
    )


def discovered_family_paths() -> tuple[str, ...]:
    patterns = (
        "kernel/vm_handoff*.c",
        "kernel/vm_handoff*.h",
        "kernel/vm_snapshot*.c",
        "kernel/vm_snapshot*.h",
        "kernel/address_space_handoff_test*.c",
        "kernel/address_space_handoff_test*.h",
        "tests/host/vm_handoff*.c",
        "tests/host/vm_handoff*.h",
        "tests/host/vm_bootstrap*.c",
        "tests/host/vm_bootstrap*.h",
    )
    return tuple(
        sorted(
            {
                str(path.relative_to(REPOSITORY_ROOT))
                for pattern in patterns
                for path in kernel_layout_source.glob_paths(REPOSITORY_ROOT, pattern)
            }
        )
    )


def resolve_local_include(
    source: Path,
    include: str,
) -> Path | None:
    candidates = (
        source.parent / include,
        REPOSITORY_ROOT / include,
        REPOSITORY_ROOT / "include" / include,
    )
    for candidate in candidates:
        if kernel_layout_source.is_file_path(candidate):
            return candidate.resolve()
    return None


def compiled_source_header_closure() -> tuple[str, ...]:
    pending = [
        (REPOSITORY_ROOT / relative).resolve()
        for relative in accepted_family_paths()
        if (REPOSITORY_ROOT / relative).suffix in {".c", ".h"}
    ]
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
            kernel_layout_source.read_path(path, encoding="utf-8")
        ):
            resolved = resolve_local_include(path, include)
            if (
                resolved is not None
                and resolved.suffix == ".h"
                and REPOSITORY_ROOT in resolved.parents
            ):
                pending.append(resolved)

    return tuple(
        sorted(str(path.relative_to(REPOSITORY_ROOT)) for path in observed)
    )


class KernelVmHandoffSourceLayoutTest(unittest.TestCase):
    def test_expected_sources_and_private_headers_exist(self) -> None:
        missing = [
            path
            for path in (*all_source_set_paths(), *PRIVATE_HEADERS)
            if not kernel_layout_source.is_file_path(REPOSITORY_ROOT / path)
        ]
        self.assertEqual([], missing)

    def test_family_path_inventory_is_exact(self) -> None:
        self.assertEqual(
            tuple(sorted(accepted_family_paths())),
            discovered_family_paths(),
        )

    def test_stable_headers_are_byte_exact(self) -> None:
        observed = {
            path: hashlib.sha256(
                kernel_layout_source.read_bytes_path(REPOSITORY_ROOT / path)
            ).hexdigest()
            for path in STABLE_HEADER_HASHES
        }
        self.assertEqual(STABLE_HEADER_HASHES, observed)

    def test_cmake_source_sets_have_exact_ordered_membership(self) -> None:
        cmake = kernel_layout_source.read_path(CMAKE_PATH, encoding="utf-8")

        for name, expected in SOURCE_SETS.items():
            with self.subTest(source_set=name):
                source_set = cmake_command(cmake, "set", name)
                listed = tuple(
                    re.findall(
                        r"(?:kernel|tests/host)/"
                        r"[A-Za-z0-9_]+\.c",
                        source_set,
                    )
                )
                self.assertEqual(expected, listed)
        for source in all_source_set_paths():
            self.assertEqual(1, cmake.count(source), source)
        self.assertEqual(1, cmake.count("kernel/vm_handoff_runtime.c"))
        self.assertEqual(1, cmake.count("kernel/vm_handoff_test.c"))

    def test_consumers_do_not_list_split_sources_directly(self) -> None:
        cmake = kernel_layout_source.read_path(CMAKE_PATH, encoding="utf-8")
        blocks = (
            cmake_command(cmake, "add_executable", "vm_bootstrap_test"),
            cmake_command(cmake, "add_executable", "pm_test"),
            cmake_command(
                cmake,
                "add_executable",
                "vm_handoff_syscall_test",
            ),
            cmake_command(cmake, "add_executable", "micros"),
            "\n".join(
                cmake_if_blocks(
                    cmake,
                    "MICROS_BUILD_ADDRESS_SPACE_HANDOFF_TEST",
                )
            ),
        )
        direct = []

        for block in blocks:
            for relative in all_source_set_paths():
                if relative in block:
                    direct.append(relative)
        self.assertEqual([], direct)

    def test_consumers_expand_exact_source_sets_once(self) -> None:
        cmake = kernel_layout_source.read_path(CMAKE_PATH, encoding="utf-8")
        consumers = {
            "vm-bootstrap": (
                cmake_command(cmake, "add_executable", "vm_bootstrap_test"),
                (
                    "MICROS_VM_HANDOFF_CORE_SOURCES",
                    "MICROS_VM_HANDOFF_CORE_HOST_TEST_SOURCES",
                    "MICROS_VM_BOOTSTRAP_HOST_TEST_SOURCES",
                ),
            ),
            "pm": (
                cmake_command(cmake, "add_executable", "pm_test"),
                ("MICROS_VM_HANDOFF_CORE_SOURCES",),
            ),
            "syscall": (
                cmake_command(
                    cmake,
                    "add_executable",
                    "vm_handoff_syscall_test",
                ),
                (
                    "MICROS_VM_HANDOFF_CORE_SOURCES",
                    "MICROS_VM_HANDOFF_SYSCALL_SOURCES",
                    "MICROS_VM_HANDOFF_SYSCALL_HOST_TEST_SOURCES",
                ),
            ),
            "micros": (
                cmake_command(cmake, "add_executable", "micros"),
                (
                    "MICROS_VM_HANDOFF_CORE_SOURCES",
                    "MICROS_VM_HANDOFF_SYSCALL_SOURCES",
                    "MICROS_VM_SNAPSHOT_SOURCES",
                ),
            ),
            "address-handoff": (
                "\n".join(
                    cmake_if_blocks(
                        cmake,
                        "MICROS_BUILD_ADDRESS_SPACE_HANDOFF_TEST",
                    )
                ),
                ("MICROS_ADDRESS_SPACE_HANDOFF_QEMU_TEST_SOURCES",),
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

    def test_private_headers_contain_declarations_only(self) -> None:
        definitions = {}
        for relative in PRIVATE_HEADERS:
            path = REPOSITORY_ROOT / relative
            if not kernel_layout_source.is_file_path(path):
                continue
            selected = c_function_definitions(
                kernel_layout_source.read_path(path, encoding="utf-8")
            )
            if selected:
                definitions[relative] = selected
        self.assertEqual({}, definitions)

    def test_sources_do_not_include_c_or_test_private_headers(self) -> None:
        c_include = re.compile(
            r"(?m)^\s*#\s*include\s*[<\"][^>\"]+\.c[>\"]"
        )
        c_violations = []
        private_violations = []

        for relative in (*all_source_set_paths(), *PRIVATE_HEADERS):
            path = REPOSITORY_ROOT / relative
            if not kernel_layout_source.is_file_path(path):
                continue
            text = kernel_layout_source.read_path(path, encoding="utf-8")
            if c_include.search(text):
                c_violations.append(relative)
            if relative in production_sources():
                for private in TEST_PRIVATE_HEADERS:
                    if Path(private).name in text:
                        private_violations.append((relative, private))
        self.assertEqual([], c_violations)
        self.assertEqual([], private_violations)

    def test_symbol_owners_follow_exact_migration_map(self) -> None:
        initial = set(INITIAL_SYMBOL_OWNER_MIGRATION)
        migration = set(SYMBOL_OWNER_MIGRATION)
        expected_current = {}
        definitions = {}

        self.assertEqual(92, len(INITIAL_SYMBOL_OWNER_MIGRATION))
        self.assertEqual(len(migration), len(SYMBOL_OWNER_MIGRATION))
        self.assertTrue(migration <= initial)
        for current, final, symbol in migration:
            self.assertEqual(FINAL_SYMBOL_OWNERS[symbol], final)
            expected_current[symbol] = current

        for relative in symbol_scan_sources():
            path = REPOSITORY_ROOT / relative
            if not kernel_layout_source.is_file_path(path):
                continue
            for symbol in c_function_definitions(
                kernel_layout_source.read_path(path, encoding="utf-8")
            ):
                definitions.setdefault(symbol, []).append(relative)

        for symbol, final in FINAL_SYMBOL_OWNERS.items():
            with self.subTest(symbol=symbol):
                self.assertEqual(
                    [expected_current.get(symbol, final)],
                    definitions.get(symbol, []),
                )

    def test_mutable_context_and_snapshot_owners_are_unique(self) -> None:
        context_definitions = []
        generic_context = re.compile(
            r"(?m)^\s*static\s+struct\s+"
            r"([A-Za-z_][A-Za-z0-9_]*test_context)\b"
        )
        for relative in compiled_source_header_closure():
            path = REPOSITORY_ROOT / relative
            if not kernel_layout_source.is_file_path(path):
                continue
            for context_type in generic_context.findall(
                kernel_layout_source.read_path(path, encoding="utf-8")
            ):
                context_definitions.append((relative, context_type))
        self.assertEqual(
            sorted(
                (
                    (
                        "kernel/address_space_handoff_test_fixture.c",
                        "micros_address_space_handoff_test_context",
                    ),
                    (
                        "tests/host/vm_bootstrap_test_fixture.c",
                        "vm_bootstrap_test_context",
                    ),
                    (
                        "tests/host/vm_handoff_syscall_test_fixture.c",
                        "vm_handoff_syscall_test_context",
                    ),
                )
            ),
            sorted(context_definitions),
        )

        snapshot_pattern = re.compile(
            r"(?ms)^_Alignas\(MICROS_VM_BOOT_INFO_ALIGNMENT\)\s*"
            r"static\s+struct\s+micros_vm_boot_info\s+snapshot\s*;"
        )
        snapshot_owners = []
        for relative in SOURCE_SETS["MICROS_VM_SNAPSHOT_SOURCES"]:
            path = REPOSITORY_ROOT / relative
            if (
                kernel_layout_source.is_file_path(path)
                and snapshot_pattern.search(kernel_layout_source.read_path(path, encoding="utf-8"))
            ):
                snapshot_owners.append(relative)
        self.assertEqual(["kernel/vm_snapshot.c"], snapshot_owners)

        representative_state = (
            (
                SOURCE_SETS[
                    "MICROS_VM_HANDOFF_SYSCALL_HOST_TEST_SOURCES"
                ],
                ("objects", "bootstrap", "vm_state", "user_memory"),
            ),
            (
                SOURCE_SETS[
                    "MICROS_ADDRESS_SPACE_HANDOFF_QEMU_TEST_SOURCES"
                ],
                (
                    "ownership_snapshot",
                    "processes",
                    "syscall_scripts",
                    "failure_stage",
                ),
            ),
        )
        for sources, names in representative_state:
            for name in names:
                self.assertLessEqual(
                    len(static_definition_paths(sources, name)),
                    1,
                    name,
                )

    def test_evidence_identities_and_case_order_remain_stable(self) -> None:
        cmake = kernel_layout_source.read_path(CMAKE_PATH, encoding="utf-8")
        core_entry = kernel_layout_source.read_path(REPOSITORY_ROOT / "tests/host/vm_handoff_test.c", encoding="utf-8")
        bootstrap_entry = kernel_layout_source.read_path(REPOSITORY_ROOT / "tests/host/vm_bootstrap_test.c", encoding="utf-8")
        syscall_entry = kernel_layout_source.read_path(REPOSITORY_ROOT / "tests/host/vm_handoff_syscall_test.c", encoding="utf-8")
        markers = {
            (
                "MICROS_VM_HANDOFF_TEST_PASS "
                "snapshot=validated ownership=handed-off vm=wired "
                "readiness=acknowledged authority=vm"
            ): 4,
            (
                "MICROS_ADDRESS_SPACE_HANDOFF_TEST_PASS "
                "phase=handed-off wired=validated ipc=resident "
                "grants=atomic mutation=revoked"
            ): 1,
            (
                "MICROS_VM_SCRATCH_PREEMPTION_TEST_PASS "
                "map=real timer=preempted roots=distinct "
                "inactive-validation=accepted reactivation=fenced "
                "sentinel=preserved commit=real "
                "stale-translation=faulted pte=absent "
                "ownership=transferable scratch=clear"
            ): 1,
        }

        self.assertEqual(1, cmake.count("NAME vm-bootstrap"))
        self.assertEqual(1, cmake.count("NAME vm-handoff-syscall"))
        for target in (
            "test-qemu-vm-handoff",
            "test-qemu-address-space-handoff",
            "test-qemu-vm-scratch-preemption",
            "test-qemu-vm-ready-early",
            "test-qemu-vm-self-fault",
            "test-qemu-vm-self-fault-sealed",
        ):
            cmake_command(cmake, "add_custom_target", target)
        for marker, expected_count in markers.items():
            self.assertEqual(expected_count, cmake.count(marker), marker)

        for entry, anchor, use_last, order in (
            (
                core_entry,
                "bool micros_vm_handoff_test_run",
                True,
                VM_HANDOFF_CASE_ORDER,
            ),
            (
                bootstrap_entry,
                "int main",
                False,
                VM_BOOTSTRAP_CASE_ORDER,
            ),
        ):
            offset = entry.rindex(anchor) if use_last else entry.index(anchor)
            body = entry[offset:]
            positions = [body.index(name) for name in order]
            self.assertEqual(sorted(positions), positions)
        syscall_body = syscall_entry[syscall_entry.index("int main"):]
        positions = [
            syscall_body.index(name)
            for name in VM_SYSCALL_CASE_ORDER[:-1]
        ]
        self.assertEqual(sorted(positions), positions)
        self.assertEqual(
            4,
            syscall_body.count(VM_SYSCALL_CASE_ORDER[-1]),
        )

    def test_validation_plan_inherits_path_classification(self) -> None:
        baselines = {
            baseline
            for baseline, _ in VM_PATH_CLASSIFICATION_EQUIVALENTS
        }
        observed = tuple(
            item
            for item in kernel_layout_source.original_path_classification_equivalents(validation_plan.PATH_CLASSIFICATION_EQUIVALENTS)
            if item[0] in baselines
        )
        self.assertEqual(VM_PATH_CLASSIFICATION_EQUIVALENTS, observed)
        for baseline, equivalents in VM_PATH_CLASSIFICATION_EQUIVALENTS:
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
            kernel_layout_source.original_host_unit_only_inputs(validation_plan.HOST_UNIT_ONLY_INPUTS),
        )

    def test_long_function_migration_matches_review_signal(self) -> None:
        initial = set(INITIAL_LONG_FUNCTION_MIGRATION)
        migration = set(LONG_FUNCTION_MIGRATION)
        oversized = {}

        self.assertEqual(13, len(INITIAL_LONG_FUNCTION_MIGRATION))
        self.assertEqual(len(migration), len(LONG_FUNCTION_MIGRATION))
        self.assertTrue(migration <= initial)
        for relative in compiled_source_header_closure():
            path = REPOSITORY_ROOT / relative
            if not kernel_layout_source.is_file_path(path):
                continue
            for name, line_count in c_function_definitions(
                kernel_layout_source.read_path(path, encoding="utf-8")
            ).items():
                if line_count > FUNCTION_REVIEW_LINE_LIMIT:
                    oversized[(relative, name)] = line_count
        self.assertEqual(migration, set(oversized), oversized)


if __name__ == "__main__":
    unittest.main()

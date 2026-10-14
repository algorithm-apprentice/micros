from __future__ import annotations

import ast
import hashlib
import json
from pathlib import Path
import re
import unittest
from unittest.mock import patch

from tests.host import test_vm_service_source_layout as vm_layout
from tests.host.test_frame_ownership_source_layout import (
    c_function_definitions,
    c_lexical_text,
    cmake_command,
)
from tests.host.test_kernel_pm_control_source_layout import macro_definition_text
from tests.host.test_pm_service_source_layout import (
    cmake_commands,
    static_local_storage,
)
from tests.host.test_vm_service_source_layout import (
    c_declaration_text,
    file_scope_objects,
    python_function_definitions,
)
from tools import validation_plan


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
REVIEWED_BASE = "49706a8ef8ef2296c42dc1ecadef0df375d2ee2f"
FUNCTION_REVIEW_LINE_LIMIT = 120
GENERATOR_PATH = "tools/generate_bootstrap_fixture.py"
STRUCTURAL_TEST_PATH = "tests/host/test_kernel_bootstrap_source_layout.py"

SOURCE_SETS = {
    "MICROS_BOOTSTRAP_MANIFEST_SOURCES": (
        "kernel/bootstrap_manifest.c",
        "kernel/bootstrap_manifest_common.c",
        "kernel/bootstrap_manifest_profiles.c",
        "kernel/bootstrap_transitions.c",
    ),
    "MICROS_BOOTSTRAP_IMAGE_SOURCES": (
        "kernel/bootstrap_image.c",
    ),
    "MICROS_BOOTSTRAP_RUNTIME_SOURCES": (
        "kernel/bootstrap_runtime.c",
        "kernel/bootstrap_runtime_state.c",
        "kernel/bootstrap_preparation.c",
        "kernel/bootstrap_preparation_image.c",
        "kernel/bootstrap_preparation_rollback.c",
        "kernel/bootstrap_runtime_validation.c",
        "kernel/bootstrap_diagnostics.c",
    ),
    "MICROS_BOOTSTRAP_SYSCALL_SOURCES": (
        "kernel/bootstrap_syscall.c",
        "kernel/bootstrap_syscall_common.c",
        "kernel/bootstrap_syscall_release.c",
        "kernel/bootstrap_syscall_ready.c",
        "kernel/bootstrap_syscall_complete.c",
        "kernel/bootstrap_syscall_failure.c",
    ),
    "MICROS_BOOT_FOUNDATION_SOURCES": (
        "kernel/main.c",
        "kernel/boot_foundation.c",
        "kernel/boot_component_tests.c",
    ),
    "MICROS_BOOTSTRAP_LAUNCHER_SOURCES": (
        "tests/qemu/bootstrap_launcher.c",
        "tests/qemu/bootstrap_launcher_common.c",
        "tests/qemu/bootstrap_launcher_manifest.c",
        "tests/qemu/bootstrap_launcher_protocol.c",
        "tests/qemu/bootstrap_launcher_control.c",
    ),
    "MICROS_BOOTSTRAP_MANIFEST_HOST_TEST_SOURCES": (
        "tests/host/bootstrap_manifest_test.c",
        "tests/host/bootstrap_manifest_fixture.c",
        "tests/host/bootstrap_manifest_acceptance_test.c",
        "tests/host/bootstrap_manifest_topology_test.c",
        "tests/host/bootstrap_manifest_tty_test.c",
        "tests/host/bootstrap_runtime_test.c",
        "tests/host/bootstrap_runtime_model.c",
        "tests/host/bootstrap_runtime_reference.c",
    ),
    "MICROS_BOOTSTRAP_CONTROL_HOST_TEST_SOURCES": (
        "tests/host/bootstrap_control_test.c",
        "tests/host/bootstrap_control_fixture.c",
        "tests/host/bootstrap_control_abi_test.c",
        "tests/host/bootstrap_control_lifecycle_test.c",
        "tests/host/bootstrap_control_completion_test.c",
        "tests/host/bootstrap_control_image_catalog_test.c",
    ),
    "MICROS_FDT_HOST_TEST_SOURCES": (
        "tests/host/fdt_test.c",
        "tests/host/fdt_test_fixture.c",
        "tests/host/fdt_header_test.c",
        "tests/host/fdt_memory_test.c",
    ),
}

FAMILY_HEADERS = {
    "MICROS_BOOTSTRAP_MANIFEST_SOURCES": (
        "kernel/bootstrap_manifest_internal.h",
    ),
    "MICROS_BOOTSTRAP_IMAGE_SOURCES": (),
    "MICROS_BOOTSTRAP_RUNTIME_SOURCES": (
        "kernel/bootstrap_runtime_internal.h",
    ),
    "MICROS_BOOTSTRAP_SYSCALL_SOURCES": (
        "kernel/bootstrap_syscall_internal.h",
    ),
    "MICROS_BOOT_FOUNDATION_SOURCES": ("kernel/boot_internal.h",),
    "MICROS_BOOTSTRAP_LAUNCHER_SOURCES": (
        "tests/qemu/bootstrap_launcher_internal.h",
    ),
    "MICROS_BOOTSTRAP_MANIFEST_HOST_TEST_SOURCES": (
        "tests/host/bootstrap_manifest_test_internal.h",
        "tests/host/bootstrap_runtime_model_internal.h",
    ),
    "MICROS_BOOTSTRAP_CONTROL_HOST_TEST_SOURCES": (
        "tests/host/bootstrap_control_test_internal.h",
    ),
    "MICROS_FDT_HOST_TEST_SOURCES": ("tests/host/fdt_test_internal.h",),
}
PRIVATE_HEADERS = tuple(path for paths in FAMILY_HEADERS.values() for path in paths)

# These are reviewed-base digests, not expectations read from the worktree.
STABLE_HASHES = {
    "include/micros/bootstrap.h":
        "01e21d32aa44e3f349ac5794161395c812c6349bf177bf91909a2cb9bd1fb274",
    "include/micros/bootstrap_control.h":
        "1b7ee0e14f67ad9c1db089920fdcce3d27eb834aeb427ba0080649a5d606b195",
    "include/micros/fdt.h":
        "69ba9d20cbd4912df63803b9ecac0d473e75776cc04a53b0b80b958b3d88b1f9",
    "kernel/bootstrap_runtime.h":
        "11767b3c99703c134496b5a112d39fec094414099fec9d9675184334876a0466",
    "kernel/bootstrap_image.h":
        "fa1ee26941435025177a7315667d61c7229e1546ed8243b2675c905bcc9ec774",
    "kernel/bootstrap_syscall.h":
        "1f668a0b400611523360cc1290c7540f7de6d1c5eb2d6f76a3fc33d1aa24d20a",
    "kernel/bootstrap_control_internal.h":
        "d2601f07bbc41301a1e98086aa910ac1625bdaed670202f911d6aa9c30bf0633",
    "kernel/bootstrap_test.h":
        "9719fca4f1dc806b4938e318f1e2f9226170d61524131da4ac438eac213ea199",
    "kernel/bootstrap_test_fixture.h":
        "868213f391d310d5a8c0a0e174e10c0867ddce1c1e99d221702b8305152540fb",
    "tests/qemu/bootstrap_protocol.h":
        "c53dca361419db0e4d7a27090220ec4a9070bb622deafbeede5bba89e5542a1a",
    "tests/qemu/bootstrap_launcher_control.h":
        "bc7345493dbfad033b855eb36fb5f6b5c25b0bc74f10aba894795336a421ef9b",
    "kernel/bootstrap_control.c":
        "3f64ff0ef5f4959bfcc66822aebd13410b1aa4ca3d3efab2bab6395871cabc52",
    "kernel/bootstrap_control_core.c":
        "9791498b0e93d3ead9f6d41b0c874ebc302e28152c4d58af9eaacfe4f2355d3a",
}
EXISTING_INLINE_HEADERS = {
    "arch/riscv64/interrupt.h":
        "596e1f799fc629a2184dadb09f8fa7d41207ba04afbc6378040c57e1c8c1f1f5",
    "kernel/bootstrap_control_internal.h":
        STABLE_HASHES["kernel/bootstrap_control_internal.h"],
}
RETAINED_NEIGHBORS = (
    "include/micros/bootstrap_memory.h",
    "kernel/bootstrap_memory.c",
    "kernel/bootstrap_test.c",
)

MANIFEST_CASE_ORDER = (
    "test_manifest_contract",
    "test_manifest_validation",
    "test_manifest_rejections",
    "test_vm_mapping_capacity",
    "test_three_service_topology",
    "test_tty_service_contract",
    "test_runtime_transitions",
    "test_retained_runtime_deadline",
    "test_replayable_runtime_model",
)
CONTROL_CASE_ORDER = (
    "test_valid_commands",
    "test_malformed_commands",
    "test_binding_lookup_rejects_oversized_count",
    "test_control_transitions",
    "test_pm_completion_rejections_are_atomic",
    "test_completion_rejects_launcher_ipc_dependencies",
    "test_pm_completion_queues_sealed_event_once",
    "test_pm_completion_wakes_only_any_receiver",
    "test_pm_completion_preserves_specific_receiver",
    "test_image_catalog",
)
FDT_CASES = (
    ("parses memory and reservations", "test_parses_memory_and_reservations"),
    ("rejects null arguments", "test_rejects_null_arguments"),
    ("accepts nonzero property padding", "test_accepts_nonzero_property_padding"),
    ("rejects truncated header", "test_rejects_truncated_header"),
    ("rejects bad magic", "test_rejects_bad_magic"),
    ("rejects incompatible version", "test_rejects_incompatible_version"),
    ("rejects total size beyond buffer", "test_rejects_total_size_beyond_buffer"),
    ("rejects overlapping blocks", "test_rejects_overlapping_blocks"),
    ("rejects unterminated reservation map", "test_rejects_unterminated_reservation_map"),
    ("rejects unknown structure token", "test_rejects_unknown_structure_token"),
    ("rejects bad property name offset", "test_rejects_bad_property_name_offset"),
    ("rejects oversized property name", "test_rejects_oversized_property_name"),
    ("rejects unsupported root cells", "test_rejects_unsupported_root_cells"),
    ("rejects partial reg tuple", "test_rejects_partial_reg_tuple"),
    ("rejects conflicting memory device_type", "test_rejects_memory_name_with_conflicting_type"),
    ("skips disabled named memory", "test_skips_disabled_named_memory"),
    ("skips disabled device_type memory", "test_skips_disabled_device_type_memory"),
    ("rejects range overflow", "test_rejects_range_overflow"),
    ("rejects memory capacity overflow", "test_rejects_memory_range_capacity_overflow"),
    ("disabled memory preserves capacity", "test_disabled_memory_does_not_consume_capacity"),
    ("rejects excessive depth", "test_rejects_excessive_tree_depth"),
    ("rejects reserved-memory cell mismatch", "test_rejects_reserved_memory_cell_mismatch"),
    ("rejects nonempty reserved-memory ranges", "test_rejects_nonempty_reserved_memory_ranges"),
    ("rejects reserved-memory as usable memory", "test_rejects_reserved_memory_as_usable_memory"),
    ("rejects duplicate device_type", "test_rejects_duplicate_device_type"),
    ("rejects dynamic reserved memory", "test_rejects_dynamic_reserved_memory"),
    ("requires memory range", "test_requires_at_least_one_memory_range"),
)

FINAL_OWNER_GROUPS = {
    "kernel/bootstrap_transitions.c": (
        "micros_bootstrap_runtime_initialize",
        "micros_bootstrap_runtime_release",
        "micros_bootstrap_runtime_release_retaining_deadline",
        "micros_bootstrap_runtime_accept_ready",
        "micros_bootstrap_runtime_complete",
        "micros_bootstrap_classify_ready",
    ),
    "kernel/bootstrap_runtime_state.c": (
        "micros_bootstrap_runtime_state",
        "micros_bootstrap_runtime_authoritative_state",
        "micros_bootstrap_runtime_is_active_controller",
    ),
    "kernel/bootstrap_runtime_validation.c": (
        "micros_bootstrap_runtime_validate",
        "micros_bootstrap_runtime_validate_vm_prepared",
    ),
    "kernel/bootstrap_diagnostics.c": (
        "micros_bootstrap_runtime_record_failure",
        "micros_bootstrap_runtime_fail",
        "micros_bootstrap_runtime_check_deadline",
    ),
    "tests/host/bootstrap_manifest_acceptance_test.c": (
        "test_manifest_contract", "test_manifest_validation", "test_manifest_rejections",
    ),
    "tests/host/bootstrap_manifest_topology_test.c": (
        "test_vm_mapping_capacity", "test_three_service_topology",
    ),
    "tests/host/bootstrap_manifest_tty_test.c": ("test_tty_service_contract",),
    "tests/host/bootstrap_runtime_test.c": (
        "test_runtime_transitions", "test_retained_runtime_deadline",
    ),
    "tests/host/bootstrap_runtime_model.c": ("test_replayable_runtime_model",),
    "tests/host/bootstrap_control_abi_test.c": (
        "test_valid_commands", "test_malformed_commands",
    ),
    "tests/host/bootstrap_control_lifecycle_test.c": (
        "test_binding_lookup_rejects_oversized_count", "test_control_transitions",
    ),
    "tests/host/bootstrap_control_completion_test.c": (
        "test_pm_completion_rejections_are_atomic",
        "test_completion_rejects_launcher_ipc_dependencies",
        "test_pm_completion_queues_sealed_event_once",
        "test_pm_completion_wakes_only_any_receiver",
        "test_pm_completion_preserves_specific_receiver",
    ),
    "tests/host/bootstrap_control_image_catalog_test.c": ("test_image_catalog",),
    "tests/host/fdt_header_test.c": (
        "test_rejects_null_arguments", "test_accepts_nonzero_property_padding",
        "test_rejects_truncated_header", "test_rejects_bad_magic",
        "test_rejects_incompatible_version", "test_rejects_total_size_beyond_buffer",
        "test_rejects_overlapping_blocks", "test_rejects_unterminated_reservation_map",
        "test_rejects_unknown_structure_token", "test_rejects_bad_property_name_offset",
        "test_rejects_oversized_property_name", "test_rejects_excessive_tree_depth",
    ),
    "tests/host/fdt_memory_test.c": (
        "test_parses_memory_and_reservations", "test_rejects_unsupported_root_cells",
        "test_rejects_partial_reg_tuple", "test_rejects_memory_name_with_conflicting_type",
        "test_skips_disabled_named_memory", "test_skips_disabled_device_type_memory",
        "test_rejects_range_overflow", "test_rejects_memory_range_capacity_overflow",
        "test_disabled_memory_does_not_consume_capacity",
        "test_rejects_reserved_memory_cell_mismatch",
        "test_rejects_nonempty_reserved_memory_ranges",
        "test_rejects_reserved_memory_as_usable_memory", "test_rejects_duplicate_device_type",
        "test_rejects_dynamic_reserved_memory", "test_requires_at_least_one_memory_range",
    ),
}
FINAL_SYMBOL_OWNERS = {
    name: owner for owner, names in FINAL_OWNER_GROUPS.items() for name in names
}


def original_owner(final_owner: str) -> str:
    return next(paths[0] for paths in SOURCE_SETS.values() if final_owner in paths)


INITIAL_SYMBOL_OWNER_MIGRATION = tuple(
    (original_owner(owner), owner, name)
    for owner, names in FINAL_OWNER_GROUPS.items() for name in names
)
SYMBOL_OWNER_MIGRATION = ()

# Physical lengths, rename/format-independent body fingerprints, and the
# Accepted semantic phase outlines all come from REVIEWED_BASE.
LONG_FUNCTION_REQUIREMENTS = {
    ("kernel/bootstrap_runtime.c", "prepare_services"): (
        176, "da156298052e75cc28c2376828fe6681c7887d3167e380929910dd1aafbb9551",
        "order setup; record resources/binding; configuration/thread pass; root/owner validation",
    ),
    ("kernel/bootstrap_runtime.c", "micros_bootstrap_runtime_prepare"): (
        227, "1b081da695ae5bd66fe13b226f8f290f74099ba60a9854a3baf627db3676c162",
        "argument/reset; immutable preflight; mechanisms/baseline; services; roles; scheduler/control; unwind/IRQ result",
    ),
    ("kernel/bootstrap_manifest.c", "stable_service_profiles_match"): (
        134, "2e4c5daa35f32525808869a8b4c03e184b9635e59773d9e15df982ad90882b2c",
        "core-service profiles; I/O-service profiles; inert application profile",
    ),
    ("kernel/bootstrap_manifest.c", "validate_profile_relationships"): (
        263, "edf02fc4863243fde8c2703e95482b635314ac1b859e25051106cfc338e0a8e9",
        "controller; target closure; role/profile identities; standard profiles/operations; entry relationships",
    ),
    ("kernel/bootstrap_manifest.c", "micros_bootstrap_manifest_validate_detailed"): (
        503, "2b2e2000a0bda6f9ed52e8dc9860aea72185edf41de8be49eda09305938b3639",
        "argument/header/table; ordered entries; aggregate/role constraints; topology; success-only publication",
    ),
    ("kernel/bootstrap_syscall.c", "handle_console_begin"): (
        215, "530508422677770c659ce5bdc1d6f418094afb77f27b6d6af0c1c6062ad4ddac",
        "identity/phase; VM/TTY/owner validity; mapping/deadline/events; prevalidated commit; invariant",
    ),
    ("kernel/bootstrap_syscall.c", "handle_release"): (
        127, "e3e9771473debb1cdcd189bd5bee085b84eaf1fc9d10b692084958729200bfc1",
        "binding/role; ordinary/console deadline; expected/fatal results; runtime validation",
    ),
    ("kernel/bootstrap_syscall.c", "handle_accept_ready"): (
        194, "65b8ef0439f3a4209de4cfcc8588f8f84a290e2b5f52ee29ff870890c78b543c",
        "role/ready plan; context/guard; exact reply/rollback; ready/TTY/no-message commit; captured result",
    ),
    ("kernel/bootstrap_syscall.c", "micros_bootstrap_handle_captured_user_ecall"): (
        146, "c3e4fcfcc653b6db4d9a0cd26c33db5448b22ed232da8e869e2d2f39c8127bd6",
        "argument/decode; authority/runtime/grants; exact command dispatch",
    ),
    ("kernel/bootstrap_image.c", "validate_image"): (
        178, "419c1da1b706a099c39d97fcac49b491c633165d8a8e515cf630f50598eaa397",
        "scalar/config/optional VM object; ordered segments/containment/zero bytes; image info",
    ),
    ("kernel/main.c", "kernel_main"): (
        436, "4e16128ade62deddc13343d61a1db288b8c6ec853bf8590bf4cfc3da0d6c4952",
        "objects/traps; FDT; memory/owners/MMU/PLIC; ordered component dispatch; shutdown",
    ),
    ("tests/qemu/bootstrap_launcher.c", "validate_manifest"): (
        210, "f1ada0a903f98497b5720a044383bb09eb78797a300bec35cb6fa6dbb4f5ad6b",
        "header/tail; entry/identity/role/budget; independent topology; complete selection",
    ),
    ("tests/qemu/bootstrap_launcher.c", "micros_service_main"): (
        229, "11a2a29d4825b2153e064ccc37fc06763e1b66dd053daed5c947097479a97a98",
        "startup/view/config; local states; serial console/release/readiness/accept; complete fallback",
    ),
    (GENERATOR_PATH, "_validated_descriptor"): (
        129, "3cfe93dfba39cc59662b68c05411ddc10ea974a5a966dcceee3c26bde51f3d67",
        "ELF/symbols/loads; exact config region; optional VM region; descriptor publication",
    ),
    ("tests/host/bootstrap_manifest_test.c", "test_tty_service_contract"): (
        400, "65ba313cc1c3ef466dfe98091a3d4df5bc6b1a178fcac680b84b41c71e5bee72",
        "inputs; standard profiles; accepted plan; role/target/config assertions",
    ),
    ("tests/host/bootstrap_manifest_test.c", "test_manifest_validation"): (
        229, "b0ad055181738eed3a1e21aca566488cb54494dd681acb860675b508a3548b93",
        "fixture; baseline acceptance; plan assertions; diagnostic/config assertions",
    ),
    ("tests/host/bootstrap_manifest_test.c", "test_manifest_rejections"): (
        261, "b9ab4b64f1e909e3ca5006c4f81ff5ff4e75660e836993769ac5103cc88d7dba",
        "ordered header/entry mutations; profile/image/resource mutations; atomic outputs",
    ),
    ("tests/host/bootstrap_manifest_test.c", "test_three_service_topology"): (
        554, "18c0d77eacd1c787c2426f6c0fcc54ef3460c0e13a8ffc86ed966c9aab33355b",
        "profiles/inputs; topology/prerequisites; exact configurations",
    ),
    ("tests/host/bootstrap_manifest_test.c", "test_replayable_runtime_model"): (
        250, "27f22a970af394d52459453b420d2500c8d2cf60b943cfe6beaf58f7c0736a62",
        "reset/schedule; production/reference dispatch; randomized classification; trace/complete comparison",
    ),
    ("tests/host/bootstrap_control_test.c", "test_malformed_commands"): (
        141, "b16ae8c2c61d392f47dd7b07995f38629454b215322fc3691619056ae9d149da",
        "register mutations; command/detail mutations; unchanged outputs",
    ),
    ("tests/host/bootstrap_control_test.c", "setup_control_fixture"): (
        174, "848436c33e295a8353bb6b3c26cc1f50fb046bb26cdb072a74651e4aa4bb3abf",
        "clear; manifest/profiles/plan; objects/endpoints/threads; bindings; control state",
    ),
    ("tests/host/bootstrap_control_test.c", "test_control_transitions"): (
        315, "861821d4267f6a39659eeaab4b148edea4a2823be3df2aa4a93822636285c78f",
        "setup; release/ready; atomic denials; complete/seal assertions",
    ),
    ("tests/host/bootstrap_control_test.c", "test_pm_completion_rejections_are_atomic"): (
        135, "e9e2c986fe28739b76fff39ff768323b43b272fe38dee61589a2d9a0cb2ce3d1",
        "valid baseline; ordered rejection mutations; complete sentinel/state preservation",
    ),
    ("tests/host/bootstrap_control_test.c", "test_completion_rejects_launcher_ipc_dependencies"): (
        142, "0bffb907454c8108027791cc50b269565f2ea8e3bb73a21358758061ddcd3009",
        "dependency setup; exact rejection; complete preservation",
    ),
    ("tests/host/bootstrap_control_test.c", "test_pm_completion_wakes_only_any_receiver"): (
        127, "7c25d73a9327d59e34c12117bed4e3bf83256576538b56212175b5657151a577",
        "receivers; seal plan/commit; exact wake/unrelated receiver assertions",
    ),
    ("tests/host/bootstrap_control_test.c", "test_image_catalog"): (
        166, "52f79dceea955af6e654d095877bfdf8a9f09897e9908f9d96562a1809fea5dd",
        "static pages/catalog; accepted metadata; malformed catalog/output preservation",
    ),
    ("tests/host/fdt_test.c", "build_fixture"): (
        268, "c48bdae52d14a3368957e61e142c6dad2f0ee9fe9383bb67d8d6b4cfbb0aa187",
        "reservation map; root/memory nodes; reserved-memory subtree; strings/header/offsets",
    ),
}
INITIAL_LONG_FUNCTION_MIGRATION = tuple(LONG_FUNCTION_REQUIREMENTS)
LONG_FUNCTION_MIGRATION = ()

RETAINED_OWNER_GROUPS = {
    "kernel/bootstrap_manifest.c": (
        "micros_bootstrap_manifest_validate", "micros_bootstrap_manifest_validate_detailed",
    ),
    "kernel/bootstrap_image.c": ("micros_bootstrap_image_catalog_validate",),
    "kernel/bootstrap_runtime.c": (
        "micros_bootstrap_runtime_prepare", "micros_bootstrap_runtime_publish_controller",
        "micros_bootstrap_runtime_launch",
    ),
    "kernel/bootstrap_syscall.c": ("micros_bootstrap_handle_captured_user_ecall",),
    "kernel/main.c": ("kernel_main",),
    "tests/qemu/bootstrap_launcher.c": ("micros_service_main",),
    "tests/qemu/bootstrap_launcher_control.c": ("micros_bootstrap_launcher_console_begin",),
    "tests/host/bootstrap_manifest_test.c": ("main",),
    "tests/host/bootstrap_control_test.c": ("main",),
    "tests/host/fdt_test.c": ("main",),
}

# Local names may stay local. Crossing a private interface uses the family's
# prefix; either spelling has the same single semantic owner.
HELPER_GROUPS = (
    ("kernel/bootstrap_manifest.c", "kernel/bootstrap_manifest_common.c", "bootstrap_manifest_", (
        "copy_bytes", "clear_bytes", "bytes_are_zero", "pointer_is_aligned",
        "fixed_name_is_canonical", "fixed_names_equal", "fixed_name_matches_literal",
    )),
    ("kernel/bootstrap_manifest.c", "kernel/bootstrap_manifest_profiles.c", "bootstrap_manifest_", (
        "find_profile", "profile_matches", "stable_service_profiles_match",
        "validate_profile_table", "validate_profile_relationships",
    )),
    ("kernel/bootstrap_manifest.c", "kernel/bootstrap_manifest.c", "bootstrap_manifest_", (
        "find_expected", "find_image", "validate_header", "validate_entry_shape", "validate_image_bound",
    )),
    ("kernel/bootstrap_manifest.c", "kernel/bootstrap_transitions.c", "bootstrap_transition_", (
        "runtime_entry", "runtime_entry_const", "runtime_release",
    )),
    ("kernel/bootstrap_runtime.c", "kernel/bootstrap_runtime_state.c", "bootstrap_runtime_", (
        "set_preparation_diagnostic", "clear_bytes", "copy_bytes", "bytes_equal", "panic_runtime",
    )),
    ("kernel/bootstrap_runtime.c", "kernel/bootstrap_diagnostics.c", "bootstrap_diagnostics_", (
        "diagnostic_reason_name", "diagnostic_phase_name", "diagnostic_state_name",
    )),
    ("kernel/bootstrap_runtime.c", "kernel/bootstrap_preparation.c", "bootstrap_preparation_", (
        "find_image", "find_policy", "validate_policies", "prepare_threads",
        "prepare_services", "prepare_vm_snapshot", "prepare_pm_control", "prepare_tty_handoff",
    )),
    ("kernel/bootstrap_runtime.c", "kernel/bootstrap_preparation_image.c", "bootstrap_preparation_", (
        "segment_permissions", "allocate_image", "verify_image", "allocate_stack", "verify_stack",
        "allocate_manifest_view", "write_user_range", "read_user_range", "user_range_matches",
        "build_service_table", "patch_configurations",
    )),
    ("kernel/bootstrap_runtime.c", "kernel/bootstrap_preparation_rollback.c", "bootstrap_preparation_", (
        "release_page", "rollback_record", "rollback_preparation",
    )),
    ("kernel/bootstrap_syscall.c", "kernel/bootstrap_syscall_common.c", "bootstrap_syscall_", (
        "panic_bootstrap_syscall", "fail_active_bootstrap", "return_result",
        "map_bootstrap_error", "map_ipc_error", "controller_is_authorized", "role_gate_ready",
    )),
    ("kernel/bootstrap_syscall.c", "kernel/bootstrap_syscall_failure.c", "bootstrap_syscall_", (
        "map_failure_reason", "failure_detail", "validate_failure_request",
    )),
    ("kernel/bootstrap_syscall.c", "kernel/bootstrap_syscall_release.c", "bootstrap_syscall_", (
        "find_runtime_entry", "service_is_ready", "no_bootstrap_deadline_is_armed",
        "console_event_state_is_clear", "kernel_notification_destination_is_clear",
        "handle_console_begin", "handle_release",
    )),
    ("kernel/bootstrap_syscall.c", "kernel/bootstrap_syscall_ready.c", "bootstrap_syscall_", (
        "handle_accept_ready",
    )),
    ("kernel/bootstrap_syscall.c", "kernel/bootstrap_syscall_complete.c", "bootstrap_syscall_", (
        "handle_complete",
    )),
    ("kernel/bootstrap_image.c", "kernel/bootstrap_image.c", "bootstrap_image_", (
        "pointer_is_aligned", "clear_bytes", "range_bytes_are_zero", "ranges_overlap", "validate_image",
    )),
    ("kernel/main.c", "kernel/boot_foundation.c", "boot_", (
        "write_range", "write_fdt_counts", "stop_after_reset_failure",
    )),
    ("tests/qemu/bootstrap_launcher.c", "tests/qemu/bootstrap_launcher_common.c", "bootstrap_launcher_", (
        "read_u32_le", "read_u64_le", "bytes_are_zero", "name_is_canonical", "names_equal",
        "find_config_endpoint", "find_manifest_service",
    )),
    ("tests/qemu/bootstrap_launcher.c", "tests/qemu/bootstrap_launcher_manifest.c", "bootstrap_launcher_", (
        "validate_manifest", "validate_configuration",
    )),
    ("tests/qemu/bootstrap_launcher.c", "tests/qemu/bootstrap_launcher_protocol.c", "bootstrap_launcher_", (
        "console_mapped_notification_is_canonical", "message_shape_detail",
    )),
    ("tests/qemu/bootstrap_launcher.c", "tests/qemu/bootstrap_launcher_control.c", "bootstrap_launcher_", (
        "bootstrap_control", "fail",
    )),
    ("tests/host/bootstrap_manifest_test.c", "tests/host/bootstrap_manifest_fixture.c", "bootstrap_manifest_test_", (
        "set_name", "profile", "initialize_manifest", "initialize_validation_fixture", "expect_validation_error",
    )),
    ("tests/host/bootstrap_manifest_test.c", "tests/host/bootstrap_runtime_model.c", "bootstrap_runtime_model_", (
        "model_random", "print_model_failure",
    )),
    ("tests/host/bootstrap_manifest_test.c", "tests/host/bootstrap_runtime_reference.c", "bootstrap_runtime_", (
        "reference_is_zero", "reference_entry", "reference_entry_const",
        "reference_initialize", "reference_release", "reference_accept_ready",
        "reference_complete", "reference_classify_ready", "project_reference",
    )),
    ("tests/host/bootstrap_control_test.c", "tests/host/bootstrap_control_fixture.c", "bootstrap_control_test_", (
        "set_name", "profile", "initialize_manifest", "context_pattern", "setup_control_fixture",
        "release_and_accept_ready", "setup_ready_pm_fixture", "commit_complete_for_test",
        "commit_current_complete_for_test", "read_u32_le", "write_u32_le", "read_u64_le",
        "sealed_notification_matches",
    )),
    ("tests/host/bootstrap_control_test.c", "tests/host/bootstrap_control_abi_test.c", "bootstrap_control_test_", (
        "decode_preserves_output_on_error", "failure_detail_result",
    )),
    ("tests/host/fdt_test.c", "tests/host/fdt_test_fixture.c", "fdt_test_", (
        "fail_builder", "write_be32", "write_be64", "append_bytes", "append_u32",
        "align_structure", "begin_node", "end_node", "append_property",
        "append_u32_property", "append_range_cells", "default_options", "build_fixture",
    )),
)
HELPER_MIGRATION_ANCHORS = {
    "kernel/bootstrap_manifest_common.c":
        ("kernel/bootstrap_manifest.c", "micros_bootstrap_manifest_validate_detailed"),
    "kernel/bootstrap_manifest_profiles.c":
        ("kernel/bootstrap_manifest.c", "micros_bootstrap_manifest_validate_detailed"),
    "kernel/bootstrap_transitions.c":
        ("kernel/bootstrap_manifest.c", "micros_bootstrap_runtime_initialize"),
    "kernel/bootstrap_runtime_state.c":
        ("kernel/bootstrap_runtime.c", "micros_bootstrap_runtime_state"),
    "kernel/bootstrap_diagnostics.c":
        ("kernel/bootstrap_runtime.c", "micros_bootstrap_runtime_record_failure"),
    "kernel/bootstrap_preparation.c": ("kernel/bootstrap_runtime.c", "prepare_services"),
    "kernel/bootstrap_preparation_image.c": ("kernel/bootstrap_runtime.c", "prepare_services"),
    "kernel/bootstrap_preparation_rollback.c":
        ("kernel/bootstrap_runtime.c", "micros_bootstrap_runtime_prepare"),
    **{
        f"kernel/bootstrap_syscall_{part}.c":
            ("kernel/bootstrap_syscall.c", "micros_bootstrap_handle_captured_user_ecall")
        for part in ("common", "release", "ready", "complete", "failure")
    },
    "kernel/boot_foundation.c": ("kernel/main.c", "kernel_main"),
    **{
        f"tests/qemu/bootstrap_launcher_{part}.c":
            ("tests/qemu/bootstrap_launcher.c", "micros_service_main")
        for part in ("common", "manifest", "protocol", "control")
    },
    "tests/host/bootstrap_manifest_fixture.c":
        ("tests/host/bootstrap_manifest_test.c", "test_manifest_validation"),
    "tests/host/bootstrap_manifest_acceptance_test.c":
        ("tests/host/bootstrap_manifest_test.c", "test_manifest_rejections"),
    "tests/host/bootstrap_runtime_model.c":
        ("tests/host/bootstrap_manifest_test.c", "test_replayable_runtime_model"),
    "tests/host/bootstrap_runtime_reference.c":
        ("tests/host/bootstrap_manifest_test.c", "test_replayable_runtime_model"),
    "tests/host/bootstrap_control_fixture.c":
        ("tests/host/bootstrap_control_test.c", "setup_control_fixture"),
    "tests/host/bootstrap_control_abi_test.c":
        ("tests/host/bootstrap_control_test.c", "test_malformed_commands"),
    "tests/host/fdt_test_fixture.c": ("tests/host/fdt_test.c", "build_fixture"),
}

RUNTIME_FIELDS = (
    "struct micros_bootstrap_control_state bootstrap_state;",
    "struct micros_bootstrap_manifest_plan validation_plan;",
    "struct micros_bootstrap_image_info image_infos[MICROS_BOOTSTRAP_SERVICE_CAPACITY];",
    "struct micros_bootstrap_binding prepared_bindings[MICROS_BOOTSTRAP_SERVICE_CAPACITY];",
    "struct preparation_record preparation_records[MICROS_BOOTSTRAP_SERVICE_CAPACITY];",
    "uint16_t preparation_order[MICROS_BOOTSTRAP_SERVICE_CAPACITY];",
    "struct preparation_baseline preparation_baseline;",
    "struct micros_bootstrap_diagnostic preparation_diagnostic;",
    "struct micros_vm_snapshot_result prepared_vm_snapshot;",
    "bool failure_record_emitted;",
)
LAUNCHER_OBJECTS = (
    "volatile struct micros_bootstrap_service_config micros_bootstrap_service_config;",
    "const volatile uint64_t micros_bootstrap_launcher_rodata = UINT64_C(0x4c41554e43484552);",
    "volatile uint64_t micros_bootstrap_launcher_data = UINT64_C(0x4253544c41554e43);",
)
TYPE_LAYOUT_HASHES = {
    "preparation_record": "0ef5ed0095fc266a1ec1d17f3c939b7b2f698f18deef8f3c4f06765a247a9e6d",
    "preparation_baseline": "4612fcb968fb12e88a0d6fe106635ac3e6b56515d8b4f0a4f2bb03b20f2be71a",
    "property_location": "61fcf56b85b3f310715d4661846fbcc07ba8f6da204f8b9dc0dbc6454a22aae2",
    "fdt_fixture": "a81acd3b07135ab1a187433648c02caf7af34fd30e256b2cf659f64696ac0c9c",
    "fixture_options": "4194db221562b66e5fc2e9c6ef53d3b2a59b2f4e7fb6524c3cc752af74b4d102",
    "structure_builder": "3149b4dad8b1ce56e73083be35d8073c44a22624abf476fdc2f8a55f02528ee9",
    "reference_runtime_entry": "b4187c645d6802359cfc3877115d4ea2376fddb49dc944551e5876548ee5a886",
    "reference_runtime": "85006e3d9ea88f7cb35a600c9f5be4b9b07f4b867da239a034fcf38327ea0073",
    "model_trace_entry": "a27da1ed6c0dafb005eff5ff2705fee47ed1eed48f554117f5d36badafe44bd1",
    "model_operation": "da855d20243b8930048657ab43cea3c5e41ae0da92414733d08045eae6da495f",
    "launcher_service_state": "a12ed51b4b894cad1d6254f3cc94e5726ee6cb3d5f57aec59d0f3db7063be424",
}
NATIVE_MAIN_HASHES = {
    "tests/host/bootstrap_manifest_test.c":
        "e633e1a4eb3bc37dd0d16407213912586c02f72a26c5b47e67b45cd611627881",
    "tests/host/bootstrap_control_test.c":
        "763c8a42fc1845f51a645dc5d0e21806318075b767dfa73d57bf1ce9401ddeec",
    "tests/host/fdt_test.c":
        "a9e2c8b2ae79594f5c79c78952899b05504b9e35c9a217d6b48fda8f932f191d",
}
PRESERVED_HELPER_BODY_HASHES = {
    ("tests/host/bootstrap_manifest_test.c", "model_random"):
        "4c44155fa802300f30b1353c1339cd0c064cf318936ee950346d6b29c48d1a4e",
    ("tests/host/bootstrap_manifest_test.c", "print_model_failure"):
        "8ca66087429576aa8c6b423b8c17a48f0d96c7f987e9ef0f36ecf75b06414880",
    ("tests/host/bootstrap_manifest_test.c", "reference_is_zero"):
        "eb6bbaafb6a6683b356658ada5ff2c0c5c09489eb14f501265d1bbd028dc0f24",
    ("tests/host/bootstrap_manifest_test.c", "reference_entry"):
        "be7e3b0efe8af43d2c72724b0cebca8890e77cd5ebd1c34e0fef7474f636eb89",
    ("tests/host/bootstrap_manifest_test.c", "reference_entry_const"):
        "be7e3b0efe8af43d2c72724b0cebca8890e77cd5ebd1c34e0fef7474f636eb89",
    ("tests/host/bootstrap_manifest_test.c", "reference_initialize"):
        "b625fb2219e2d7e9dd9bf315ccde07ef010a14c619694d279e84b6df6f714875",
    ("tests/host/bootstrap_manifest_test.c", "reference_release"):
        "8466e6220acb13b8fd4a16577fd4793f213cd16e8b246d8a73e39201a7a7ae66",
    ("tests/host/bootstrap_manifest_test.c", "reference_accept_ready"):
        "ad1a95b1aea312de87050d7b8a2e11030885c2ff2d5b704759e5b8568be78ca5",
    ("tests/host/bootstrap_manifest_test.c", "reference_complete"):
        "d4d8ad09edbc4288070677d93cd2f7a97400fffff855fc93f978243a28309620",
    ("tests/host/bootstrap_manifest_test.c", "reference_classify_ready"):
        "fb6aa95c47021f58a59a4dce582bdac9995ed1554b3942d62759592b2809e1ca",
    ("tests/host/bootstrap_manifest_test.c", "project_reference"):
        "41d8d3efc5e527b26c3558a6fd837f4b5d0a8775419b572ef50d8b9a549f6a43",
    ("tests/qemu/bootstrap_launcher.c", "message_shape_detail"):
        "216de1c66c4b858151c2b2ebf0bdc82fc87e88f7813177756d828d24034c4d68",
    ("tests/qemu/bootstrap_launcher.c", "console_mapped_notification_is_canonical"):
        "5283f6e0f9b815e3ed3285a2522b7590ce6e7052f206bc58219c55ea74fed09d",
    ("tests/qemu/bootstrap_launcher.c", "bootstrap_control"):
        "50991f45e403fc41a30bfe3b6ba5aeea6421e8bf092795ad7cb79125d169df82",
    ("tests/qemu/bootstrap_launcher.c", "fail"):
        "944af7e14e93dd68a371557a7e1ab50fc0bb57481b1e9ab787856d8fe5778975",
}

GENERATOR_BODY_HASHES = {
    "_format_bytes": "63b813ed21c6102b23105adf9a68b5d65f74b9665eecf621cad2a0cf1cb864fe",
    "_render_segment_bytes": "8866f5e75a387c83e8e1adfdb15453728a93afea78ee8b735d445e37782a5de1",
    "_render_image": "35aa62dc2a53240cb8d8aae5785d0bf96074be7e2d2f9105f931539714e52d61",
    "render_fixture": "95977198f70a9c9f8ec8009bcb8a0e2797072e34f5c60df5aee6adb36c4f5845",
    "parse_arguments": "ac6dece6887016dd88030d3d8c1bd3e906f37ca78863609d935b9777e7594e33",
    "main": "3cc13a7418630dcf51faf450816df7ac9b462225a9d6855b1f6f25fe95da0dae",
}
GENERATOR_SIGNATURES = {
    "_format_bytes": (("data",), ()),
    "_validated_descriptor": (("image", "prefix"), ()),
    "_render_segment_bytes": (("descriptor",), ()),
    "_render_image": (("descriptor", "image_id", "indent"), ("    ",)),
    "render_fixture": (("launcher_image", "probe_image"), ()),
    "parse_arguments": (("argv",), (None,)),
    "main": (("argv",), (None,)),
}
GENERATOR_IMPORTS = {
    "tools/generate_vm_handoff_fixture.py": (3, 3, 3),
    "tools/generate_pm_service_fixture.py": (4, 1, 4),
    "tools/generate_vm_scratch_preemption_fixture.py": (4, 1, 4),
    "tools/generate_tty_service_fixture.py": (5, 1, 5),
    "tools/generate_ramfs_service_fixture.py": (6, 1, 6),
    "tools/generate_vfs_service_fixture.py": (7, 1, 1),
}

QEMU_WORKFLOWS = (
    "test-qemu-smoke", "test-qemu-panic", "test-qemu-trap", "test-qemu-timer",
    "test-qemu-uart-console", "test-qemu-frame-allocator", "test-qemu-trap-panic",
    "test-qemu-mmu", "test-qemu-object-model", "test-qemu-endpoint", "test-qemu-grant",
    "test-qemu-grant-syscall", "test-qemu-user-runtime", "test-qemu-bootstrap-launcher",
    "test-qemu-vm-handoff", "test-qemu-pm-service", "test-qemu-vm-scratch-preemption",
    "test-qemu-tty", "test-qemu-ramfs", "test-qemu-vfs", "test-qemu-vm-ready-early",
    "test-qemu-vm-self-fault", "test-qemu-vm-self-fault-sealed",
    "test-qemu-bootstrap-ready-timeout", "test-qemu-bootstrap-manifest-panic",
    "test-qemu-ipc", "test-qemu-ipc-ecall-core", "test-qemu-ipc-syscall",
    "test-qemu-ipc-syscall-panic", "test-qemu-nested-trap", "test-qemu-frame-ownership",
    "test-qemu-user-address-space", "test-qemu-address-space-handoff",
    "test-qemu-user-execution", "test-qemu-scheduler",
    "test-qemu-scheduler-invalid-outgoing", "test-qemu-scheduler-invalid-next",
)
CROSS_GATE_WORKFLOWS = (
    "test-qemu-uart-console", "test-qemu-object-model", "test-qemu-endpoint",
    "test-qemu-grant", "test-qemu-grant-syscall", "test-qemu-user-runtime",
    "test-qemu-bootstrap-launcher", "test-qemu-vm-handoff", "test-qemu-pm-service",
    "test-qemu-vm-scratch-preemption", "test-qemu-tty", "test-qemu-ramfs", "test-qemu-vfs",
    "test-qemu-vm-ready-early", "test-qemu-vm-self-fault", "test-qemu-vm-self-fault-sealed",
    "test-qemu-bootstrap-ready-timeout", "test-qemu-bootstrap-manifest-panic",
    "test-qemu-ipc", "test-qemu-ipc-ecall-core", "test-qemu-ipc-syscall",
    "test-qemu-ipc-syscall-panic", "test-qemu-frame-ownership",
    "test-qemu-user-address-space", "test-qemu-address-space-handoff",
    "test-qemu-user-execution", "test-qemu-scheduler",
)
LAUNCHER_WORKFLOWS = (
    "test-qemu-bootstrap-launcher", "test-qemu-vm-handoff", "test-qemu-pm-service",
    "test-qemu-vm-scratch-preemption", "test-qemu-tty", "test-qemu-ramfs", "test-qemu-vfs",
    "test-qemu-vm-ready-early", "test-qemu-vm-self-fault", "test-qemu-vm-self-fault-sealed",
    "test-qemu-bootstrap-ready-timeout", "test-qemu-bootstrap-manifest-panic",
)
QEMU_TARGET_HASHES = dict(zip(LAUNCHER_WORKFLOWS, (
    "c9ae167f43d2f1e5626a350b1dff6335c1e85be734cd5195c73da90126d5239d",
    "0582ecd5f53324fd0d95f21ea4694d8aef2c15b1fd860e832dbdf8cff4cae11a",
    "47f334f1094e1f3e0d523665244fe0cd423e281ffe1db5d58abfe7791e219ee9",
    "2db5f5287440d21ce9ddff8a840ab7fdfb7987bbff97ecec49831a4009b0526d",
    "e749cfc5b07cbd28fffc897c4d2ef90e83582d4ccdc0942278749ef07ae583ca",
    "f8ebbc4f698709571f37a2d0c47f86db4a6940392e3bc561eab2b5aa21c9f94a",
    "a6d9f9383a2cc56210d64375e13c75bb57713c9d7478c8810d09b09a3a43d609",
    "50f96b3916c9b29ab9271ac2b5efd16610bd543af449ec07ff68cdf1129129bc",
    "fbc76bafb24a02a343e7ae3117a47f9e7cdbd945c9723cd5c11e5f708f377fc1",
    "ab50360a071250981aa9af68a4a323d719ae0b649707b4b2740d5d004ebbecd8",
    "3196b8412e6175819a9ae0c0c94e73e189907401bcb2dc888324134b474c5beb",
    "74b94bd38247d08e3e8d8ab84df5e84e22a9fe929778ba72efac7eabf133c7ec",
)))

BOOT_BRANCHES = (
    ("MICROS_BUILD_UART_CONSOLE_TEST", ("micros_uart_console_runtime_run_self_test",), ()),
    ("MICROS_BUILD_PANIC_TEST", (), ("intentional-test",)),
    ("MICROS_BUILD_TRAP_TEST", ("micros_trap_run_self_test",), (
        "trap-test-restore",
        "MICROS_TRAP_TEST_PASS origin=S cause=illegal-instruction registers=preserved "
        "hart-context=routed primary-stack=selected sscratch=anchor\n",
    )),
    ("MICROS_BUILD_TRAP_PANIC_TEST", ("micros_trap_run_panic_test",), ()),
    ("MICROS_BUILD_TIMER_TEST", ("micros_timer_run_self_test", "micros_timer_ticks"), (
        "timer-test-failed", "MICROS_TIMER_TEST_PASS ticks=", " interval=", " active=",
        " deadline=", " owner=hart", "\n",
    )),
    ("MICROS_BUILD_FRAME_ALLOCATOR_TEST", ("micros_bootstrap_memory_run_self_test",), (
        "frame-allocator-test",
        "MICROS_FRAME_ALLOCATOR_TEST_PASS allocations=0x0000000000000004 "
        "reuse=lowest invariants=preserved\n",
    )),
    ("MICROS_BUILD_MMU_TEST", (
        "micros_kernel_address_space_run_self_test", "micros_plic_runtime_run_self_test",
    ), (
        "mmu-test-failed",
        "MICROS_MMU_TEST_PASS store-fault=text execute-fault=writable traps=0x0000000000000002\n",
        "MICROS_TTY_CONTROLLER_TEST_PASS mapping=exact priority=disabled enable=disabled "
        "context=supervisor seie=independent\n",
    )),
    ("MICROS_BUILD_FRAME_OWNERSHIP_TEST", ("micros_frame_ownership_runtime_run_self_test",), (
        "frame-ownership-test",
        "MICROS_FRAME_OWNERSHIP_TEST_PASS stale=rejected release=blocked handoff=atomic invariants=preserved\n",
    )),
    ("MICROS_BUILD_USER_ADDRESS_SPACE_TEST", ("micros_user_address_space_runtime_run_self_test",), (
        "user-address-space-test",
        "MICROS_USER_ADDRESS_SPACE_TEST_PASS roots=isolated reuse=zeroed active=guarded "
        "ownership=validated sum=cleared\n",
    )),
    ("MICROS_BUILD_ADDRESS_SPACE_HANDOFF_TEST", ("micros_address_space_handoff_runtime_run_self_test",), (
        "address-space-handoff-test",
        "MICROS_ADDRESS_SPACE_HANDOFF_TEST_PASS phase=handed-off wired=validated ipc=resident "
        "grants=atomic mutation=revoked\n",
        "MICROS_GRANT_SYSCALL_HANDOFF_PASS phase=handed-off operations=create,revoke,copy-from,copy-to "
        "errors=stable registers=preserved\n",
    )),
    ("MICROS_BUILD_USER_EXECUTION_TEST", ("micros_user_execution_runtime_run_self_test",), (
        "user-execution-test-returned",
    )),
    ("MICROS_BUILD_SCHEDULER_TEST", ("micros_scheduler_runtime_run_self_test",), ("scheduler-test-returned",)),
    ("MICROS_BUILD_IPC_ECALL_CORE_TEST", ("micros_ipc_ecall_core_runtime_run_self_test",), (
        "ipc-ecall-core-test-returned",
    )),
    ("MICROS_BUILD_IPC_SYSCALL_TEST", ("micros_ipc_syscall_runtime_run_self_test",), (
        "ipc-syscall-test-returned",
    )),
    ("MICROS_BUILD_IPC_SYSCALL_PANIC_TEST", ("micros_ipc_syscall_panic_runtime_run_self_test",), (
        "ipc-syscall-panic-test-returned",
    )),
    ("MICROS_BUILD_GRANT_SYSCALL_TEST", ("micros_grant_syscall_runtime_run_self_test",), (
        "grant-syscall-test-returned",
    )),
    ("MICROS_BUILD_USER_RUNTIME_TEST", ("micros_user_runtime_runtime_run_self_test",), (
        "user-runtime-test-returned",
    )),
    ("defined(MICROS_BUILD_BOOTSTRAP_LAUNCHER_TEST)||defined(MICROS_BUILD_BOOTSTRAP_READY_TIMEOUT_TEST)"
     "||defined(MICROS_BUILD_BOOTSTRAP_MANIFEST_PANIC_TEST)",
     ("micros_bootstrap_test_launch",), ("bootstrap-test-returned",)),
    ("defined(MICROS_BUILD_VM_HANDOFF_TEST)||defined(MICROS_BUILD_VM_READY_EARLY_TEST)"
     "||defined(MICROS_BUILD_VM_SELF_FAULT_TEST)||defined(MICROS_BUILD_VM_SELF_FAULT_SEALED_TEST)",
     ("micros_vm_handoff_test_launch",), ("vm-handoff-test-returned",)),
    ("MICROS_BUILD_PM_SERVICE_TEST", ("micros_pm_service_test_launch",), ("pm-service-test-returned",)),
    ("MICROS_BUILD_VM_SCRATCH_PREEMPTION_TEST", ("micros_vm_scratch_preemption_test_launch",), (
        "vm-scratch-preemption-test-returned",
    )),
    ("MICROS_BUILD_TTY_SERVICE_TEST", ("micros_tty_service_test_launch",), ("tty-service-test-returned",)),
    ("MICROS_BUILD_RAMFS_SERVICE_TEST", ("micros_ramfs_service_test_launch",), ("ramfs-service-test-returned",)),
    ("MICROS_BUILD_VFS_SERVICE_TEST", ("micros_vfs_service_test_launch",), ("vfs-service-test-returned",)),
    ("defined(MICROS_BUILD_SCHEDULER_INVALID_OUTGOING_TEST)||defined(MICROS_BUILD_SCHEDULER_INVALID_NEXT_TEST)",
     ("micros_scheduler_invalid_runtime_run_self_test",), ("scheduler-invalid-test-returned",)),
    ("MICROS_BUILD_OBJECT_MODEL_TEST", ("micros_kernel_object_runtime_run_self_test",), (
        "object-model-test",
        "MICROS_OBJECT_MODEL_TEST_PASS process-generation=advanced stale=rejected "
        "thread-limit=enforced hart-local=preserved\n",
    )),
    ("MICROS_BUILD_ENDPOINT_TEST", ("micros_endpoint_runtime_run_self_test",), (
        "endpoint-test",
        "MICROS_ENDPOINT_TEST_PASS generation=validated profiles=immutable visibility=staged "
        "authorization=separate grants=generation-safe\n",
    )),
    ("MICROS_BUILD_GRANT_TEST", ("micros_grant_runtime_run_self_test",), (
        "grant-test",
        "MICROS_GRANT_TEST_PASS identity=generation-safe directions=checked bounds=validated "
        "copies=atomic phase=bootstrap cleanup=complete\n",
    )),
    ("MICROS_BUILD_IPC_TEST", ("micros_ipc_runtime_run_self_test",), (
        "ipc-test",
        "MICROS_IPC_ADDRESS_SPACES count=three roots=preserved contexts=preserved stacks=preserved "
        "scheduler=preserved messages=preserved\n",
        "MICROS_IPC_TEST_PASS endpoints=generation-safe queues=blocking calls=tokenized "
        "notifications=coalesced kernel-events=injected deadlock=rejected\n",
    )),
    ("MICROS_BUILD_NESTED_TRAP_TEST", (
        "micros_kernel_object_runtime_set_test_emergency_stack", "micros_nested_trap_test_trigger",
    ), ("nested-trap-test-stack", "nested-trap-test-returned")),
)

MANIFEST_COMMON = "kernel/bootstrap_manifest_common.c"
MANIFEST_PROFILES = "kernel/bootstrap_manifest_profiles.c"
TRANSITIONS = "kernel/bootstrap_transitions.c"
RUNTIME_STATE = "kernel/bootstrap_runtime_state.c"
PREPARATION = "kernel/bootstrap_preparation.c"
PREPARATION_IMAGE = "kernel/bootstrap_preparation_image.c"
ROLLBACK = "kernel/bootstrap_preparation_rollback.c"
RUNTIME_VALIDATION = "kernel/bootstrap_runtime_validation.c"
DIAGNOSTICS = "kernel/bootstrap_diagnostics.c"
SYSCALL_COMMON = "kernel/bootstrap_syscall_common.c"
SYSCALL_COMMANDS = SOURCE_SETS["MICROS_BOOTSTRAP_SYSCALL_SOURCES"][2:]
LAUNCHER_COMMON = "tests/qemu/bootstrap_launcher_common.c"
MODEL = "tests/host/bootstrap_runtime_model.c"
REFERENCE = "tests/host/bootstrap_runtime_reference.c"

PERMITTED_OWNER_DEPENDENCIES = {
    MANIFEST_COMMON: (),
    MANIFEST_PROFILES: (MANIFEST_COMMON,),
    TRANSITIONS: (MANIFEST_COMMON,),
    "kernel/bootstrap_manifest.c": (MANIFEST_COMMON, MANIFEST_PROFILES),
    "kernel/bootstrap_image.c": (),
    RUNTIME_STATE: (),
    PREPARATION_IMAGE: (RUNTIME_STATE,),
    ROLLBACK: (RUNTIME_STATE,),
    RUNTIME_VALIDATION: (RUNTIME_STATE, PREPARATION_IMAGE),
    DIAGNOSTICS: (RUNTIME_STATE,),
    PREPARATION: (RUNTIME_STATE, PREPARATION_IMAGE, ROLLBACK,
                  "kernel/bootstrap_manifest.c", "kernel/bootstrap_image.c"),
    "kernel/bootstrap_runtime.c": (
        RUNTIME_STATE, PREPARATION, PREPARATION_IMAGE, ROLLBACK, RUNTIME_VALIDATION,
        DIAGNOSTICS, "kernel/bootstrap_manifest.c", "kernel/bootstrap_image.c", TRANSITIONS,
    ),
    SYSCALL_COMMON: (RUNTIME_STATE, RUNTIME_VALIDATION, DIAGNOSTICS),
    **{path: (SYSCALL_COMMON, RUNTIME_STATE, RUNTIME_VALIDATION, DIAGNOSTICS)
       for path in SYSCALL_COMMANDS},
    "kernel/bootstrap_syscall.c": (
        SYSCALL_COMMON, *SYSCALL_COMMANDS, RUNTIME_STATE, RUNTIME_VALIDATION, DIAGNOSTICS,
    ),
    "kernel/main.c": ("kernel/boot_foundation.c", "kernel/boot_component_tests.c"),
    "kernel/boot_foundation.c": (),
    "kernel/boot_component_tests.c": ("kernel/boot_foundation.c",),
    LAUNCHER_COMMON: (),
    "tests/qemu/bootstrap_launcher_manifest.c": (LAUNCHER_COMMON,),
    "tests/qemu/bootstrap_launcher_protocol.c": (
        LAUNCHER_COMMON, "tests/qemu/bootstrap_launcher_control.c",
    ),
    "tests/qemu/bootstrap_launcher_control.c": (),
    "tests/qemu/bootstrap_launcher.c": SOURCE_SETS["MICROS_BOOTSTRAP_LAUNCHER_SOURCES"][1:],
    "tests/host/bootstrap_manifest_fixture.c": ("kernel/bootstrap_manifest.c",),
    REFERENCE: (),
    MODEL: ("tests/host/bootstrap_manifest_fixture.c", REFERENCE, TRANSITIONS),
}
for family, fixture, production in (
    ("MICROS_BOOTSTRAP_MANIFEST_HOST_TEST_SOURCES", "tests/host/bootstrap_manifest_fixture.c",
     ("kernel/bootstrap_manifest.c", TRANSITIONS)),
    ("MICROS_BOOTSTRAP_CONTROL_HOST_TEST_SOURCES", "tests/host/bootstrap_control_fixture.c",
     ("kernel/bootstrap_manifest.c", TRANSITIONS, "kernel/bootstrap_image.c")),
    ("MICROS_FDT_HOST_TEST_SOURCES", "tests/host/fdt_test_fixture.c", ()),
):
    paths = SOURCE_SETS[family]
    PERMITTED_OWNER_DEPENDENCIES[paths[0]] = (*paths[1:], *production)
    for path in paths[1:]:
        PERMITTED_OWNER_DEPENDENCIES.setdefault(
            path, production if path == fixture else (fixture, *production),
        )


def read_source(relative: str) -> str:
    return (REPOSITORY_ROOT / relative).read_text(encoding="utf-8")


def compact(text: str) -> str:
    return "".join(text.split())


def compact_hash(text: str) -> str:
    return hashlib.sha256(compact(text).encode()).hexdigest()


def all_sources() -> tuple[str, ...]:
    return tuple(path for paths in SOURCE_SETS.values() for path in paths)


def accepted_family_paths() -> set[str]:
    return {*all_sources(), *PRIVATE_HEADERS, *STABLE_HASHES, *RETAINED_NEIGHBORS, GENERATOR_PATH}


def discovered_family_paths() -> set[str]:
    patterns = (
        "kernel/bootstrap*.[ch]", "kernel/boot_*.[ch]", "kernel/main.c",
        "include/micros/bootstrap*.h", "include/micros/fdt.h",
        "tests/host/bootstrap*.[ch]", "tests/host/fdt*.[ch]",
        "tests/qemu/bootstrap_launcher*.[ch]", "tests/qemu/bootstrap_protocol.h",
        GENERATOR_PATH,
    )
    return {
        str(path.relative_to(REPOSITORY_ROOT))
        for pattern in patterns for path in REPOSITORY_ROOT.glob(pattern)
    }


def local_include_closure(relative: str) -> tuple[str, ...]:
    pending = [relative]
    observed = set()
    while pending:
        for dependency in vm_layout.local_include_closure(pending.pop()):
            if dependency in observed:
                continue
            observed.add(dependency)
            for include in re.findall(r"(?m)^\s*#\s*include\s*<([^>]+)>", read_source(dependency)):
                resolved = vm_layout.resolve_local_include(REPOSITORY_ROOT / dependency, include)
                if resolved is not None and REPOSITORY_ROOT in resolved.parents:
                    pending.append(str(resolved.relative_to(REPOSITORY_ROOT)))
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


def c_function_body(text: str, name: str) -> str:
    lexical = c_lexical_text(text)
    match = re.search(rf"\b{re.escape(name)}\s*\([^;{{}}]*\)\s*\{{", lexical)
    if match is None:
        raise AssertionError(f"missing function body: {name}")
    start = match.end() - 1
    depth = 0
    for token in re.finditer(r"[{}]", lexical[start:]):
        depth += 1 if token.group() == "{" else -1
        if depth == 0:
            return text[start:start + token.end()]
    raise AssertionError(f"unterminated function body: {name}")


def body_fingerprint(body: str) -> str:
    keywords = set(
        "auto break case char const continue default do double else enum extern float for "
        "goto if inline int long register restrict return short signed sizeof static struct "
        "switch typedef union unsigned void volatile while _Alignas _Alignof _Atomic _Bool "
        "_Complex _Generic _Imaginary _Noreturn _Static_assert _Thread_local".split()
    )
    identifiers = {}
    normalized = []
    for token in re.findall(r"[A-Za-z_][A-Za-z0-9_]*|[0-9]+|[^\s]", c_lexical_text(body)):
        if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", token) and token not in keywords:
            token = identifiers.setdefault(token, f"id{len(identifiers)}")
        normalized.append(token)
    return hashlib.sha256(" ".join(normalized).encode()).hexdigest()


def python_body_hash(node: ast.FunctionDef) -> str:
    body = ast.Module(body=node.body, type_ignores=[])
    return hashlib.sha256(ast.dump(body, include_attributes=False).encode()).hexdigest()


def helper_specs() -> tuple[tuple[str, str, str, str], ...]:
    return tuple(
        (current, final, name, prefix + name)
        for current, final, prefix, names in HELPER_GROUPS for name in names
    )


def canonical_helper_text(text: str) -> str:
    for _, _, name, qualified in helper_specs():
        text = re.sub(rf"\b{re.escape(qualified)}\b", name, text)
    return text


def logical_owner(path: str, name: str) -> str:
    if name in FINAL_SYMBOL_OWNERS:
        return FINAL_SYMBOL_OWNERS[name]
    for current, final, local, qualified in helper_specs():
        if (path == current and name == local) or (path == final and name in (local, qualified)):
            return final
    return path


def has_function_reference(text: str, name: str) -> bool:
    if name.startswith(("micros_", "bootstrap_", "boot_", "fdt_test_", "test_")):
        return re.search(rf"\b{re.escape(name)}\b", text) is not None
    if re.search(rf"\b{re.escape(name)}\s*\(", text):
        return True
    local_object = re.search(
        rf"\b(?:const\s+)?(?:struct\s+[A-Za-z_][A-Za-z0-9_]*|[A-Za-z_][A-Za-z0-9_]*)"
        rf"\s+[*\s]*\b{re.escape(name)}\s*(?:;|=|\[)", text,
    )
    if local_object is not None:
        return False
    return re.search(
        rf"(?<!&)&(?!&)\s*{re.escape(name)}\b(?!\s*(?:->|\.))"
        rf"|^\s*#\s*define\b[^\n]*\b{re.escape(name)}\b",
        text, re.MULTILINE,
    ) is not None


def is_immutable(declaration: str) -> bool:
    return re.match(r"(?:static\s+)?const\b|static\s+_Alignas\([^)]*\)\s+const\b", declaration) is not None


def scalar_context(declaration: str) -> tuple[str, str]:
    match = re.fullmatch(
        r"static\s+struct\s+([A-Za-z_][A-Za-z0-9_]*)\s+([A-Za-z_][A-Za-z0-9_]*)\s*;",
        declaration,
    )
    if match is None:
        raise AssertionError(f"expected one scalar, static-lifetime context: {declaration}")
    return match.group(1), match.group(2)


def type_bodies(text: str, name: str) -> tuple[str, ...]:
    return tuple(re.findall(
        rf"\b(?:struct|enum)\s+{re.escape(name)}\s*\{{([^{{}}]*)\}}",
        c_declaration_text(text),
    ))


def string_records(text: str) -> tuple[str, ...]:
    string = r'"(?:\\.|[^"\\])*"'
    return tuple(
        "".join(ast.literal_eval(literal) for literal in re.findall(string, group))
        for group in re.findall(string + rf"(?:\s*{string})*", text)
    )


def branch_records(text: str) -> tuple[tuple[str, tuple[str, ...], tuple[str, ...]], ...]:
    joined = re.sub(r"\\\n\s*", " ", text)
    return tuple(
        (
            compact(match.group(2)),
            tuple(re.findall(r"\b(micros_[A-Za-z0-9_]+)\s*\(", c_lexical_text(match.group(3)))),
            string_records(match.group(3)),
        )
        for match in re.finditer(
            r"(?ms)^\s*#\s*(ifdef|if)\s+([^\n]+)\n(.*?)^\s*#\s*endif\b", joined,
        )
    )


class KernelBootstrapSourceLayoutTest(unittest.TestCase):
    maxDiff = None

    @classmethod
    def setUpClass(cls) -> None:
        cls.texts = {
            path: read_source(path) for path in all_sources()
            if (REPOSITORY_ROOT / path).is_file()
        }
        cls.definitions = {
            path: c_function_definitions(text) for path, text in cls.texts.items()
        }

    def find_helper(self, current: str, local: str) -> tuple[str, str]:
        candidates = []
        for old, final, name, qualified in helper_specs():
            if (old, name) != (current, local):
                continue
            for path, spelling in {(old, name), (final, name), (final, qualified)}:
                if spelling in self.definitions.get(path, {}):
                    candidates.append((path, spelling))
        self.assertEqual(1, len(candidates), (current, local, candidates))
        return candidates[0]

    def helper_body(self, current: str, local: str) -> str:
        path, name = self.find_helper(current, local)
        return canonical_helper_text(c_function_body(self.texts[path], name))

    def symbol_owner(self, name: str) -> str:
        current = {symbol: owner for owner, _, symbol in SYMBOL_OWNER_MIGRATION}
        return current.get(name, FINAL_SYMBOL_OWNERS[name])

    def expanded_calls(
        self, path: str, name: str, paths: tuple[str, ...], leaves: set[str],
        active: tuple[tuple[str, str], ...] = (),
    ) -> tuple[str, ...]:
        self.assertNotIn((path, name), active, "phase helpers must not recurse")
        calls = []
        body = c_lexical_text(c_function_body(self.texts[path], name))
        for called in re.findall(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(", body):
            canonical = canonical_helper_text(called)
            providers = [
                (source, called) for source in paths
                if called in self.definitions.get(source, {})
            ]
            if (path, called) in providers:
                providers = [(path, called)]
            if providers and canonical not in leaves:
                self.assertEqual(1, len(providers), (path, called, providers))
                calls.extend(self.expanded_calls(
                    *providers[0], paths, leaves, (*active, (path, name)),
                ))
            else:
                calls.append(canonical)
        return tuple(calls)

    def test_accepted_nine_families_and_private_headers_exist_exactly(self) -> None:
        self.assertEqual(9, len(SOURCE_SETS))
        self.assertEqual(44, len(all_sources()))
        self.assertEqual(44, len(set(all_sources())))
        self.assertEqual(9, len(PRIVATE_HEADERS))
        self.assertEqual(70, len(accepted_family_paths()))
        self.assertEqual(
            [],
            sorted(path for path in accepted_family_paths()
                   if not (REPOSITORY_ROOT / path).is_file()),
            "ADR-0060 requires the complete source/header skeleton before extraction",
        )
        self.assertEqual(accepted_family_paths(), discovered_family_paths())

    def test_reviewed_interfaces_decoder_and_cohesive_control_core_are_byte_stable(self) -> None:
        self.assertEqual(13, len(STABLE_HASHES))
        for path, expected in STABLE_HASHES.items():
            with self.subTest(path=path):
                self.assertEqual(
                    expected, hashlib.sha256((REPOSITORY_ROOT / path).read_bytes()).hexdigest(),
                    REVIEWED_BASE,
                )
        core = c_function_definitions(read_source("kernel/bootstrap_control_core.c"))
        self.assertEqual(27, len(core))
        self.assertEqual(119, max(core.values()))
        inline = c_function_definitions(read_source("kernel/bootstrap_control_internal.h"))
        self.assertEqual({"micros_bootstrap_control_find_binding_bounded"}, set(inline))
        self.assertLessEqual(max(inline.values()), FUNCTION_REVIEW_LINE_LIMIT)

    def test_source_sets_have_exact_order_and_no_duplicate_raw_split_sources(self) -> None:
        cmake = read_source("CMakeLists.txt")
        observed_sets = re.findall(
            r"\bset\s*\(\s*(MICROS_(?:BOOTSTRAP_[A-Z_]+|BOOT_FOUNDATION|FDT_HOST_TEST)_SOURCES)\b",
            cmake,
        )
        self.assertEqual(set(SOURCE_SETS), set(observed_sets))
        self.assertEqual(9, len(observed_sets))
        for name, expected in SOURCE_SETS.items():
            with self.subTest(source_set=name):
                blocks = cmake_commands(cmake, "set", name)
                self.assertEqual(1, len(blocks), name)
                self.assertEqual(
                    expected,
                    tuple(re.findall(r"(?:kernel|tests/host|tests/qemu)/[A-Za-z0-9_]+\.c", blocks[0])),
                )
        for source in all_sources():
            # This already separate wrapper also has its existing native ABI consumer.
            count = 2 if source == "tests/qemu/bootstrap_launcher_control.c" else 1
            self.assertEqual(count, cmake.count(source), source)

    def test_all_current_consumers_expand_only_their_required_sets_once(self) -> None:
        cmake = read_source("CMakeLists.txt")
        consumers = {
            "micros": ("add_executable", (
                "MICROS_BOOTSTRAP_MANIFEST_SOURCES", "MICROS_BOOTSTRAP_IMAGE_SOURCES",
                "MICROS_BOOTSTRAP_RUNTIME_SOURCES", "MICROS_BOOTSTRAP_SYSCALL_SOURCES",
                "MICROS_BOOT_FOUNDATION_SOURCES",
            )),
            "bootstrap_test": ("add_executable", (
                "MICROS_BOOTSTRAP_MANIFEST_SOURCES", "MICROS_BOOTSTRAP_MANIFEST_HOST_TEST_SOURCES",
            )),
            "bootstrap_control_test": ("add_executable", (
                "MICROS_BOOTSTRAP_MANIFEST_SOURCES", "MICROS_BOOTSTRAP_IMAGE_SOURCES",
                "MICROS_BOOTSTRAP_CONTROL_HOST_TEST_SOURCES",
            )),
            "fdt_test": ("add_executable", ("MICROS_FDT_HOST_TEST_SOURCES",)),
            "micros_bootstrap_launcher": (
                "micros_add_bootstrap_user", ("MICROS_BOOTSTRAP_LAUNCHER_SOURCES",),
            ),
        }
        for target, (command, required) in consumers.items():
            with self.subTest(consumer=target):
                blocks = cmake_commands(cmake, command, target)
                self.assertEqual(1, len(blocks))
                observed = {
                    name: blocks[0].count("${" + name + "}") for name in SOURCE_SETS
                }
                self.assertEqual({name: int(name in required) for name in SOURCE_SETS}, observed)
                self.assertFalse(set(re.findall(
                    r"(?:kernel|tests/host|tests/qemu)/[A-Za-z0-9_]+\.c", blocks[0],
                )) & set(all_sources()), target)
        for name in SOURCE_SETS:
            self.assertEqual(
                sum(name in required for _, required in consumers.values()),
                cmake.count("${" + name + "}"), name,
            )
        for target in ("micros", "bootstrap_control_test"):
            block = cmake_command(cmake, "add_executable", target)
            for source in ("kernel/bootstrap_control.c", "kernel/bootstrap_control_core.c"):
                self.assertEqual(1, block.count(source), (target, source))
        native_control = cmake_command(cmake, "add_executable", "bootstrap_control_test")
        for source in ("kernel/endpoint.c", "kernel/kernel_objects.c", "kernel/scheduler_core.c"):
            self.assertEqual(1, native_control.count(source), source)
        self.assertEqual(1, native_control.count("${MICROS_IPC_CORE_SOURCES}"))
        self.assertEqual(
            1, cmake_command(cmake, "add_executable", "fdt_test").count("kernel/fdt.c"),
        )
        runtime = cmake_command(cmake, "add_executable", "runtime_test")
        self.assertEqual(1, runtime.count("tests/qemu/bootstrap_launcher_control.c"))
        self.assertFalse(any("${" + name + "}" in runtime for name in SOURCE_SETS))

    def test_exact_sixty_symbol_migration_tracks_actual_current_and_final_owners(self) -> None:
        self.assertEqual(60, len(FINAL_SYMBOL_OWNERS))
        self.assertEqual(60, len(INITIAL_SYMBOL_OWNER_MIGRATION))
        self.assertEqual(60, len(set(INITIAL_SYMBOL_OWNER_MIGRATION)))
        self.assertEqual(len(SYMBOL_OWNER_MIGRATION), len(set(SYMBOL_OWNER_MIGRATION)))
        self.assertLessEqual(set(SYMBOL_OWNER_MIGRATION), set(INITIAL_SYMBOL_OWNER_MIGRATION))
        self.assertEqual(
            (6, 8, 46),
            (
                sum(old == "kernel/bootstrap_manifest.c" for old, _, _ in INITIAL_SYMBOL_OWNER_MIGRATION),
                sum(old == "kernel/bootstrap_runtime.c" for old, _, _ in INITIAL_SYMBOL_OWNER_MIGRATION),
                sum(old.startswith("tests/host/") for old, _, _ in INITIAL_SYMBOL_OWNER_MIGRATION),
            ),
        )
        for old, final, name in INITIAL_SYMBOL_OWNER_MIGRATION:
            self.assertEqual(FINAL_SYMBOL_OWNERS[name], final)
            self.assertNotEqual(old, final)
        for name in FINAL_SYMBOL_OWNERS:
            with self.subTest(symbol=name):
                self.assertEqual(
                    [self.symbol_owner(name)],
                    [path for path, definitions in self.definitions.items() if name in definitions],
                )
        expected_cases = {*MANIFEST_CASE_ORDER, *CONTROL_CASE_ORDER, *(name for _, name in FDT_CASES)}
        self.assertEqual(46, len(expected_cases))
        self.assertEqual(
            expected_cases,
            {name for path, definitions in self.definitions.items() if path.startswith("tests/host/")
             for name in definitions if name.startswith("test_")},
        )

    def test_retained_entries_and_supported_function_inventory_are_exact(self) -> None:
        for path, names in RETAINED_OWNER_GROUPS.items():
            with self.subTest(owner=path):
                self.assertLessEqual(set(names), set(self.definitions.get(path, {})))
        self.assertEqual(
            set(NATIVE_MAIN_HASHES),
            {path for path, names in self.definitions.items() if "main" in names},
        )
        for name, owner in (
            ("kernel_main", "kernel/main.c"),
            ("micros_service_main", "tests/qemu/bootstrap_launcher.c"),
            ("micros_bootstrap_handle_captured_user_ecall", "kernel/bootstrap_syscall.c"),
        ):
            self.assertEqual([owner], [path for path, names in self.definitions.items() if name in names])
        expected = {
            name for names in RETAINED_OWNER_GROUPS.values() for name in names if name.startswith("micros_")
        } | {name for name in FINAL_SYMBOL_OWNERS if name.startswith("micros_")}
        self.assertEqual(
            expected, {name for names in self.definitions.values() for name in names if name.startswith("micros_")},
        )

    def test_private_helper_owners_follow_their_specific_migration_anchor(self) -> None:
        active = set(LONG_FUNCTION_MIGRATION) | {
            (old, name) for old, _, name in SYMBOL_OWNER_MIGRATION
        }
        for current, final, name, _ in helper_specs():
            with self.subTest(helper=name, original=current):
                path, _ = self.find_helper(current, name)
                if path == current and current != final:
                    self.assertIn(HELPER_MIGRATION_ANCHORS[final], active)
                else:
                    self.assertEqual(final, path)

    def test_new_private_interfaces_are_declarations_only_and_have_one_declared_owner(self) -> None:
        declared = set()
        allowed_names = set(FINAL_SYMBOL_OWNERS)
        prefixes = (
            "bootstrap_manifest_", "bootstrap_transition_", "bootstrap_runtime_",
            "bootstrap_preparation_", "bootstrap_diagnostics_", "bootstrap_syscall_",
            "boot_", "bootstrap_launcher_", "bootstrap_manifest_test_",
            "bootstrap_control_test_", "fdt_test_",
        )
        for path in PRIVATE_HEADERS:
            if not (REPOSITORY_ROOT / path).is_file():
                continue
            text = read_source(path)
            with self.subTest(header=path):
                self.assertEqual({}, c_function_definitions(text))
                self.assertEqual((), file_scope_objects(text))
                self.assertNotRegex(c_declaration_text(text), r"\b(?:static|inline)\b")
                self.assertNotRegex(c_lexical_text(macro_definition_text(text)), r"\bstatic\b")
                for name in re.findall(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(", c_declaration_text(text)):
                    if name in {"_Static_assert", "_Alignas", "_Alignof", "sizeof"}:
                        continue
                    self.assertTrue(name in allowed_names or name.startswith(prefixes), (path, name))
                    owners = [source for source, names in self.definitions.items() if name in names]
                    self.assertEqual(1, len(owners), (path, name, owners))
                    declared.add(name)
        stable_declarations = "\n".join(read_source(path) for path in STABLE_HASHES if path.endswith(".h"))
        for path, names in self.definitions.items():
            lexical = c_declaration_text(self.texts[path])
            for name in names:
                static = re.search(
                    rf"(?ms)^static\b[^;{{}}]*\b{re.escape(name)}\s*\(", lexical,
                )
                if static is not None or name in {"main", "kernel_main", "micros_service_main"}:
                    continue
                self.assertTrue(
                    name in declared or re.search(rf"\b{re.escape(name)}\s*\(", stable_declarations),
                    (path, name, "cross-file functions require a private or preserved declaration"),
                )

    def test_recursive_local_includes_cannot_hide_code_storage_or_textual_sources(self) -> None:
        closure = {
            dependency for path in self.texts for dependency in local_include_closure(path)
        }
        for path in sorted(closure):
            text = read_source(path)
            with self.subTest(path=path):
                self.assertNotRegex(
                    text, r'(?m)^\s*#\s*include\s*[<"][^">]+\.(?:c|s|S|py)[">]',
                )
                self.assertNotRegex(text, r'(?m)^\s*#\s*include\s+(?![<"\s])')
                self.assertNotRegex(c_lexical_text(macro_definition_text(text)), r"\bstatic\b")
                if not path.endswith(".h"):
                    continue
                self.assertEqual((), file_scope_objects(text))
                definitions = c_function_definitions(text)
                if definitions:
                    self.assertIn(path, EXISTING_INLINE_HEADERS)
                    self.assertEqual(
                        EXISTING_INLINE_HEADERS[path],
                        hashlib.sha256((REPOSITORY_ROOT / path).read_bytes()).hexdigest(),
                    )

    def test_parser_checks_reject_alias_array_and_hidden_macro_state_without_files(self) -> None:
        good = "static struct bootstrap_runtime_context runtime_context;"
        self.assertEqual(("bootstrap_runtime_context", "runtime_context"), scalar_context(good))
        for declaration in (
            "static struct bootstrap_runtime_context runtime_context[2];",
            "static context_alias runtime_context;",
            "extern struct bootstrap_runtime_context runtime_context = {0};",
        ):
            with self.subTest(declaration=declaration):
                self.assertEqual(1, len(file_scope_objects(declaration)))
                with self.assertRaises(AssertionError):
                    scalar_context(declaration)
        for declaration in (
            "static struct bootstrap_runtime_context one, two;",
            "static struct bootstrap_runtime_context one = {0}, two = {0};",
        ):
            with self.assertRaisesRegex(AssertionError, "multiple objects"):
                file_scope_objects(declaration)
        macro = "#define HIDDEN_STATE static struct bootstrap_runtime_context hidden;\n"
        self.assertRegex(c_lexical_text(macro_definition_text(macro)), r"\bstatic\b")
        with patch(f"{__name__}.local_include_closure", return_value=("owner.c", "bridge.h", "hidden.h")), \
                patch(f"{__name__}.read_source", side_effect={
                    "owner.c": "", "bridge.h": "",
                    "hidden.h": "#define HIDDEN_CALL micros_bootstrap_runtime_prepare\n"
                                "static inline void hidden(void) { micros_bootstrap_runtime_fail(0); }\n",
                }.__getitem__):
            executable = header_executable_text("owner.c")
        self.assertIn("micros_bootstrap_runtime_prepare", executable)
        self.assertIn("micros_bootstrap_runtime_fail", executable)
        self.assertFalse(has_function_reference("profile->id == 1", "profile"))
        self.assertFalse(has_function_reference("valid && profile->id == 1", "profile"))
        self.assertFalse(has_function_reference("field = &profile->id", "profile"))
        self.assertFalse(has_function_reference(
            "const struct micros_privilege_profile *profile; resolve(&profile);", "profile",
        ))
        self.assertTrue(has_function_reference("profile(1, 0)", "profile"))
        self.assertTrue(has_function_reference("callback = &profile;", "profile"))
        self.assertTrue(has_function_reference("#define CALLBACK profile", "profile"))
        self.assertTrue(has_function_reference(
            "callback = bootstrap_manifest_copy_bytes;", "bootstrap_manifest_copy_bytes",
        ))

    def test_target_storage_is_exactly_the_original_ten_or_one_complete_context(self) -> None:
        mutable = {
            path: tuple(declaration for declaration in file_scope_objects(self.texts[path])
                        if not is_immutable(declaration))
            for path in SOURCE_SETS["MICROS_BOOTSTRAP_RUNTIME_SOURCES"] if path in self.texts
        }
        mutable = {path: declarations for path, declarations in mutable.items() if declarations}
        original = tuple(compact("static " + field) for field in RUNTIME_FIELDS)
        observed = {path: tuple(map(compact, declarations)) for path, declarations in mutable.items()}
        if observed == {"kernel/bootstrap_runtime.c": original}:
            self.assertNotIn(RUNTIME_STATE, mutable)
        else:
            self.assertEqual({RUNTIME_STATE}, set(mutable))
            self.assertEqual(1, len(mutable[RUNTIME_STATE]))
            context_type, object_name = scalar_context(mutable[RUNTIME_STATE][0])
            definitions = [
                body for path in (*self.texts, *PRIVATE_HEADERS) if (REPOSITORY_ROOT / path).is_file()
                for body in type_bodies(read_source(path), context_type)
            ]
            self.assertEqual(1, len(definitions), context_type)
            self.assertEqual(tuple(map(compact, RUNTIME_FIELDS)), tuple(
                compact(field + ";") for field in definitions[0].split(";") if field.strip()
            ))
            self.assertTrue(any(
                re.search(rf"\breturn\s+&{re.escape(object_name)}\s*;", c_lexical_text(
                    c_function_body(self.texts[RUNTIME_STATE], name),
                )) for name in self.definitions[RUNTIME_STATE]
            ), "the state owner must expose one private borrow, not a copied runtime")
        for symbol in ("micros_bootstrap_runtime_state", "micros_bootstrap_runtime_authoritative_state"):
            path = self.symbol_owner(symbol)
            body = compact(c_function_body(self.texts[path], symbol))
            self.assertIn("MICROS_BOOTSTRAP_PHASE_UNINITIALIZED?NULL:", body)
            self.assertRegex(body, r":&[A-Za-z_][A-Za-z0-9_]*(?:->|\.)?bootstrap_state;|:&bootstrap_state;")

    def test_launcher_exported_objects_keep_types_initializers_and_one_owner(self) -> None:
        observed = {}
        names = ("micros_bootstrap_service_config", "micros_bootstrap_launcher_rodata",
                 "micros_bootstrap_launcher_data")
        for path in SOURCE_SETS["MICROS_BOOTSTRAP_LAUNCHER_SOURCES"]:
            if path not in self.texts:
                continue
            objects = tuple(
                declaration for declaration in file_scope_objects(self.texts[path])
                if any(re.search(rf"\b{name}\b", declaration) for name in names)
                or not is_immutable(declaration)
            )
            if objects:
                observed[path] = tuple(map(compact, objects))
        expected = tuple(map(compact, LAUNCHER_OBJECTS))
        self.assertIn(
            observed,
            ({"tests/qemu/bootstrap_launcher.c": expected}, {LAUNCHER_COMMON: expected}),
        )

    def test_native_mutable_storage_preserves_automatic_lifetimes_and_three_catalog_pages(self) -> None:
        host_paths = tuple(path for path in self.texts if path.startswith("tests/host/"))
        file_objects = {
            path: tuple(declaration for declaration in file_scope_objects(self.texts[path])
                        if not is_immutable(declaration))
            for path in host_paths
        }
        file_objects = {path: declarations for path, declarations in file_objects.items() if declarations}
        local_objects = {
            path: tuple(declaration for declaration in static_local_storage(self.texts[path])
                        if not is_immutable(declaration))
            for path in host_paths
        }
        local_objects = {path: declarations for path, declarations in local_objects.items() if declarations}
        if file_objects:
            owner = "tests/host/bootstrap_control_image_catalog_test.c"
            self.assertEqual({owner}, set(file_objects))
            self.assertEqual(1, len(file_objects[owner]))
            context_type, _ = scalar_context(file_objects[owner][0])
            bodies = [
                body for path in (*host_paths, *PRIVATE_HEADERS) if (REPOSITORY_ROOT / path).is_file()
                for body in type_bodies(read_source(path), context_type)
            ]
            self.assertEqual(1, len(bodies))
            self.assertEqual(
                compact("unsigned char text[MICROS_SV39_PAGE_SIZE];"
                        "unsigned char rodata[MICROS_SV39_PAGE_SIZE];"
                        "unsigned char data[MICROS_SV39_PAGE_SIZE];"),
                compact(bodies[0]),
            )
            self.assertEqual({}, local_objects)
        else:
            expected = tuple(
                compact(f"static unsigned char {name}[MICROS_SV39_PAGE_SIZE];")
                for name in ("text", "rodata", "data")
            )
            self.assertEqual(
                {"tests/host/bootstrap_control_test.c": expected},
                {path: tuple(map(compact, declarations)) for path, declarations in local_objects.items()},
            )
            self.assertIn(
                ("tests/host/bootstrap_control_test.c",
                 "tests/host/bootstrap_control_image_catalog_test.c", "test_image_catalog"),
                SYMBOL_OWNER_MIGRATION,
            )

    def test_no_other_file_scope_or_static_local_mutable_state_is_added(self) -> None:
        exceptional = {
            *SOURCE_SETS["MICROS_BOOTSTRAP_RUNTIME_SOURCES"],
            *SOURCE_SETS["MICROS_BOOTSTRAP_LAUNCHER_SOURCES"],
            "tests/host/bootstrap_control_image_catalog_test.c",
        }
        for path, text in self.texts.items():
            with self.subTest(path=path):
                if path not in exceptional:
                    self.assertEqual(
                        (), tuple(item for item in file_scope_objects(text) if not is_immutable(item)),
                    )
                if path != "tests/host/bootstrap_control_test.c":
                    self.assertEqual(
                        (), tuple(item for item in static_local_storage(text) if not is_immutable(item)),
                    )

    def test_existing_record_fixture_reference_and_trace_field_layouts_are_preserved(self) -> None:
        texts = [
            read_source(path) for path in (*self.texts, *PRIVATE_HEADERS)
            if (REPOSITORY_ROOT / path).is_file()
        ]
        for name, expected in TYPE_LAYOUT_HASHES.items():
            with self.subTest(type=name):
                bodies = [body for text in texts for body in type_bodies(text, name)]
                self.assertEqual(1, len(bodies))
                self.assertEqual(expected, compact_hash(bodies[0]))

    def assert_automatic_context(
        self, path: str, entry: str, family_word: str, required: tuple[str, ...],
        optional: tuple[str, ...], original_declarations: tuple[str, ...],
    ) -> None:
        body = c_function_body(self.texts[path], entry)
        contexts = [
            (tag, name) for tag, name in re.findall(
                r"\bstruct\s+([A-Za-z_][A-Za-z0-9_]*context)"
                r"\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:=\s*\{0\})?\s*;",
                c_declaration_text(body),
            ) if family_word in tag
        ]
        if not contexts:
            self.assertIn((path, entry), LONG_FUNCTION_MIGRATION)
            for declaration in original_declarations:
                self.assertIn(compact(declaration), compact(body))
            return
        self.assertEqual(1, len(contexts))
        context_type, object_name = contexts[0]
        self.assertNotRegex(c_declaration_text(body), rf"\bstatic\b[^;]*\b{re.escape(object_name)}\b")
        bodies = [
            fields for source in (*self.texts, *PRIVATE_HEADERS)
            if (REPOSITORY_ROOT / source).is_file()
            for fields in type_bodies(read_source(source), context_type)
        ]
        self.assertEqual(1, len(bodies))
        fields = tuple(compact(field + ";") for field in bodies[0].split(";") if field.strip())
        self.assertEqual(len(fields), len(set(fields)))
        self.assertLessEqual(set(map(compact, required)), set(fields))
        self.assertLessEqual(set(fields), set(map(compact, (*required, *optional))))

    def test_boot_and_launcher_contexts_stay_automatic_in_the_original_entry(self) -> None:
        boot_fields = (
            "const struct micros_frame_allocator *frame_allocator;",
            "const struct micros_frame_ownership *frame_ownership;",
            "const struct micros_kernel_address_space_report *address_space;",
            "const struct micros_kernel_objects *kernel_objects;",
            "struct micros_hart *boot_hart;",
            "struct micros_fdt_memory_map memory_map;",
        )
        self.assert_automatic_context(
            "kernel/main.c", "kernel_main", "boot", boot_fields,
            ("uintptr_t hart_id;", "uintptr_t fdt_address;", "enum micros_fdt_error error;", "size_t index;"),
            boot_fields,
        )
        launcher_fields = (
            "const struct micros_bootstrap_manifest *manifest;",
            "uint32_t order[MICROS_BOOTSTRAP_SERVICE_CAPACITY];",
            "uint8_t state[MICROS_BOOTSTRAP_SERVICE_CAPACITY];",
        )
        self.assert_automatic_context(
            "tests/qemu/bootstrap_launcher.c", "micros_service_main", "launcher",
            launcher_fields, ("size_t order_index;",),
            (
                "const struct micros_bootstrap_manifest *manifest =",
                "uint32_t order[MICROS_BOOTSTRAP_SERVICE_CAPACITY] = {0};",
                "uint8_t state[MICROS_BOOTSTRAP_SERVICE_CAPACITY] = {0};",
            ),
        )

    def test_manifest_candidate_and_captured_transaction_plans_are_not_static(self) -> None:
        body = c_function_body(
            self.texts["kernel/bootstrap_manifest.c"], "micros_bootstrap_manifest_validate_detailed",
        )
        candidates = re.findall(
            r"\bstruct\s+([A-Za-z_][A-Za-z0-9_]*)\s+candidate\s*(?:=\s*\{0\})?\s*;",
            c_declaration_text(body),
        )
        self.assertEqual(1, len(candidates), "validation publishes one automatic candidate on success")
        self.assertNotRegex(c_declaration_text(body), r"\bstatic\b[^;]*\bcandidate\b")
        for path in SOURCE_SETS["MICROS_BOOTSTRAP_SYSCALL_SOURCES"]:
            if path not in self.texts:
                continue
            self.assertEqual((), file_scope_objects(self.texts[path]), path)
            self.assertEqual((), static_local_storage(self.texts[path]), path)

    def test_dependency_direction_is_checked_per_owner_through_recursive_includes(self) -> None:
        self.assertEqual(set(all_sources()), set(PERMITTED_OWNER_DEPENDENCIES))
        providers = {}
        for path, definitions in self.definitions.items():
            for name in definitions:
                providers.setdefault(name, []).append(path)
        for path, definitions in self.definitions.items():
            executable_headers = header_executable_text(path)
            macros = macro_definition_text(self.texts[path])
            for name in definitions:
                owner = logical_owner(path, name)
                allowed = {owner, *PERMITTED_OWNER_DEPENDENCIES[owner]}
                body = c_lexical_text(
                    c_function_body(self.texts[path], name) + "\n" + macros + "\n" + executable_headers,
                )
                body = re.sub(r"\b(?:struct|union|enum)\s+[A-Za-z_][A-Za-z0-9_]*", "", body)
                violations = {}
                for called, sources in providers.items():
                    if not has_function_reference(body, called):
                        continue
                    selected = [path] if called in definitions else sources
                    owners = {logical_owner(source, called) for source in selected}
                    if not owners & allowed:
                        violations[called] = sorted(owners)
                with self.subTest(source=path, function=name, owner=owner):
                    self.assertEqual({}, violations)

    def test_private_include_direction_preserves_portable_launcher_and_oracle_boundaries(self) -> None:
        host_headers = {path for path in PRIVATE_HEADERS if path.startswith("tests/host/")}
        portable = {
            *SOURCE_SETS["MICROS_BOOTSTRAP_MANIFEST_SOURCES"],
            *SOURCE_SETS["MICROS_BOOTSTRAP_IMAGE_SOURCES"],
        }
        target_headers = {
            "kernel/bootstrap_runtime_internal.h", "kernel/bootstrap_syscall_internal.h",
            "kernel/boot_internal.h",
        }
        launcher = set(SOURCE_SETS["MICROS_BOOTSTRAP_LAUNCHER_SOURCES"])
        for path in self.texts:
            closure = set(local_include_closure(path))
            if path.startswith("kernel/"):
                self.assertFalse(closure & host_headers, path)
                self.assertNotIn("tests/qemu/bootstrap_launcher_internal.h", closure, path)
            if path in portable:
                self.assertFalse(closure & target_headers, path)
                self.assertFalse(closure & host_headers, path)
            if path in launcher:
                self.assertFalse({item for item in closure if item.startswith("kernel/")}, path)
                self.assertFalse(closure & host_headers, path)
            if path == REFERENCE:
                self.assertFalse({item for item in closure if item.startswith("kernel/")}, path)
                self.assertFalse(closure & (host_headers - {
                    "tests/host/bootstrap_runtime_model_internal.h",
                }), path)

    def assert_independent_reference(self, text: str) -> None:
        self.assertNotRegex(
            c_lexical_text(text),
            r"\b(?:micros_[A-Za-z0-9_]+|bootstrap_manifest_[A-Za-z0-9_]+"
            r"|bootstrap_transition_[A-Za-z0-9_]+|bootstrap_preparation_[A-Za-z0-9_]+"
            r"|bootstrap_runtime_(?!reference_)[A-Za-z0-9_]+)\s*\(",
        )

    def test_reference_transitions_classification_and_projection_remain_independent(self) -> None:
        for current, final, _, names in HELPER_GROUPS:
            if final != REFERENCE:
                continue
            for name in names:
                self.assert_independent_reference(self.helper_body(current, name))
        if REFERENCE in self.texts:
            self.assert_independent_reference(
                "\n".join(c_function_body(self.texts[REFERENCE], name)
                          for name in self.definitions[REFERENCE]) + header_executable_text(REFERENCE),
            )
        for call in (
            "micros_bootstrap_runtime_release(0, 0, 0);",
            "bootstrap_manifest_copy_bytes(0, 0, 0);",
            "bootstrap_transition_runtime_entry(0, 0);",
        ):
            with self.subTest(hidden_call=call):
                with self.assertRaises(AssertionError):
                    self.assert_independent_reference(call)

    def test_read_only_validation_and_reverse_rollback_do_not_gain_upward_authority(self) -> None:
        readonly_calls = {
            "micros_kernel_object_runtime_authoritative_registry",
            "micros_ipc_runtime_authoritative_registry", "micros_tty_handoff_runtime_state",
            "micros_bootstrap_control_validate", "micros_frame_ownership_runtime_validate",
            "micros_grant_runtime_validate", "micros_tty_handoff_runtime_validate",
            "micros_user_address_space_validate", "micros_user_execution_inspect",
            "micros_user_execution_validate_context", "micros_vm_handoff_runtime_state",
            "micros_vm_handoff_runtime_validate", "micros_vm_boot_validate",
            "micros_vm_snapshot_validate_current",
        }
        for name in FINAL_OWNER_GROUPS[RUNTIME_VALIDATION]:
            path = self.symbol_owner(name)
            calls = self.expanded_calls(
                path, name, SOURCE_SETS["MICROS_BOOTSTRAP_RUNTIME_SOURCES"],
                readonly_calls | {"bytes_equal", "user_range_matches", "read_user_range"},
            )
            self.assertLessEqual({call for call in calls if call.startswith("micros_")}, readonly_calls)
        rollback = self.helper_body("kernel/bootstrap_runtime.c", "rollback_preparation")
        self.assertLess(rollback.index("--count"), rollback.index("rollback_record("))
        for name in ("rollback_preparation", "rollback_record", "release_page"):
            body = self.helper_body("kernel/bootstrap_runtime.c", name)
            self.assertNotRegex(
                c_lexical_text(body),
                r"\bmicros_[A-Za-z0-9_]*(?:_allocate|_create|_reserve|_publish|_admit)(?:_[A-Za-z0-9_]+)?\s*\(",
            )

    def test_native_case_registries_order_labels_short_circuit_and_status_are_unchanged(self) -> None:
        for path, expected_hash in NATIVE_MAIN_HASHES.items():
            with self.subTest(entry=path):
                body = c_function_body(self.texts[path], "main")
                self.assertEqual(expected_hash, compact_hash(body))
        for path, expected in (
            ("tests/host/bootstrap_manifest_test.c", MANIFEST_CASE_ORDER),
            ("tests/host/bootstrap_control_test.c", CONTROL_CASE_ORDER),
        ):
            self.assertEqual(
                expected, tuple(re.findall(r"\b(test_[A-Za-z0-9_]+)\s*\(", c_function_body(self.texts[path], "main"))),
            )
        self.assertEqual(
            FDT_CASES,
            tuple(re.findall(
                r'\{\s*"([^"\n]+)"\s*,\s*(test_[A-Za-z0-9_]+)\s*\}',
                c_function_body(self.texts["tests/host/fdt_test.c"], "main"),
            )),
        )
        self.assertEqual(
            (
                "manifest contract failed\n", "manifest validation failed\n",
                "manifest rejections failed\n", "VM mapping capacity failed\n",
                "three-service topology failed\n", "TTY service contract failed\n",
                "runtime transitions failed\n", "retained runtime deadline failed\n",
                "runtime model failed\n",
            ),
            string_records(c_function_body(self.texts["tests/host/bootstrap_manifest_test.c"], "main")),
        )
        fdt_strings = string_records(c_function_body(self.texts["tests/host/fdt_test.c"], "main"))
        self.assertEqual(
            ("TAP version 13\n", "ok %zu - %s\n", "not ok %zu - %s\n"),
            fdt_strings[len(FDT_CASES):],
        )

    def test_existing_model_and_protocol_leaf_bodies_are_preserved_across_moves(self) -> None:
        for (current, name), expected in PRESERVED_HELPER_BODY_HASHES.items():
            with self.subTest(helper=name):
                self.assertEqual(expected, compact_hash(self.helper_body(current, name)))

    def test_model_seed_draw_dispatch_projection_and_final_sixteen_trace_are_preserved(self) -> None:
        path = self.symbol_owner("test_replayable_runtime_model")
        entry = "test_replayable_runtime_model"
        initialize = ("reference_initialize", "micros_bootstrap_runtime_initialize")
        release = ("reference_release", "micros_bootstrap_runtime_release")
        ready = ("reference_accept_ready", "micros_bootstrap_runtime_accept_ready")
        complete = ("reference_complete", "micros_bootstrap_runtime_complete")
        tail = ("reference_classify_ready", "micros_bootstrap_classify_ready",
                "project_reference", "memcmp", "print_model_failure")
        expected = (*initialize, *release * 4, *ready * 3, *complete, *release, *tail)
        leaves = {*expected, "model_random"}
        calls = self.expanded_calls(
            path, entry, SOURCE_SETS["MICROS_BOOTSTRAP_MANIFEST_HOST_TEST_SOURCES"], leaves,
        )
        self.assertEqual(expected, tuple(call for call in calls if call in set(expected)))
        self.assertEqual(1, calls.count("model_random"))
        self.assertLess(calls.index("model_random"), calls.index("reference_initialize"))
        model_text = (
            c_function_body(self.texts[path], entry)
            if path == "tests/host/bootstrap_manifest_test.c" else self.texts[path]
        )
        model_text = canonical_helper_text(model_text)
        self.assertEqual(1, model_text.count("UINT64_C(0x42535452504c4159)"))
        for fragment in (
            "step<4096", "step%16", "random_value&UINT64_C(3)", "random_value&UINT64_C(4)",
            "random_value>>32", "random_value&UINT64_C(8)", "trace[step%16]",
            ".expected_error=expected_error", ".actual_error=actual_error",
            ".expected_class=expected_class", ".actual_class=actual_class",
            "compare_error&&actual_error!=expected_error", "actual_class!=expected_class",
        ):
            self.assertIn(fragment, compact(model_text))
        switches = re.findall(r"\bcase\s+([0-9]+)\s*:", c_lexical_text(model_text))
        self.assertEqual(
            ("0", "1", "2", "13", "3", "4", "5", "6", "7", "8", "9", "10", "11",
             "12", "15", "14", "0", "1", "2"), tuple(switches),
        )
        for declaration in (
            "struct reference_runtime reference = {0};",
            "struct micros_bootstrap_runtime runtime = {0};",
            "struct micros_bootstrap_runtime expected_runtime;",
            "struct model_trace_entry trace[16] = {0};",
        ):
            self.assertIn(compact(declaration), compact(model_text))

    def test_launcher_serial_control_and_starting_service_classification_are_preserved(self) -> None:
        leaves = {
            "validate_manifest", "validate_configuration", "find_manifest_service",
            "find_config_endpoint", "console_mapped_notification_is_canonical",
            "message_shape_detail", "read_u32_le", "bootstrap_control", "fail",
            "micros_bootstrap_launcher_console_begin",
        }
        calls = self.expanded_calls(
            "tests/qemu/bootstrap_launcher.c", "micros_service_main",
            SOURCE_SETS["MICROS_BOOTSTRAP_LAUNCHER_SOURCES"], leaves,
        )
        selected = leaves - {"fail"} | {"micros_bootstrap_launcher_console_begin", "micros_runtime_receive"}
        self.assertEqual(
            (
                "validate_manifest", "validate_configuration", "find_manifest_service",
                "find_manifest_service", "micros_bootstrap_launcher_console_begin",
                "micros_runtime_receive", "console_mapped_notification_is_canonical",
                "bootstrap_control", "micros_runtime_receive", "message_shape_detail",
                "find_config_endpoint", "find_manifest_service", "read_u32_le", "read_u32_le",
                "bootstrap_control", "bootstrap_control",
            ),
            tuple(call for call in calls if call in selected),
        )
        classification = []
        for path in SOURCE_SETS["MICROS_BOOTSTRAP_LAUNCHER_SOURCES"]:
            for name in self.definitions.get(path, {}):
                body = canonical_helper_text(c_function_body(self.texts[path], name))
                if re.search(r"state\[[^]]+\]\s*==\s*LAUNCHER_SERVICE_STARTING", body):
                    classification.append(body)
        self.assertEqual(1, len(classification))
        body = classification[0]
        match = re.search(
            r"if\s*\([^{}]*==\s*LAUNCHER_SERVICE_STARTING\s*&&[^{}]*service_id\s*==\s*"
            r"(?:[A-Za-z_][A-Za-z0-9_]*->)?service_id\s*\)\s*\{", body,
        )
        self.assertIsNotNone(match)
        starting = c_function_body("void selected(void)" + body[match.end() - 1:], "selected")
        self.assertIn("payload[4]", starting)
        self.assertIn("payload[16]", starting)
        suffix = body[match.end() - 1 + len(starting):]
        self.assertLess(suffix.index("LAUNCHER_SERVICE_READY"), suffix.index("LAUNCHER_SERVICE_PREPARED"))
        self.assertLess(
            suffix.index("MICROS_BOOTSTRAP_FAILURE_READY_DUPLICATE"),
            suffix.index("MICROS_BOOTSTRAP_FAILURE_READY_EARLY"),
        )

    def test_boot_compile_time_branch_order_calls_markers_and_panic_fallbacks_are_exact(self) -> None:
        path = (
            "kernel/main.c" if ("kernel/main.c", "kernel_main") in LONG_FUNCTION_MIGRATION
            else "kernel/boot_component_tests.c"
        )
        self.assertIn(path, self.texts)
        bodies = "\n".join(c_function_body(self.texts[path], name) for name in self.definitions[path])
        self.assertEqual(30, len(BOOT_BRANCHES))
        self.assertEqual(BOOT_BRANCHES, branch_records(bodies))
        selected = {
            "micros_kernel_object_runtime_initialize", "micros_trap_install",
            "micros_kernel_object_runtime_registry", "micros_kernel_object_runtime_boot_hart",
            "micros_fdt_parse_memory_map", "micros_fdt_error_name", "sbi_system_reset",
            "micros_bootstrap_memory_initialize", "micros_bootstrap_frame_allocator",
            "micros_frame_ownership_runtime_initialize", "micros_kernel_address_space_initialize",
            "micros_plic_initialize", "micros_kernel_address_space_report",
            "micros_frame_ownership_runtime_ledger", "micros_frame_ownership_runtime_validate",
        }
        calls = self.expanded_calls(
            "kernel/main.c", "kernel_main", SOURCE_SETS["MICROS_BOOT_FOUNDATION_SOURCES"],
            selected | {"stop_after_reset_failure"},
        )
        self.assertEqual(
            (
                "micros_kernel_object_runtime_initialize", "micros_trap_install",
                "micros_kernel_object_runtime_registry", "micros_kernel_object_runtime_boot_hart",
                "micros_fdt_parse_memory_map", "micros_fdt_error_name", "sbi_system_reset",
                "micros_bootstrap_memory_initialize", "micros_bootstrap_frame_allocator",
                "micros_frame_ownership_runtime_initialize", "micros_kernel_address_space_initialize",
                "micros_plic_initialize", "micros_kernel_address_space_report",
                "micros_frame_ownership_runtime_ledger", "micros_frame_ownership_runtime_validate",
                "sbi_system_reset",
            ), tuple(call for call in calls if call in selected),
        )
        self.assertEqual(2, calls.count("stop_after_reset_failure"))
        stop = self.helper_body("kernel/main.c", "stop_after_reset_failure")
        self.assertIn('"MICROS_TEST_FAILURE sbi-system-reset-returned\\n"', stop)
        self.assertIn('volatile("wfi")', compact(stop))

    def test_captured_authority_dispatch_and_ready_seal_commit_order_are_preserved(self) -> None:
        family = SOURCE_SETS["MICROS_BOOTSTRAP_SYSCALL_SOURCES"]
        preflight = (
            "micros_bootstrap_control_decode", "micros_bootstrap_runtime_authoritative_state",
            "micros_ipc_runtime_authoritative_registry", "controller_is_authorized",
            "micros_bootstrap_runtime_validate", "micros_grant_runtime_authoritative_registry",
        )
        calls = self.expanded_calls(
            "kernel/bootstrap_syscall.c", "micros_bootstrap_handle_captured_user_ecall", family,
            {*preflight, "handle_console_begin", "handle_release", "handle_accept_ready", "handle_complete"},
        )
        self.assertEqual(preflight, tuple(call for call in calls if call in preflight))
        facade = "\n".join(
            c_function_body(self.texts["kernel/bootstrap_syscall.c"], name)
            for name in self.definitions["kernel/bootstrap_syscall.c"]
        )
        self.assertEqual(
            ("CONSOLE_BEGIN", "RELEASE", "ACCEPT_READY", "FAIL", "COMPLETE"),
            tuple(re.findall(r"\bcase\s+MICROS_BOOTSTRAP_COMMAND_([A-Z_]+)\s*:", facade)),
        )
        for handler, expected in (
            ("handle_accept_ready", (
                "micros_bootstrap_control_prepare_ready", "micros_user_execution_store_context",
                "micros_scheduler_begin_current_ipc", "micros_ipc_reply_expected_caller",
                "micros_scheduler_rollback_current_ipc", "micros_bootstrap_control_commit_ready_prevalidated",
                "micros_tty_handoff_runtime_commit_ready_prevalidated",
                "micros_ipc_stage_no_message_completion", "micros_scheduler_commit_current_ipc",
            )),
            ("handle_complete", (
                "micros_bootstrap_control_prepare_complete", "micros_scheduler_preflight_current_ipc",
                "micros_scheduler_begin_current_ipc", "micros_thread_scheduler_remove",
                "micros_endpoint_commit_source_only_prevalidated",
                "micros_bootstrap_control_commit_complete_prevalidated",
            )),
        ):
            path, name = self.find_helper("kernel/bootstrap_syscall.c", handler)
            calls = self.expanded_calls(path, name, family, set(expected))
            self.assertEqual(expected, tuple(call for call in calls if call in expected), handler)

    def test_generator_shared_signatures_rendering_cli_and_descriptor_schema_are_stable(self) -> None:
        tree = ast.parse(read_source(GENERATOR_PATH))
        functions = {node.name: node for node in tree.body if isinstance(node, ast.FunctionDef)}
        for name, (arguments, defaults) in GENERATOR_SIGNATURES.items():
            self.assertIn(name, functions)
            args = functions[name].args
            self.assertEqual(arguments, tuple(argument.arg for argument in args.args), name)
            self.assertEqual(defaults, tuple(ast.literal_eval(value) for value in args.defaults), name)
            self.assertEqual([], args.posonlyargs)
            self.assertEqual([], args.kwonlyargs)
            self.assertIsNone(args.vararg)
            self.assertIsNone(args.kwarg)
        for name, expected in GENERATOR_BODY_HASHES.items():
            self.assertEqual(expected, python_body_hash(functions[name]), name)
        expected_keys = (
            "prefix", "image", "symbols", "loads", "config_address", "image_end",
            "page_count", "vm_boot_info_address", "vm_boot_info_size",
        )
        descriptors = [
            node for node in ast.walk(tree) if isinstance(node, ast.Dict)
            and any(isinstance(key, ast.Constant) and key.value == "config_address" for key in node.keys)
        ]
        self.assertEqual(1, len(descriptors))
        self.assertEqual(expected_keys, tuple(ast.literal_eval(key) for key in descriptors[0].keys))
        self.assertIn(
            "sum(program.memory_size//PAGE_SIZEforprograminloads)", compact(read_source(GENERATOR_PATH)),
        )
        self.assertEqual(
            ("check_user_elf.validate_elf(image)", "check_user_elf.defined_symbols(image)"),
            tuple(fragment for fragment in (
                "check_user_elf.validate_elf(image)", "check_user_elf.defined_symbols(image)",
            ) if fragment in compact(read_source(GENERATOR_PATH))),
        )

    def test_all_six_current_generator_importers_keep_the_shared_helper_contract(self) -> None:
        observed = {}
        for path in REPOSITORY_ROOT.glob("tools/*.py"):
            tree = ast.parse(path.read_text(encoding="utf-8"))
            aliases = set()
            direct = {}
            for node in ast.walk(tree):
                if isinstance(node, ast.ImportFrom) and node.module == "tools":
                    aliases.update(alias.asname or alias.name for alias in node.names
                                   if alias.name == "generate_bootstrap_fixture")
                if isinstance(node, ast.ImportFrom) and node.module == "tools.generate_bootstrap_fixture":
                    direct.update((alias.asname or alias.name, alias.name) for alias in node.names)
                if isinstance(node, ast.Import):
                    aliases.update(alias.asname or alias.name for alias in node.names
                                   if alias.name == "tools.generate_bootstrap_fixture")
            references = [
                node.attr for node in ast.walk(tree) if isinstance(node, ast.Attribute)
                and isinstance(node.value, ast.Name) and node.value.id in aliases
            ]
            references.extend(
                direct[node.id] for node in ast.walk(tree)
                if isinstance(node, ast.Name) and isinstance(node.ctx, ast.Load) and node.id in direct
            )
            if references:
                self.assertLessEqual(set(references), {
                    "_format_bytes", "_validated_descriptor", "_render_segment_bytes", "_render_image",
                })
                self.assertEqual(0, references.count("_format_bytes"))
                observed[str(path.relative_to(REPOSITORY_ROOT))] = tuple(
                    references.count(name)
                    for name in ("_validated_descriptor", "_render_segment_bytes", "_render_image")
                )
        self.assertEqual(GENERATOR_IMPORTS, observed)

    def test_existing_native_target_ctest_tier_and_launcher_qemu_identities_are_retained(self) -> None:
        cmake = read_source("CMakeLists.txt")
        for executable, ctest in (
            ("bootstrap_test", "bootstrap-manifest"), ("bootstrap_control_test", "bootstrap-control"),
            ("fdt_test", "fdt-parser"),
        ):
            self.assertEqual(1, len(re.findall(
                rf"\badd_test\s*\(\s*NAME\s+{re.escape(ctest)}\s+COMMAND\s+{executable}\s*\)", cmake,
            )))
            for target in ("test-unit-fast", "test-unit"):
                block = cmake_command(cmake, "add_custom_target", target)
                self.assertEqual(1, len(re.findall(rf"\b{executable}\b", block)), target)
            self.assertNotRegex(
                cmake,
                rf"\bset_tests_properties\s*\([^)]*\b{re.escape(ctest)}\b[^)]*\bLABELS\s+slow\b",
            )
            self.assertEqual(
                compact(f"target_compile_features({executable} PRIVATE c_std_17)"),
                compact(cmake_command(cmake, "target_compile_features", executable)),
            )
            options = cmake_commands(cmake, "target_compile_options", executable)
            self.assertEqual(2, len(options))
            self.assertEqual(
                compact(f"target_compile_options({executable} PRIVATE -Wall -Wextra -Werror "
                        "-Wpedantic -Wmissing-prototypes -Wstrict-prototypes)"), compact(options[0]),
            )
            self.assertEqual(
                compact(f"target_compile_options({executable} PRIVATE "
                        "-fno-omit-frame-pointer -fsanitize=address,undefined)"), compact(options[1]),
            )
            self.assertEqual(
                compact(f"target_link_options({executable} PRIVATE -fsanitize=address,undefined)"),
                compact(cmake_command(cmake, "target_link_options", executable)),
            )
            includes = cmake_command(cmake, "target_include_directories", executable)
            expected = compact(f'target_include_directories({executable} PRIVATE '
                               '"${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/include")')
            allowed = {expected}
            if executable == "fdt_test":
                allowed.add(compact('target_include_directories(fdt_test PRIVATE '
                                    '"${CMAKE_CURRENT_SOURCE_DIR}/include")'))
            self.assertIn(compact(includes), allowed)
        self.assertEqual(
            "61d9ad47dd24b97af078699f0f0e17ffa6c641342ba09f764ecbc3e7e4cbfff4",
            hashlib.sha256((REPOSITORY_ROOT / "CMakePresets.json").read_bytes()).hexdigest(),
        )
        presets = json.loads(read_source("CMakePresets.json"))
        self.assertEqual(
            set(QEMU_WORKFLOWS),
            {preset["name"] for preset in presets["workflowPresets"]
             if preset["name"].startswith("test-qemu-")},
        )
        for target, expected in QEMU_TARGET_HASHES.items():
            self.assertEqual(expected, compact_hash(cmake_command(cmake, "add_custom_target", target)), target)

    def test_validation_baselines_retain_full_cross_gate_and_launcher_metadata(self) -> None:
        self.assertEqual(37, len(QEMU_WORKFLOWS))
        self.assertEqual(27, len(CROSS_GATE_WORKFLOWS))
        self.assertEqual(12, len(LAUNCHER_WORKFLOWS))
        self.assertEqual(QEMU_WORKFLOWS, tuple(validation_plan.QEMU_WORKFLOWS))
        baselines = {
            "kernel/bootstrap_manifest.c": (False, CROSS_GATE_WORKFLOWS),
            "kernel/bootstrap_runtime.c": (False, CROSS_GATE_WORKFLOWS),
            "kernel/bootstrap_syscall.c": (False, CROSS_GATE_WORKFLOWS),
            "kernel/bootstrap_image.c": (False, CROSS_GATE_WORKFLOWS),
            "kernel/main.c": (True, QEMU_WORKFLOWS),
            "tests/qemu/bootstrap_launcher.c": (False, LAUNCHER_WORKFLOWS),
            GENERATOR_PATH: (False, LAUNCHER_WORKFLOWS),
            "tests/host/bootstrap_manifest_test.c": (True, ()),
            "tests/host/bootstrap_control_test.c": (True, ()),
            "tests/host/fdt_test.c": (True, ()),
        }
        for path, (full, workflows) in baselines.items():
            with self.subTest(path=path):
                self.assertEqual(full, validation_plan.requires_full_tier([path]))
                self.assertEqual(workflows, tuple(validation_plan.affected_workflows([path])))
                self.assertEqual([], validation_plan.affected_slow_models([path]))
                self.assertEqual([], validation_plan.affected_image_workflows([path]))
                if full:
                    self.assertEqual(
                        validation_plan.plan([path], "full", "main"),
                        validation_plan.plan([path], "fast", "main"),
                    )
        for path in ("CMakeLists.txt", "CMakePresets.json"):
            self.assertTrue(validation_plan.requires_full_tier([path]))
            self.assertEqual(QEMU_WORKFLOWS, tuple(validation_plan.affected_workflows([path])))
            self.assertEqual(set(validation_plan.SLOW_MODEL_INPUTS), set(
                validation_plan.affected_slow_models([path]),
            ))
            self.assertEqual(set(validation_plan.IMAGE_WORKFLOWS), set(
                validation_plan.affected_image_workflows([path]),
            ))

    def test_new_paths_inherit_the_exact_original_classification_without_narrowing_native(self) -> None:
        equivalents = dict(validation_plan.PATH_CLASSIFICATION_EQUIVALENTS)
        for family, paths in SOURCE_SETS.items():
            expected = (*paths[1:], *FAMILY_HEADERS[family])
            if not expected:
                continue
            baseline = paths[0]
            with self.subTest(baseline=baseline):
                self.assertEqual(expected, equivalents.get(baseline))
                for path in expected:
                    self.assertEqual(
                        validation_plan.requires_full_tier([baseline]),
                        validation_plan.requires_full_tier([path]), path,
                    )
                    for classifier in (
                        validation_plan.affected_workflows, validation_plan.affected_slow_models,
                        validation_plan.affected_image_workflows,
                    ):
                        self.assertEqual(classifier([baseline]), classifier([path]), (path, classifier.__name__))
                    for tier in ("fast", "pr"):
                        self.assertEqual(
                            validation_plan.plan([baseline], tier, "main"),
                            validation_plan.plan([path], tier, "main"), (path, tier),
                        )

    def test_this_structural_gate_itself_is_host_unit_only(self) -> None:
        self.assertIn(STRUCTURAL_TEST_PATH, validation_plan.HOST_UNIT_ONLY_INPUTS)
        self.assertEqual(
            [validation_plan.UNIT_FAST, *validation_plan.diff_commands("main")],
            validation_plan.plan([STRUCTURAL_TEST_PATH], "fast", "main"),
        )

    def test_exact_twenty_seven_long_migration_matches_every_actual_current_signal(self) -> None:
        self.assertEqual(27, len(INITIAL_LONG_FUNCTION_MIGRATION))
        self.assertEqual(27, len(set(INITIAL_LONG_FUNCTION_MIGRATION)))
        self.assertEqual(len(LONG_FUNCTION_MIGRATION), len(set(LONG_FUNCTION_MIGRATION)))
        self.assertLessEqual(set(LONG_FUNCTION_MIGRATION), set(INITIAL_LONG_FUNCTION_MIGRATION))
        self.assertTrue(all(size > 120 and len(phases.split(";")) >= 3
                            for size, _, phases in LONG_FUNCTION_REQUIREMENTS.values()))
        oversized = {
            (path, name) for path, names in self.definitions.items() for name, size in names.items()
            if size > FUNCTION_REVIEW_LINE_LIMIT
        }
        oversized.update(
            (GENERATOR_PATH, name) for name, size in python_function_definitions(read_source(GENERATOR_PATH)).items()
            if size > FUNCTION_REVIEW_LINE_LIMIT
        )
        self.assertEqual(set(LONG_FUNCTION_MIGRATION), oversized)
        for path, name in LONG_FUNCTION_MIGRATION:
            size = (
                python_function_definitions(read_source(path))[name]
                if path.endswith(".py") else self.definitions[path][name]
            )
            self.assertLessEqual(size, LONG_FUNCTION_REQUIREMENTS[(path, name)][0], (path, name))

    def test_moving_renaming_or_formatting_the_same_long_body_cannot_discharge_it(self) -> None:
        hashes = {
            fingerprint: (path, name)
            for (path, name), (_, fingerprint, _) in LONG_FUNCTION_REQUIREMENTS.items()
            if path.endswith(".c")
        }
        for path, names in self.definitions.items():
            for name, size in names.items():
                fingerprint = body_fingerprint(c_function_body(self.texts[path], name))
                if fingerprint not in hashes:
                    continue
                original = hashes[fingerprint]
                self.assertEqual(original, (path, name), "moving or renaming is not a phase extraction")
                self.assertIn(original, LONG_FUNCTION_MIGRATION)
                self.assertGreater(size, FUNCTION_REVIEW_LINE_LIMIT, "formatting is not a phase extraction")
        tree = ast.parse(read_source(GENERATOR_PATH))
        expected_hash = LONG_FUNCTION_REQUIREMENTS[(GENERATOR_PATH, "_validated_descriptor")][1]
        for node in ast.walk(tree):
            if isinstance(node, ast.FunctionDef) and python_body_hash(node) == expected_hash:
                self.assertEqual("_validated_descriptor", node.name)
                self.assertIn((GENERATOR_PATH, node.name), LONG_FUNCTION_MIGRATION)
                self.assertGreater(node.end_lineno - node.lineno + 1, FUNCTION_REVIEW_LINE_LIMIT)
        first = c_function_body("void before(void) { int value = 1; value += 2; }", "before")
        renamed = c_function_body("void after(void)\n{\nint renamed=1;renamed+=2;\n}", "after")
        self.assertEqual(body_fingerprint(first), body_fingerprint(renamed))


if __name__ == "__main__":
    unittest.main()

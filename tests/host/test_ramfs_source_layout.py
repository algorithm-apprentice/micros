"""Bounded source-contract regression for Accepted ADR-0066."""

from __future__ import annotations

import ast
import hashlib
import json
from pathlib import Path
import re
import unittest
from unittest.mock import patch

from tests.host.test_frame_ownership_source_layout import c_function_definitions, c_lexical_text
from tests.host.test_kernel_bootstrap_source_layout import c_function_body, local_include_closure
from tests.host.test_kernel_pm_control_source_layout import macro_definition_text
from tests.host.test_pm_service_source_layout import cmake_commands
from tests.host.test_vm_service_source_layout import file_scope_objects
from tools.check_docs import topological_order
from tools import validation_context, validation_plan


ROOT = Path(__file__).resolve().parents[2]
REVIEWED_BASE = "4290ca21c06ac88b299d694c236b774896cb6171"
STRUCTURAL_TEST_PATH = "tests/host/test_ramfs_source_layout.py"
GENERATOR_PATH = "tools/generate_ramfs_service_fixture.py"
BASE_CMAKE_DIGEST = "afb07b0e891e11c677782570ac5a5310dd3cd3d2418c5b741555fe0263a04877"
BASELINE_MODEL_TRACE = "79bfaa8d631a40bf"

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
        "kernel/ramfs_service_test.c",
        "kernel/ramfs_service_test_fixture.c",
        "kernel/ramfs_service_test_report.c",
    ),
}
PRIVATE_HEADERS = (
    "servers/ramfs/ramfs_internal.h",
    "tests/host/ramfs_test_internal.h",
    "tests/host/ramfs_seed_test_internal.h",
    "tests/host/ramfs_model_internal.h",
    "tests/qemu/ramfs_service_vfs_internal.h",
    "kernel/ramfs_service_test_internal.h",
)
FAMILY_PREFIXES = ("ramfs_", "ramfs_test_", "ramfs_seed_test_", "ramfs_model_", "vfs_", "ramfs_test_")
FUNCTIONAL_CONTEXT_FIELDS = (
    "seed_image", "seed_image_size", "state", "state_snapshot",
    "corrupt_state", "storage_snapshot", "bus",
)
FUNCTIONAL_CONTEXT_FUNCTIONS = (
    "load_seed", "reset_bus", "call_expect", "failure_preserves_state",
    "initialize_unmounted", "initialize_mounted", "write_grant_bytes",
    "lookup_path", "create_node", "seed_state_is_exact_and_invariants_fail_closed",
    "protocol_mount_and_result_layout_are_exact",
    "lookup_semantics_and_reference_limits_are_exact",
    "handles_create_mkdir_and_capacity_are_exact",
    "read_write_sparse_and_block_capacity_are_exact",
    "getdents_cursor_and_records_are_exact", "putnode_and_handle_validation_are_exact",
)
SEED_CONTEXT_FIELDS = ("seed_image", "mutated_image", "seed_image_size")
SEED_CONTEXT_FUNCTIONS = (
    "load_seed", "build_nondirectory_parent_seed", "production_seed_validates",
    "exact_capacity_boundaries_validate", "argument_validation_is_fail_closed",
    "header_corruption_is_rejected", "record_name_and_reserved_corruption_is_rejected",
    "parent_and_mode_corruption_is_rejected", "data_and_capacity_corruption_is_rejected",
)
PEER_CONTEXT_FIELDS = (
    "vfs_ramfs_buffer", "vfs_tty_buffer", "vfs_ramfs_endpoint", "vfs_tty_endpoint",
)
PEER_CONTEXT_FUNCTIONS = (
    "vfs_validate_configuration", "vfs_ramfs_result_matches", "vfs_call_ramfs",
    "vfs_call_ramfs_with_grant", "vfs_mount", "vfs_lookup", "vfs_create_node",
    "vfs_transfer", "vfs_transfer_without_grant", "vfs_getdent", "vfs_putnode",
    "vfs_malformed_version_is_exact", "vfs_directory_record_matches", "vfs_enumerate_root",
    "vfs_run_filesystem_scenario", "vfs_tty_result_matches", "vfs_call_tty",
    "vfs_receive_tty_events", "vfs_collect_tty_write", "vfs_wait_for_tty_drain",
    "vfs_write_pass_marker",
)
FIXTURE_TRANSPORTS = {
    1: (
        "ramfs_test_fixture", "ramfs_test_context", "ramfs_test_",
        FUNCTIONAL_CONTEXT_FIELDS, FUNCTIONAL_CONTEXT_FUNCTIONS,
    ),
    2: (
        "ramfs_seed_test_fixture", "ramfs_seed_test_context", "ramfs_seed_test_",
        SEED_CONTEXT_FIELDS, SEED_CONTEXT_FUNCTIONS,
    ),
    4: (
        "vfs_service_fixture", "vfs_service_context", "vfs_",
        PEER_CONTEXT_FIELDS, PEER_CONTEXT_FUNCTIONS,
    ),
}
MODEL_STATE_FIELDS = (
    "seed", "random_state", "trace_hash", "step", "trace", "production",
    "reference", "bus", "coverage", "directory_cursor",
    "model_directory_slot", "capacity_file_slot",
)
MODEL_TYPE_DIGEST = "93a26c70d7060c60dc9de8fd965111b93d4057895f39e4ee0f4d7ebe65dce702"
MODEL_MACRO_DIGEST = "f986d01714b3d8caf017f82eebd4e2d8de6de768d0878a543d7ad316c348de18"
PEER_RESULT_TYPE_DIGEST = "8e7637dec192ddb613c8fc031ffc39791653e1b0f4b8d8f043d4efe9cfc55803"
KERNEL_CONST_OBJECT_DIGEST = "49d4895ed14632490d6f1dfddfb0c18a52acd9ae7787eeaff0dea63b63f15cab"
KERNEL_RAW_OBJECT_DIGEST = "fed126e702cae04edc462115554a1f1dc14fa3a17d26415691f3275c5736df6b"
KERNEL_REPORT_MACRO_DIGEST = "9107b9699d4b7b3926278a41aec3ba3c8bc2f3e3f9a79fba685ef7359a5ef57b"
DATA_RENAMES = {
    1: {"ramfs_test_io": "test_io", "ramfs_test_vfs_endpoint": "vfs_endpoint"},
    3: {"ramfs_model_io": "model_io"},
    5: {"ramfs_test_runtime_config": "runtime_config"},
}
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

ORIGINAL_FUNCTIONS = (
    {
        0: ("protocol_result", "micros_ramfs_handle_call"),
        1: (
            "zero_bytes", "bytes_are_zero", "read_u16_le", "read_u32_le",
            "read_u64_le", "write_u32_le", "write_u64_le", "request_type_is_known",
            "result_is_defined", "micros_ramfs_decode_request",
            "micros_ramfs_build_result", "prepare_reply",
        ),
        2: (
            "endpoint_is_valid", "mode_is_canonical", "node_is_directory",
            "node_is_regular", "micros_ramfs_handle", "decode_handle",
            "initialize_empty_node", "node_handle", "resolve_node",
            "resolve_referenced_node", "names_equal", "name_equals_bytes",
            "find_free_node", "find_free_block", "handle_mount", "handle_putnode",
        ),
        3: ("validate_state", "micros_ramfs_state_validate", "micros_ramfs_state_initialize"),
        4: ("copy_grant",),
        5: ("find_child", "is_ancestor", "path_components_are_bounded", "handle_lookup", "handle_create_or_mkdir"),
        6: ("stage_read", "handle_read", "handle_write"),
        7: ("write_directory_record", "directory_entry_for_cursor", "handle_getdents"),
    },
    {
        0: ("main",),
        1: (
            "write_u32_le", "write_u64_le", "read_u32_le", "read_u64_le",
            "test_copy", "load_seed", "reset_bus", "request", "lookup_request",
            "create_request", "transfer_request", "getdents_request", "putnode_request",
            "reply_result", "reply_error_fields_are_zero", "call_expect",
            "failure_preserves_state", "initialize_unmounted", "initialize_mounted",
            "write_grant_bytes", "lookup_path", "create_node",
        ),
        2: ("seed_state_is_exact_and_invariants_fail_closed", "protocol_mount_and_result_layout_are_exact"),
        3: (
            "lookup_semantics_and_reference_limits_are_exact",
            "handles_create_mkdir_and_capacity_are_exact", "putnode_and_handle_validation_are_exact",
        ),
        4: ("read_write_sparse_and_block_capacity_are_exact",),
        5: ("getdents_cursor_and_records_are_exact",),
    },
    {
        0: ("main",),
        1: (
            "write_u16_le", "write_u32_le", "write_u64_le", "load_seed",
            "refresh_digest", "initialize_header", "initialize_entry",
            "build_maximum_data_seed", "build_maximum_entry_seed",
            "build_block_exhaustion_seed", "build_nondirectory_parent_seed", "expect_rejection",
        ),
        2: ("production_seed_validates", "exact_capacity_boundaries_validate", "argument_validation_is_fail_closed"),
        3: (
            "header_corruption_is_rejected", "record_name_and_reserved_corruption_is_rejected",
            "parent_and_mode_corruption_is_rejected", "data_and_capacity_corruption_is_rejected",
        ),
    },
    {
        0: ("run_model", "main"),
        1: ("write_u32_le", "write_u64_le", "load_seed", "model_failure", "model_copy", "record_operation"),
        2: (
            "initialize_reference_node", "initialize_reference", "reference_handle",
            "reference_resolve", "reference_is_directory", "reference_name_equal",
            "reference_find_child", "reference_is_ancestor", "reference_path_components_are_bounded",
            "reference_mount", "reference_lookup", "reference_find_free_node", "reference_create",
            "reference_find_free_block", "reference_write", "reference_read", "reference_write_record",
            "reference_record_for_cursor", "reference_getdents", "reference_putnode",
        ),
        3: ("compare_state",),
        4: (
            "read_u32_le", "read_u64_le", "request", "lookup_request", "create_request",
            "transfer_request", "getdents_request", "putnode_request", "execute_call", "prepare_bus",
            "execute_mount", "execute_lookup", "execute_create", "execute_write", "execute_read",
            "execute_getdents", "execute_putnode", "execute_stale", "execute_malformed",
        ),
        5: (
            "next_random", "build_absolute_path", "random_live_slot",
            "random_referenced_nonroot_slot", "execute_random_operation", "execute_scripted_operation",
        ),
    },
    {
        0: ("vfs_report_and_stop", "micros_service_main"),
        1: (
            "vfs_clear_bytes", "vfs_fill_bytes", "vfs_copy_bytes", "vfs_bytes_equal",
            "vfs_bytes_are_zero", "vfs_write_u32_le", "vfs_write_u64_le", "vfs_read_u32_le",
            "vfs_read_u64_le", "vfs_endpoint_is_canonical", "vfs_validate_configuration", "vfs_send_ready",
        ),
        2: (
            "vfs_initialize_ramfs_request", "vfs_ramfs_result_matches", "vfs_call_ramfs",
            "vfs_call_ramfs_with_grant", "vfs_result_has_no_fields", "vfs_mount", "vfs_lookup",
            "vfs_create_node", "vfs_transfer", "vfs_transfer_without_grant", "vfs_getdent",
            "vfs_putnode", "vfs_malformed_version_is_exact",
        ),
        3: ("vfs_directory_record_matches", "vfs_enumerate_root", "vfs_run_filesystem_scenario"),
        4: (
            "vfs_tty_result_matches", "vfs_call_tty", "vfs_receive_tty_events",
            "vfs_collect_tty_write", "vfs_wait_for_tty_drain", "vfs_write_pass_marker",
        ),
    },
    {
        0: ("micros_ramfs_service_test_launch",),
        1: (),
        2: ("ramfs_test_failure", "ramfs_test_uart_is_physically_drained", "micros_ramfs_service_test_handle_trap"),
    },
)
LONG_FUNCTIONS = (
    (0, "validate_state"), (0, "handle_lookup"), (0, "handle_create_or_mkdir"),
    (0, "handle_write"), (0, "micros_ramfs_handle_call"),
    (1, "protocol_mount_and_result_layout_are_exact"),
    (1, "lookup_semantics_and_reference_limits_are_exact"),
    (1, "handles_create_mkdir_and_capacity_are_exact"),
    (1, "read_write_sparse_and_block_capacity_are_exact"),
    (1, "getdents_cursor_and_records_are_exact"),
    (2, "header_corruption_is_rejected"),
    (2, "record_name_and_reserved_corruption_is_rejected"),
    (3, "reference_lookup"), (3, "execute_scripted_operation"),
    (4, "vfs_run_filesystem_scenario"), (5, "micros_ramfs_service_test_handle_trap"),
    (6, "render_fixture"),
)
INITIAL_SYMBOL_OWNER_MIGRATION = tuple(
    (family, owner, name)
    for family, owners in enumerate(ORIGINAL_FUNCTIONS)
    for owner, names in owners.items() if owner != 0
    for name in names
)
SYMBOL_OWNER_MIGRATION = ()
LONG_FUNCTION_MIGRATION = tuple(
    entry for entry in LONG_FUNCTIONS if entry != (0, "micros_ramfs_handle_call")
)

# BEGIN IMMUTABLE BASELINE DATA
BASELINE_BODIES = {0: {'protocol_result': '02a1a2f4f9f9938f5183c64a2ac52bd381128e5fd6332b7ebb2a21d5670e2a43',
     'micros_ramfs_handle_call': '37789d6e598c2319ee03c2910f1d401adae67ef951ead74e3375e0c93cf06861',
     'zero_bytes': '279028c262e0cf01204fbfbdd5a812cc5f86bf3815ae2cde286ba7c44f0cb241',
     'bytes_are_zero': '72f37bbc10acb9b5154f818145297080f938080684d87f69a9b8cd5d53bfa7fa',
     'read_u16_le': '3fb40615f3b36f23f72703dc6d65554426bf7ff3c9c17ce5d39ac925d3f26330',
     'read_u32_le': '02eb4e24479bbec6a75d6409d19a6a6696a39bd0130073c4a9d464202648201b',
     'read_u64_le': 'c384b11717a13a95815b07878011a7e7912d53e6c12676efd331397208bd88fd',
     'write_u32_le': '038a71c460f3bf0f0fae67ebda3f5d2fab9599fb4e0342bf8759586b5f854a34',
     'write_u64_le': '83e68f575c262a6e060ae2091246f4680a74268f40bc132e863e7530b293edea',
     'request_type_is_known': 'b143e327e7925791bfc8830a55e4b178ce3b05cb5056e712b812440491b58ec4',
     'result_is_defined': '38f116787b269aa060c0f46241d0c6a67e7ecb7922900c1c7381b1ad3be09fe3',
     'micros_ramfs_decode_request': '268945d5728d8076fc168d156c59928c971a8085f6b786e2b9c23c88211addad',
     'micros_ramfs_build_result': '999076d2628ceb072840f0147924e5955b91b8f8718c7583c48f7702cf193762',
     'prepare_reply': '369cbbca2ad6ee05b23d59db96956f1ea09dfdfb7ce99d0d32ed6795f5aba697',
     'endpoint_is_valid': '552f282bb4b35c427f1ae1c2712d59850931463cd2137681b253dd353273294c',
     'mode_is_canonical': 'ec7b5620b94fb5edd7f5c691c9bf7fb8893f132a09ddb7a9db75a3d29a8616ef',
     'node_is_directory': '96bde5bfed51df171bdf57081381acb2f3d7325b20e1940c3bed4e02e2131da4',
     'node_is_regular': 'a2290c45c2ecdd9e61336a42d14a7b38defc13e1944d529a92a68d3272663d54',
     'micros_ramfs_handle': 'ac93ef68431a97305ebf36dac4fe817f68c49605fe585e335b92399d4aa7e7a2',
     'decode_handle': 'a3a7a4e6726db11964c564ec1d5fa8d2900faef14360bf57572dc671669cb877',
     'initialize_empty_node': '03d6cf52731524f8f1e5602e2fa963056b863e4ecfbedd012a1079de3e8375b5',
     'node_handle': 'd179edf5c7874204690324c1220cb50fc22cf1f37e94af73f4faa93d4b84a637',
     'resolve_node': 'f38631020093213da1e4a6acee8265afe6c5b24a5ced564dbbbb89362b324c0d',
     'resolve_referenced_node': '03359732009aa1206ea2c985e8ee48794e68e226f1baf9fe951d3c5ca8fbe0b6',
     'names_equal': '56c66a162228b3f65618aae0eadccb3fc53378be9b2798c56b45558cef7ca4b2',
     'name_equals_bytes': '3fc51fe7dcf0cf40c42c99d488fca245ec6ac1c539bc23334cc1a353af560e74',
     'find_free_node': '38de6e31012eabf9ab5fe1f802267fb7749a21a25a20dab4167a1715c51804cd',
     'find_free_block': '2675008c1fade6271a66a2ddd6b5a16a6d1e32e4749163c5618d7d9b35d19547',
     'handle_mount': '6f51511e70d427b19fe521c4f62eaae9008f89db5c18901877be50f8e79a1fc8',
     'handle_putnode': '23ba0c353e6c831fc4c97aee029559b4c52c2afe0b8295a2cb0047b88dcbf9c2',
     'validate_state': 'efcb62312d6f1156730dbf72ca98886546677140cfd09f28decb5b9bb63d1807',
     'micros_ramfs_state_validate': '3b22df214479a8bd0722f514286671170216be7ec429ce0ef494c73f005b5353',
     'micros_ramfs_state_initialize': '7f86d0922f4c279eca42aa034ad6bb8fe3e9c4f078c38c9506cda80ab23251ad',
     'copy_grant': '1a250aeb7572f43bc344161b4f9ba4be4f523f478ca463b1ca2326de098e48be',
     'find_child': 'c1acaf574a1e85a30e4e1830ea8588b7423444e3de90c12c15421173ac1c54d0',
     'is_ancestor': 'd1f8816e9100c582731e97f5fd3640abec2a1bd5b14bd683dff69ef69e1a57ad',
     'path_components_are_bounded': '369415c72cb29c1ad018280d8e39c8404374d4314acb4400fc421eaca7d7f3c8',
     'handle_lookup': '236990ef8e13e262f3ce1680c5b3a50a87244cd514a6c5a3e9e126b3fb458c02',
     'handle_create_or_mkdir': '48bc2e1b399a744f23eb1227b3976967f9e086a15ffec8afda219d2471029417',
     'stage_read': 'f564ca6bd68bb574d046a5f0b6f5ab3920e97e46ad974327615b70bbd9e33004',
     'handle_read': '0d3ce1c35f87c8942e85c3a3dae1534157fb272184a1beb21f8f2834fcd3b4fd',
     'handle_write': '73487f546b872418853c892556e177db2cfbf32207e0d6221a88fa567f611450',
     'write_directory_record': '09580424b70054cfcabb99644164b61fd419d79ec8335bc9294419594f1d7950',
     'directory_entry_for_cursor': '03c93cc90d575787b59b84d61cdecea22bb69ac3752edc9c6ed8d259ae279de6',
     'handle_getdents': '2423ce29c61f0a3e21ccba8973c24188965aff433ac42de0c7530e9ac5d32d21'},
 1: {'main': '4172ccab4d189e2a8160ad4ddd4e75b79e1ee1345bee0b710752cf8d17ed942c',
     'write_u32_le': '038a71c460f3bf0f0fae67ebda3f5d2fab9599fb4e0342bf8759586b5f854a34',
     'write_u64_le': '83e68f575c262a6e060ae2091246f4680a74268f40bc132e863e7530b293edea',
     'read_u32_le': '02eb4e24479bbec6a75d6409d19a6a6696a39bd0130073c4a9d464202648201b',
     'read_u64_le': 'c384b11717a13a95815b07878011a7e7912d53e6c12676efd331397208bd88fd',
     'test_copy': '94473a228e0e7cadaa67f5c8c6d1a08025bb5e0b67e62b14aa0d7fed0bbc666c',
     'load_seed': '28d1a0bfd6906bce57fbff31557c0cc5822d1b2b782ab82e3fa7da8f0e563ef7',
     'reset_bus': '84192016ff75f136dadf5297744126c3b8f96aef9652dd02f85d14980eb40775',
     'request': 'f38e219d702ca66047a9fa7d011e8c2175aca4cd6e4da46e6e4fa46b5ac8da86',
     'lookup_request': '81919fd546486ba4ea7b645a7429615e9dd84e288b24eea94a86a7420b356cf5',
     'create_request': 'ba3e9fa80f7b5a46720de92635c4466a2c06b9360420a4c2b4c0c1f955d5b5fd',
     'transfer_request': '9391bc7b3766e0971ef49c82792740ec536e1f15ca0b582f2636d09ebd9b24b7',
     'getdents_request': 'e5c08500807e6a744e68a5ba392ccd82023e20965a9c104ba1349fb556507c2d',
     'putnode_request': '7c2dcd1fedfecd1be3d074214f6e6e53459b45aee8c6b4580dda498df844f705',
     'reply_result': '2da0b8f999164dde6195051a77f9b339bc1550b951dff185b300cb4429f6f656',
     'reply_error_fields_are_zero': 'bf041c9cbaac4043e9f7a5179eba95157fda81efc8ee139deb7a14d3c92d1feb',
     'call_expect': '8198063587e70a9e698afb87574ffeb44eb8b0236cb7ca305a2eb87063b83186',
     'failure_preserves_state': '4ac4286d87d5433126b1d3f4fa2ba02907043d9b5209e0945d3c604e2d62fd5f',
     'initialize_unmounted': '9dd23b21a11497437a52b0025f028bfd98f75b5ea5c76686afed55122dfbdd34',
     'initialize_mounted': 'afcc46d00449115c9aa77a6f91d5d990479b24322005b9a345159c9d265539d3',
     'write_grant_bytes': '6e32ddf54af0c419bba8d5b712670ee1627bcf455db6495675992be71fb03d31',
     'lookup_path': '5fc596c3eb176e5013f737812178cf231778c664f38ecda28217d25d972ecf2b',
     'create_node': '2b158beb0b2778916c6e01db101515156c026e6c2dd668a04458d8b55e3788c3',
     'seed_state_is_exact_and_invariants_fail_closed': 'be35db4eec06bd58ceca4dc8825012db6c5c9684c843a0bc515f9921f86720e7',
     'protocol_mount_and_result_layout_are_exact': '6163bd803c26975a4e6e2600c72d30d863232b10832f8ecce928f2e7db0bac75',
     'lookup_semantics_and_reference_limits_are_exact': 'a03944534203fd748fa510df2e73e9e2690bb5dc008f68a0ee5d04afa4118813',
     'handles_create_mkdir_and_capacity_are_exact': '91e2aa3f4e74375d5695ff07cbe5eb908e91d013a58020ce33f72a17daa06bb7',
     'putnode_and_handle_validation_are_exact': '8047c02af1b546e96491a36b9504a6d0e3d7e929eb17ddf3f3989382ee45a1e5',
     'read_write_sparse_and_block_capacity_are_exact': 'cb49142740cfc56031837425ff597722765668a7579b00b4bd406cfc6a49c33b',
     'getdents_cursor_and_records_are_exact': '26f3e13b0a86ed6a21f706f42750013a9d052bfbdb6050a0fd453295a0300f99'},
 2: {'main': '80e74de255c6212d56b8b368e02c45a80fd24ce0d5bfdc396082485f8655a86d',
     'write_u16_le': 'ac1238a2cc093d2bb82a69fa68e17fb1263a0f10e5fc0e40b60bd448a1e647d6',
     'write_u32_le': '038a71c460f3bf0f0fae67ebda3f5d2fab9599fb4e0342bf8759586b5f854a34',
     'write_u64_le': '83e68f575c262a6e060ae2091246f4680a74268f40bc132e863e7530b293edea',
     'load_seed': '28d1a0bfd6906bce57fbff31557c0cc5822d1b2b782ab82e3fa7da8f0e563ef7',
     'refresh_digest': '2cbb52d8e01c5fdf93bbfeb97abffb19765d06005353fbb5f2e00da39daf0d3b',
     'initialize_header': '3a485ae26a6880f1a05e625a392d6363a15e7d624fc33016e2756d33fb232db2',
     'initialize_entry': '3d0435fbfd3f890268ab2b78163dd2d65417a8525cbeea8b0e21555adead6717',
     'build_maximum_data_seed': 'a146670da589763af9592980776a6763ee466a028b0fdf3bdbbf3b145cdbb1e9',
     'build_maximum_entry_seed': '5e0735e979a58c571540caffd02f28bdd87be51540e442687cf55a17065c2fae',
     'build_block_exhaustion_seed': '7d60484a60f20088d4b0beecf68887b4b3b2062463c9ad9404f94cfffd00c3a5',
     'build_nondirectory_parent_seed': '2c907ba08383403e92e4df390fc3562533ae428fc7dd4eb93724039a16980e1d',
     'expect_rejection': 'b14801fc974b3fb88dd4904d9d6faaadaebdc4c07d7ebae1435a3a3dac84db21',
     'production_seed_validates': '708922435957610d5e5fd942587273fe2fa005d36a2510f46126b9c15a3ced1e',
     'exact_capacity_boundaries_validate': '6ee594a881f45603a2cf52647ef466410c9595eeba36d111c7dc87f513672ecb',
     'argument_validation_is_fail_closed': '411ad66b01ec5afeab840b31a923c094ad5825b3f2877f3f22757c1cd793a62c',
     'header_corruption_is_rejected': 'f036a1e1ad92f956c5b24a3af44bc181415bf70a50285ede2a02d4d6da979152',
     'record_name_and_reserved_corruption_is_rejected': '271e81764ffe86b957b3be39c7f0723c8ebf16b2302426bb812b0d4d0edb8e56',
     'parent_and_mode_corruption_is_rejected': 'f03d003f7a87e89ca563e52b62f1f2b90b30d04d421ef293b7226ec069201a90',
     'data_and_capacity_corruption_is_rejected': 'b097141b90aa4e0480324c255b5642c73c255fae7e456024875849f731aa36a6'},
 3: {'run_model': '501fbb1a87fd8cbfd6be86f63f58f267e2eb6a2d841b029c08faa4a5c7df4a37',
     'main': 'e574fc574fe70355cf1a800cd3ddec9523e0b8f5e12cd50c9cb69bed560c32bc',
     'write_u32_le': '038a71c460f3bf0f0fae67ebda3f5d2fab9599fb4e0342bf8759586b5f854a34',
     'write_u64_le': '83e68f575c262a6e060ae2091246f4680a74268f40bc132e863e7530b293edea',
     'load_seed': '28d1a0bfd6906bce57fbff31557c0cc5822d1b2b782ab82e3fa7da8f0e563ef7',
     'model_failure': '0f8c6b0c8022cbfe7274fb8794b4c154ebe7c9d51428c26c52563dbab72aaa9d',
     'model_copy': '77a840b6d3c71dc1ae4a47c3d0ec436a85c5507d4fc8e73bdad41a81fece1895',
     'record_operation': '41a2cbb056606b0dca54bf43863f597410c76e014c0c1d7407d2d61647095794',
     'initialize_reference_node': 'c86f574b231127291f90ab34dfccbfd89be155ffd30bd129ec416050c060c294',
     'initialize_reference': '5e2c6287355852a6af5a1483c64463f19b688e62de7dabe844f212b143e5a4a3',
     'reference_handle': '41cc5c8c572d49ddcab81c857a8649a6a32e7f08e08adef3cfec43e8c95a9449',
     'reference_resolve': 'e8c6ac9041f7020e9593033f5986cc2e16273c815d0840a0382c4ccfe6095bdb',
     'reference_is_directory': '96bde5bfed51df171bdf57081381acb2f3d7325b20e1940c3bed4e02e2131da4',
     'reference_name_equal': '2a0da4740194c50ee0986d5747aa3810030a897b74b5848596e61791fdecec95',
     'reference_find_child': '7f123036dc445b8dc7fcf7434a1a8f14a9ffd92c69b796159e5f66a9e7234786',
     'reference_is_ancestor': '25e277823ffc12d752a532512ba275d3cd32bfe59da6483fa2eb88ae11acabd2',
     'reference_path_components_are_bounded': '369415c72cb29c1ad018280d8e39c8404374d4314acb4400fc421eaca7d7f3c8',
     'reference_mount': 'e7868fcf0ff5e70b2fb9912efe4f30d2e9794f3b4e8de70b7149b024575709d7',
     'reference_lookup': '1225bdd4c2198ee82cf9bbfcea5f0050337ac170be56bf7a1ae4c76a68c2d5ae',
     'reference_find_free_node': '8cfbeb991974b83f40f8e742fc74206dd0caa475ceeb5501b8140f8279ff3865',
     'reference_create': 'b6bc2845e6d5f5ac5f7e3873d4735e51a4e0ebf0ff142888cd6bd7516ccc7b75',
     'reference_find_free_block': '5844da5eea1f5ea97df49ab01d0b57c357a1653f09e0ac02c01852729bc0b439',
     'reference_write': '4538ad7751b6a3e624f5d957fb15b2318f69c33d8d2626c0b12020a05c0b397a',
     'reference_read': '7a31a0c48568964832bec37e60ab587c3e3bf80fd2cfbecf76559466d7f2fdda',
     'reference_write_record': '7834702609f8b8b30220d70ee458a16202aa408aab556d6f60701a8082b63c80',
     'reference_record_for_cursor': '639e17a65ece2cf29ac73b75d01c185246c9d49a9b59c4a83538c75d72a29583',
     'reference_getdents': '677af65eb19874d10d10154e4900969b2134a12420fa8ffb9408e2b69f9b697f',
     'reference_putnode': '6372d18953003f8d3e4d457c02c5e4fd5c24aad43de3c440dc7031eca869a8f0',
     'compare_state': '1b95a3a819a421966588745e6978e88a7d07961df484ff2dd2d06289b9fde6dc',
     'read_u32_le': '02eb4e24479bbec6a75d6409d19a6a6696a39bd0130073c4a9d464202648201b',
     'read_u64_le': 'c384b11717a13a95815b07878011a7e7912d53e6c12676efd331397208bd88fd',
     'request': 'afdf4e47832015fd5d44f46509ede6e1825ba356e63ca398ab63cf8beb458408',
     'lookup_request': '10076101af4cce06a6bf30dfce54f983617fa0ecc5ecfe2403bdf0286971ef97',
     'create_request': '3b69e579384ee794a3523673f4964657826ad0d813aac84868a9921d51aca3d9',
     'transfer_request': 'db9c4760f3a7547516f66b2808556400a0b1201fc03053d7fd832440ce04b986',
     'getdents_request': 'f3b1ed3b2712a8670e78a7f4ae2d1d64a0cd9445e4280b9c33f48f974ed4137c',
     'putnode_request': '7c2dcd1fedfecd1be3d074214f6e6e53459b45aee8c6b4580dda498df844f705',
     'execute_call': 'd883aeddb07f6f3bcbf6e06be7ad18bfceda46bacb27d009808e12bad6baff5f',
     'prepare_bus': 'bcb84722158de46453002f996bffe7ce2f824da1b92e14fb6bf5b3c2cbfdc121',
     'execute_mount': '782027b738ade0ee3fb6b3c17dee422e06669e09985504ace4ed3bed3e800b0e',
     'execute_lookup': '29eccbd86533683e417c6eae4876f90d0ad511ae591e63f0d37e8751eeef5eac',
     'execute_create': 'a92724d2299ab5456658a8848e3f45374e6b5c094ed385fff67bb1eb2a2db703',
     'execute_write': '80b47286b69daf86b34d06d05d9769a1ded309f7af7f9e42a26a0e6a009980a7',
     'execute_read': '7df8c73a6da7b664cf105d6f9542e0ca068f692fa62a7538fc63d52352dd1f6e',
     'execute_getdents': '2fe5173d47c14e49b60edd9ade75eb445f73c77efa1ffc6a4d490a05a88f09be',
     'execute_putnode': '08a3e29e68cba118ef0e9e2d6dfd54e09bb78addfa808a09c915c04d252218f0',
     'execute_stale': '5c5f859d3ec12ac67013d2a2ca939ee26a205851a2a4927b7930a3a19a3c4d10',
     'execute_malformed': '65a0bf9a34cd63367a326a12af6e0692fe26a090452de86e9644ec548651e4b8',
     'next_random': '85aea9d83da050d798543a10aeb50c6a41c045f9bbc15d92729b298d6a2d0bd3',
     'build_absolute_path': '41f017982996fc425759201b4bac9c694af5b2e592d15618cfc04e350c83105b',
     'random_live_slot': 'b093e539e31b2dfcb5a64259aab1a2685d13d46da78263aaeb9a2846c3ccbd9e',
     'random_referenced_nonroot_slot': '0ea2fdcac9097929ba681458e39e13e8737af093b3434e4c4ce62400285be5c8',
     'execute_random_operation': '2246692300466af8b5998e99573d6f2fc4781c1fb63b838a9f6df4d3715b0b18',
     'execute_scripted_operation': 'f1ab1b052bd9c34e1511dab6cc4a21b28b93108bfa29d2870d17667747fce6f4'},
 4: {'vfs_report_and_stop': '6954b2e3d8de3e63b3e45beac6fddf95042c3213ccd967c5a8716734272391ea',
     'micros_service_main': '09bfa9e2802bdf44b59f7a4fae49db7616fe117592dd07c67f555b335c8628f1',
     'vfs_clear_bytes': '279028c262e0cf01204fbfbdd5a812cc5f86bf3815ae2cde286ba7c44f0cb241',
     'vfs_fill_bytes': 'fea55b2c07147bacb45ab0e45e65aac5e9a02c8ee13a73dc40d4c211e444eb38',
     'vfs_copy_bytes': '5c15cd98409a754d782f3c55c4e18eb4f2e6acb77ce9dcf128c64486f4f4e389',
     'vfs_bytes_equal': '86b7b6c20b6a65e423b2413053a7fc3f9cf563a634f4a0b189bcba3140e1b66e',
     'vfs_bytes_are_zero': '72f37bbc10acb9b5154f818145297080f938080684d87f69a9b8cd5d53bfa7fa',
     'vfs_write_u32_le': '038a71c460f3bf0f0fae67ebda3f5d2fab9599fb4e0342bf8759586b5f854a34',
     'vfs_write_u64_le': '83e68f575c262a6e060ae2091246f4680a74268f40bc132e863e7530b293edea',
     'vfs_read_u32_le': '02eb4e24479bbec6a75d6409d19a6a6696a39bd0130073c4a9d464202648201b',
     'vfs_read_u64_le': 'c384b11717a13a95815b07878011a7e7912d53e6c12676efd331397208bd88fd',
     'vfs_endpoint_is_canonical': 'b6f0a941e931113f598472ef58d04212b46514291f203b6f0bc353a9ed8d7be6',
     'vfs_validate_configuration': '25b7db0678e1cff8d12abe099170f2f87ad8f589d970d1ed743822de9ee14e8f',
     'vfs_send_ready': '70553fc5607a633706657f6ab7f7fc54e6b02dd62f0d95f4d4341134d96b33b5',
     'vfs_initialize_ramfs_request': '82f78d4f2b1a71d1ff7ea0271ffb1fbee508d8e1574fdbedee7ee6186e580efa',
     'vfs_ramfs_result_matches': '4b19da9488d5643abb469b64925948187ecb17fd680d73faa04753c400f239e9',
     'vfs_call_ramfs': 'bca3d0632e7b4509cd313e48cd9f979ba9dfe09b9df718cd1441af53003bc2ee',
     'vfs_call_ramfs_with_grant': 'deac3dd128764c80857393a3b0b4a62735affa8cc279bedbd2608d09ba6b0564',
     'vfs_result_has_no_fields': 'debe0a0f66045cb01d823b39be595672d665b08a674f732da878e429e1f43b44',
     'vfs_mount': 'e5483a60387f03ca96424771fb8a1569b6011f3c217c2f09b2099d70d175d55d',
     'vfs_lookup': '27e043b1839b59ba1694107562e3b14934b59f775496de4df6aca24c70b845cf',
     'vfs_create_node': '11f5f7a0b6a9889cffee03b4d487b2a9998c6160505fb900eb225af29989c2f8',
     'vfs_transfer': 'c6b327dc8e2cd391f87f3f811a340334b59db35cddaf651eadb77ef16b7a8371',
     'vfs_transfer_without_grant': '9ea4f73b1871fba6e183d3febdb6ee36fd47715a7c7ea85cffc7ed03c6267d7c',
     'vfs_getdent': 'da36d6f9cc11e2ebf33d5de26e25b51a32cdb4f60ddab249263dc10981fcb027',
     'vfs_putnode': '55d0ebf41c2ce4d0b8d6c55f2a42cd574caa7e513b44e3e895fcd6811d35d253',
     'vfs_malformed_version_is_exact': 'eb62a2c9f1cc18f5a3371de7883386656eabc86583444437188b83e51fbe7187',
     'vfs_directory_record_matches': '4f45dbb0bea2ba36e5f9e0ac85da054e00600c1153c15f97554948d7bc817b0a',
     'vfs_enumerate_root': 'dc7b0128c4cc0f5d2fb488271603a10a594b21c856a7b8b9a5049c9f7ca1dbb2',
     'vfs_run_filesystem_scenario': '2cc804608c1e70525c4bcea616d78cd9c4795e53f48be07a358ce635d7ac9a53',
     'vfs_tty_result_matches': 'd2ec33660a27e91bebd20bc8f6ad6b3b0d24f083ba9be12e5ca97c205a5b1e0a',
     'vfs_call_tty': '120a070e796a94064f5691754b140aed36fdfa95bb4463071efd037435fc261d',
     'vfs_receive_tty_events': '649e937afdae222c1af535e86c7b80ac392b6d550e44d9d70e87e5047fb4461e',
     'vfs_collect_tty_write': '816a9c0471038c640461eab3ff2ecb8cba08389325110cb017f720f94f71b45c',
     'vfs_wait_for_tty_drain': 'd0c5dde2109e4cd1d0bea4bd2d4524defedee73ddbbeecab5fed0b1af9690eca',
     'vfs_write_pass_marker': '3401924ee9ccf3beeb5657e18bda4205c846720d5f91c11f470d665e28f4e8d7'},
 5: {'micros_ramfs_service_test_launch': '2fca03ca2be1cec2e0aff533acfb16127c4053676e800a8bb943202e1692aa16',
     'ramfs_test_failure': 'b76e2d1cebebf4207b4ea15c333eef3fb118136157261f5111e5aa4dcee39926',
     'ramfs_test_uart_is_physically_drained': 'a8c841f0362f090d385747210d5d2dd0471372c072d15c5d621c1a027d6af569',
     'micros_ramfs_service_test_handle_trap': '2c918cf62a9ca315989972bd1b427468d5ce604715f971d805cd8f88e3e7015b'}}
STABLE_HASHES = {'include/micros/ramfs.h': '4c9a87e0eb25212e5cd5321a722f004000290aa10c2a6b96681ce2a1f20b963f',
 'servers/ramfs/ramfs_core.h': '0dcf13f31752d5477b42f20ff00dc95f4eab949cdcfe06cbe8ae106b935a9138',
 'servers/ramfs/ramfs_seed.h': 'fe97c957ef647b278c43f230f8585f78ba81c4a721bfd8898607729d49c44e69',
 'servers/ramfs/ramfs_seed.c': '92ce45ab757f970f516bdae6752541acc7de6e090eb8d9a49df9e606569f616a',
 'servers/ramfs/ramfs_service.c': '14dce56aca9b55dafe0e99689c21f5dfa7d9ed36e7cab28c1206bc22330f2fd3',
 'servers/ramfs/ramfs_embedded_seed.h': 'ec3d7a57e16b0f2989d6880f6590a4602a93fe038e8cfdf47a56dd31d64bf237',
 'servers/ramfs/seed.json': 'f2360b17b5e8612f28a5a0a6ca8423ca97e8356fa9d65957efe44630a3851dcf',
 'servers/ramfs/seed/etc/motd': 'd40726b8bc6eeb13c6013855e2d7c6779394a7397c3b8b9a6ced1e12259a763f',
 'tools/generate_ramfs_seed.py': '64d1085d02f59d82c0c3bd0c1c33eea564547b28b6df793b30015012345deac5',
 'tools/embed_ramfs_seed.py': '4ea30c2ce9c62559310baa5c876ca55667bb33166e8f0505872cd9e4be17d774',
 'tests/host/test_generate_ramfs_seed.py': '40a470a6c3f50970705141e53bc05e0c27d69df72a86cb73789e9f9e4f750fc0',
 'tests/host/test_embed_ramfs_seed.py': 'ee5378b1d789cc3202c5a14620d59c841dbdf03111e272e09fe29af7cf9ae242',
 'tests/qemu/ramfs_service_protocol.h': 'd92b9f3f44df4d7e4dce8d4eec3e90e73b897055cf0c082c7907a0410dc29a6f',
 'tests/qemu/ramfs_service_report.S': '0a0140181542e55a0db0706af21dba431ca448ab40f4fd1bfdb2bb23a3843417',
 'kernel/ramfs_service_test.h': 'b773f072d86fc818f0f7e4e97383f9e9da2c37af8c8e94bf86a772f1fe475cc1',
 'kernel/ramfs_service_test_fixture.h': '8fd2d9c2475627b6b615e8e4393897425d430e52930bc115deb3963a63c8af9a',
 'CMakePresets.json': 'eae6c86ce5ac8f0e1b39605ebbd6816135558f0b4f8ea293b3680d7ecb79df86'}
BASELINE_GENERATOR = '4300e26d4a506c216874425299a09ad1b28b525d6f94d9e78d0a2e0081f06259'
BASELINE_GENERATOR_OUTPUT = 'feca6c3c32b0f87883c6ef7a67cf4b410445550e2314b10c1740725c29212707'
# END IMMUTABLE BASELINE DATA


def source(path):
    return (ROOT / path).read_text()


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


def restored_kernel_objects(objects):
    restored = []
    for declaration in objects:
        if "ramfs_test_runtime_config =" in declaration:
            if not declaration.startswith("const struct micros_bootstrap_runtime_config "):
                raise AssertionError("runtime configuration must remain one const object")
            declaration = "static " + re.sub(r"\bramfs_test_runtime_config\b", "runtime_config", declaration)
        elif not declaration.startswith("static const "):
            raise AssertionError("kernel profile/service/policy data must stay static const")
        restored.append(declaration)
    return tuple(restored)


def family_paths(family):
    return tuple(SOURCE_SETS.values())[family]


def alias_rows():
    return tuple(
        (paths[0], (*paths[1:], PRIVATE_HEADERS[family]))
        for family, paths in enumerate(SOURCE_SETS.values())
    )


def owned_definitions(family):
    return {
        (path, name): lines
        for path in family_paths(family) if (ROOT / path).is_file()
        for name, lines in c_function_definitions(source(path)).items()
    }


def original_location(family, owner, name, definitions):
    path = family_paths(family)[
        0 if (family, owner, name) in SYMBOL_OWNER_MIGRATION else owner
    ]
    prefix = FAMILY_PREFIXES[family]
    candidates = [
        (candidate, spelling) for candidate, spelling in definitions
        if spelling in {name, prefix + name}
    ]
    if len(candidates) != 1 or candidates[0][0] != path:
        raise AssertionError(f"wrong/missing original owner: {(family, owner, name, candidates, path)}")
    return candidates[0]


def normal_names(family):
    return {
        FAMILY_PREFIXES[family] + name: name
        for names in ORIGINAL_FUNCTIONS[family].values() for name in names
    }


def fixture_transport_tokens(family, name, tokens):
    tag, getter, prefix, fields, context_functions = FIXTURE_TRANSPORTS[family]
    tokens = list(tokens)
    if name in ("main", "micros_service_main"):
        binding = list(body_tokens(
            f"struct {tag} *fixture = {getter}();"
        ))
        if tokens[1:1 + len(binding)] != binding:
            raise AssertionError("main must borrow its sole family-typed fixture once")
        del tokens[1:1 + len(binding)]
    functions = {
        spelling for original in context_functions
        for spelling in (original, prefix + original)
    }
    output = []
    index = 0
    while index < len(tokens):
        token = tokens[index]
        if tokens[index:index + 3] == ["fixture", "-", ">"]:
            if index + 3 >= len(tokens) or tokens[index + 3] not in fields:
                raise AssertionError("unknown family fixture field")
            output.append(tokens[index + 3])
            index += 4
        elif token in functions and tokens[index + 1:index + 2] == ["("]:
            if tokens[index + 2:index + 3] != ["fixture"]:
                raise AssertionError("fixture transport must pass the same borrowed context first")
            delimiter = tokens[index + 3:index + 4]
            if delimiter not in ([","], [")"]):
                raise AssertionError("invalid family context argument")
            output.extend((token, "("))
            index += 4 if delimiter == [","] else 3
        else:
            output.append(token)
            index += 1
    return tuple(output)


def model_transport_tokens(name, tokens):
    tokens = list(tokens)
    if name == "run_model":
        bindings = list(body_tokens(
            "struct ramfs_model *selected = ramfs_model_context(); "
            "const struct ramfs_model_seed *seed_info = ramfs_model_seed_context();"
        ))
        if tokens[1:1 + len(bindings)] != bindings:
            raise AssertionError("model driver must borrow distinct typed model and seed stores")
        del tokens[1:1 + len(bindings)]
    output = []
    index = 0
    seed_fields = ("seed_image", "seed_image_size")
    while index < len(tokens):
        token = tokens[index]
        if name == "run_model" and tokens[index:index + 3] == ["selected", "-", ">"]:
            if index + 3 >= len(tokens) or tokens[index + 3] not in MODEL_STATE_FIELDS:
                raise AssertionError("unknown running-model field")
            output.extend(("model", ".", tokens[index + 3]))
            index += 4
        elif name == "run_model" and tokens[index:index + 5] == ["sizeof", "(", "*", "selected", ")"]:
            output.extend(("sizeof", "(", "model", ")"))
            index += 5
        elif name == "run_model" and token == "selected":
            output.extend(("&", "model"))
            index += 1
        elif (
            name == "run_model" and tokens[index:index + 3] == ["seed_info", "-", ">"]
            or name == "load_seed" and tokens[index:index + 2] == ["seed_fixture", "."]
        ):
            prefix_length = 3 if name == "run_model" else 2
            field_index = index + prefix_length
            if field_index >= len(tokens) or tokens[field_index] not in seed_fields:
                raise AssertionError("seed access must stay in its separate two-field store")
            output.append(tokens[field_index])
            index = field_index + 1
        else:
            output.append("__FILE__" if name == "model_failure" and token == "file_name" else token)
            index += 1
    return tuple(output)


def require_model_failure_macro(macro):
    tokens = list(body_tokens(macro))
    if tuple(tokens) != body_tokens(MODEL_EXPECT_WITH_FILE):
        raise AssertionError("model failures must carry the actual caller file/line and immediate exit")
    index = tokens.index("__FILE__")
    if tokens[index:index + 3] != ["__FILE__", ",", "\\"]:
        raise AssertionError("unexpected caller-file argument boundary")
    del tokens[index:index + 3]
    restored = tuple("model_failure" if token == "ramfs_model_model_failure" else token for token in tokens)
    if digest_bytes(json.dumps(restored, separators=(",", ":")).encode()) != MODEL_MACRO_DIGEST:
        raise AssertionError("model assertion behavior differs beyond verified caller-file transport")


def peer_read_tokens(name, tokens):
    if name != "micros_service_main":
        return tokens
    output = []
    index = 0
    while index < len(tokens):
        if tokens[index:index + 3] == ("vfs_initialized_data_value", "(", ")"):
            output.append("vfs_initialized_data")
            index += 3
        else:
            output.append(tokens[index])
            index += 1
    return tuple(output)


def dispatcher_body_tokens(caller, phase):
    caller = list(body_tokens(caller))
    phase = list(body_tokens(phase))
    call = list(body_tokens(
        "error = dispatch_request(state, &request, io, &fields, &result);"
    ))
    locations = [
        index for index in range(len(caller))
        if caller[index:index + len(call)] == call
    ]
    if len(locations) != 1:
        raise AssertionError("dispatch must return into the caller's existing error guard exactly once")
    initialization = list(body_tokens(
        "enum micros_ramfs_core_error error = MICROS_RAMFS_CORE_OK;"
    ))
    terminal = list(body_tokens("return error;"))
    if phase[:1] != ["{"] or phase[-1:] != ["}"]:
        raise AssertionError("dispatch must have one bounded function body")
    if phase[1:1 + len(initialization)] != initialization or phase[-1 - len(terminal):-1] != terminal:
        raise AssertionError("dispatch must retain typed error initialization and return its exact error")
    inner = phase[1 + len(initialization):-1 - len(terminal)]
    expanded = list(body_tokens("error = MICROS_RAMFS_CORE_OK;"))
    index = 0
    while index < len(inner):
        token = inner[index]
        if inner[index:index + 3] == ["request", "-", ">"]:
            expanded.extend(("request", "."))
            index += 3
        elif inner[index:index + 2] == ["*", "result"]:
            expanded.append("result")
            index += 2
        elif token in ("request", "fields", "result"):
            expanded.extend(("&", token))
            index += 1
        else:
            expanded.append(token)
            index += 1
    position = locations[0]
    return tuple(caller[:position] + expanded + caller[position + len(call):])


def original_body_digest(family, name, text):
    tokens = body_tokens(text)
    if family == 0 and name == "micros_ramfs_handle_call" and (0, name) not in LONG_FUNCTION_MIGRATION:
        phase = c_function_body(source(family_paths(0)[0]), "dispatch_request")
        tokens = dispatcher_body_tokens(text, phase)
    if family in FIXTURE_TRANSPORTS and (ROOT / PRIVATE_HEADERS[family]).exists():
        tokens = fixture_transport_tokens(family, name, tokens)
    if family == 3 and (ROOT / PRIVATE_HEADERS[3]).exists():
        tokens = model_transport_tokens(name, tokens)
    if family == 4 and (ROOT / PRIVATE_HEADERS[4]).exists():
        tokens = peer_read_tokens(name, tokens)
    renames = normal_names(family)
    if family in DATA_RENAMES:
        constants = DATA_RENAMES[family]
        for index, token in enumerate(tokens):
            if token in constants and (
                tokens[index - 1:index] == (".",)
                or tokens[max(0, index - 2):index] == ("-", ">")
            ):
                raise AssertionError("a fixture global alias cannot rename a structure member")
        renames.update(constants)
    normalized = tuple(renames.get(token, token) for token in tokens)
    return digest_bytes(json.dumps(normalized, separators=(",", ":")).encode())


def fixture_parameter_matches(family):
    tag, _, _, _, context_functions = FIXTURE_TRANSPORTS[family]
    definitions = owned_definitions(family)
    result = []
    for owner, names in ORIGINAL_FUNCTIONS[family].items():
        for name in names:
            if name not in context_functions:
                continue
            path, spelling = original_location(family, owner, name, definitions)
            matches = re.findall(
                rf"\b{spelling}\s*\(\s*struct\s+{tag}\s*\*\s*fixture\s*[,)]",
                c_lexical_text(source(path)),
            )
            result.append((spelling, len(matches)))
    return tuple(result)


def restored_cmake(text):
    for family, (name, paths) in enumerate(SOURCE_SETS.items()):
        blocks = cmake_commands(text, "set", name)
        if not blocks:
            continue
        if len(blocks) != 1 or body_tokens(blocks[0]) != body_tokens(f"set({name} {' '.join(paths)})"):
            raise AssertionError(f"wrong RAMFS source set: {name}")
        insertion = blocks[0] + "\n\n"
        if text.count(insertion) != 1:
            raise AssertionError(f"unexpected source-set insertion boundary: {name}")
        text = text.replace(insertion, "", 1)
        variable = "${" + name + "}"
        if text.count(variable) != (3, 1, 1, 1, 1, 1)[family]:
            raise AssertionError(f"wrong source-set consumer count: {name}")
        text = text.replace(variable, paths[0])
    if digest_bytes(text.encode()) != BASE_CMAKE_DIGEST:
        raise AssertionError("CMake contains changes outside the exact RAMFS source operands")
    return text


class RamfsSourceLayoutTest(unittest.TestCase):
    maxDiff = None

    def test_exact_frozen_contract_and_migrations(self):
        self.assertEqual((6, 32, 6), (len(SOURCE_SETS), sum(map(len, SOURCE_SETS.values())), len(PRIVATE_HEADERS)))
        self.assertEqual((45, 30, 20, 54, 36, 4), tuple(
            sum(map(len, owners.values())) for owners in ORIGINAL_FUNCTIONS
        ))
        self.assertEqual((180, 17), (len(INITIAL_SYMBOL_OWNER_MIGRATION), len(LONG_FUNCTIONS)))
        self.assertEqual(189, sum(map(len, BASELINE_BODIES.values())))
        for pending, initial in (
            (SYMBOL_OWNER_MIGRATION, INITIAL_SYMBOL_OWNER_MIGRATION),
            (LONG_FUNCTION_MIGRATION, LONG_FUNCTIONS),
        ):
            self.assertEqual(len(pending), len(set(pending)))
            self.assertLessEqual(set(pending), set(initial))

    def test_every_original_definition_has_one_exact_owner(self):
        for family, owners in enumerate(ORIGINAL_FUNCTIONS):
            definitions = owned_definitions(family)
            for owner, names in owners.items():
                for name in names:
                    with self.subTest(family=family, name=name):
                        original_location(family, owner, name, definitions)

    def test_activated_families_require_complete_sets_and_private_headers(self):
        cmake = source("CMakeLists.txt")
        for family, (name, paths) in enumerate(SOURCE_SETS.items()):
            active = any(
                entry not in SYMBOL_OWNER_MIGRATION
                for entry in INITIAL_SYMBOL_OWNER_MIGRATION if entry[0] == family
            )
            with self.subTest(family=family):
                present = tuple(path for path in paths if (ROOT / path).is_file())
                self.assertEqual(paths if active else paths[:1], present)
                self.assertEqual(active, (ROOT / PRIVATE_HEADERS[family]).is_file())
                blocks = cmake_commands(cmake, "set", name)
                if not active:
                    self.assertEqual((), blocks)
                    continue
                self.assertEqual(1, len(blocks))
                self.assertEqual(
                    body_tokens(f"set({name} {' '.join(paths)})"),
                    body_tokens(blocks[0]),
                )
                self.assertEqual((3, 1, 1, 1, 1, 1)[family], cmake.count("${" + name + "}"))

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
        macro = header[header.index("#define EXPECT_TRUE"):header.index("\nenum {")].strip()
        self.assertEqual(
            "dc1fbf95f12b7896694addb93aa8603ed20b7bd57022a9d01f7b8c7342234fb6",
            body_digest(macro),
        )
        expected_objects = (
            "static struct ramfs_test_fixture test_fixture;",
            "const micros_endpoint_t ramfs_test_vfs_endpoint = UINT32_C(0x00001005);",
            "const struct micros_ramfs_io ramfs_test_io = { "
            ".copy = test_copy, .context = &test_fixture.bus, };",
        )
        for path in paths:
            expected = expected_objects if path == paths[1] else ()
            self.assertEqual(
                tuple(map(body_tokens, expected)),
                tuple(map(body_tokens, file_scope_objects(source(path)))),
                path,
            )
        self.assertEqual(
            body_tokens("{ return &test_fixture; }"),
            body_tokens(c_function_body(source(paths[1]), "ramfs_test_context")),
        )
        definitions = owned_definitions(1)
        self.assertEqual(31, len(definitions))
        self.assertEqual(16, len(fixture_parameter_matches(1)))
        for spelling, count in fixture_parameter_matches(1):
            self.assertEqual(1, count, spelling)
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
        macro = header[
            header.index("#define EXPECT_TRUE"):header.index("struct ramfs_seed_test_fixture")
        ].strip()
        self.assertEqual(
            "dc1fbf95f12b7896694addb93aa8603ed20b7bd57022a9d01f7b8c7342234fb6",
            body_digest(macro),
        )
        self.assertEqual(
            body_tokens("{ return &seed_fixture; }"),
            body_tokens(c_function_body(source(paths[1]), "ramfs_seed_test_context")),
        )
        self.assertEqual(21, len(owned_definitions(2)))
        self.assertEqual(9, len(fixture_parameter_matches(2)))
        for spelling, count in fixture_parameter_matches(2):
            self.assertEqual(1, count, spelling)
        self.assertEqual({}, c_function_definitions(header))
        self.assertEqual((), file_scope_objects(header))
        rejection = c_function_body(source(paths[1]), "ramfs_seed_test_expect_rejection")
        self.assertIn("struct micros_ramfs_seed_plan expected_plan;", rejection)
        self.assertIn("struct micros_ramfs_seed_plan plan;", rejection)
        self.assertNotIn("static struct micros_ramfs_seed_plan", rejection)

    def test_model_types_seed_lifetime_and_callback_identity_are_exact(self):
        paths = family_paths(3)
        header = source(PRIVATE_HEADERS[3])
        types = header[header.index("enum {"):header.index("struct ramfs_model_seed {")].strip()
        self.assertEqual(MODEL_TYPE_DIGEST, body_digest(types))
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
        self.assertEqual(56, len(owned_definitions(3)))
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
        types = header[header.index("struct vfs_ramfs_result {"):header.index("struct vfs_service_fixture {")]
        self.assertEqual(PEER_RESULT_TYPE_DIGEST, body_digest(types))
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
            expected = objects if path == paths[1] else ()
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
        self.assertEqual(38, len(owned_definitions(4)))
        self.assertEqual(21, len(fixture_parameter_matches(4)))
        for spelling, count in fixture_parameter_matches(4):
            self.assertEqual(1, count, spelling)
        self.assertIn("uint64_t vfs_initialized_data_value(void);", header)
        self.assertIn("void micros_ramfs_service_test_report(uint64_t magic);", header)

    def test_kernel_const_configuration_and_report_macros_are_base_exact(self):
        paths = family_paths(5)
        objects = file_scope_objects(source(paths[1]))
        self.assertEqual(4, len(objects))
        restored = restored_kernel_objects(objects)
        self.assertEqual(KERNEL_CONST_OBJECT_DIGEST, body_digest("\n".join(restored)))
        self.assertEqual(
            KERNEL_REPORT_MACRO_DIGEST,
            body_digest(macro_definition_text(source(paths[2]))),
        )
        self.assertEqual((), file_scope_objects(source(paths[0])))
        self.assertEqual((), file_scope_objects(source(paths[2])))
        self.assertEqual(4, len(owned_definitions(5)))

    def test_kernel_configuration_preserves_raw_literals_not_only_masked_shape(self):
        fixture = source(family_paths(5)[1])
        objects = raw_const_objects(
            fixture, ("profiles", "expected_services", "policies", "ramfs_test_runtime_config"),
        )
        self.assertEqual(
            KERNEL_RAW_OBJECT_DIGEST,
            body_digest("\n".join(restored_kernel_objects(objects))),
        )

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

    def test_original_bodies_remain_frozen_during_owner_moves(self):
        for family, owners in enumerate(ORIGINAL_FUNCTIONS):
            definitions = owned_definitions(family)
            for owner, names in owners.items():
                for name in names:
                    with self.subTest(family=family, name=name):
                        path, spelling = original_location(family, owner, name, definitions)
                        self.assertEqual(
                            BASELINE_BODIES[family][name],
                            original_body_digest(family, name, c_function_body(source(path), spelling)),
                        )

    def test_facade_exposes_a_typed_operation_dispatch_phase(self):
        text = source(family_paths(0)[0])
        definitions = c_function_definitions(text)
        self.assertIn("dispatch_request", definitions)
        self.assertLessEqual(definitions["dispatch_request"], 120)
        self.assertLessEqual(definitions["micros_ramfs_handle_call"], 120)
        signature = re.findall(
            r"(?m)^static enum micros_ramfs_core_error dispatch_request\([^;{}]*\)\s*\{",
            text,
        )
        self.assertEqual(1, len(signature))
        expected = (
            "static enum micros_ramfs_core_error dispatch_request("
            "struct micros_ramfs_state *state, const struct micros_ramfs_request *request, "
            "const struct micros_ramfs_io *io, struct ramfs_result_fields *fields, "
            "enum micros_ramfs_result *result) {"
        )
        self.assertEqual(body_tokens(expected), body_tokens(signature[0]))

    def test_exact_long_inventory_has_no_implicit_exceptions(self):
        actual = set()
        for family in range(6):
            renames = normal_names(family)
            for (_, name), lines in owned_definitions(family).items():
                if lines > 120:
                    actual.add((family, renames.get(name, name)))
        for node in ast.walk(ast.parse(source(GENERATOR_PATH))):
            if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)) and node.end_lineno - node.lineno + 1 > 120:
                actual.add((6, node.name))
        self.assertEqual(set(LONG_FUNCTION_MIGRATION), actual)

    def test_stable_neighbors_and_seed_inventory_are_byte_exact(self):
        self.assertEqual(17, len(STABLE_HASHES))
        for path, expected in STABLE_HASHES.items():
            with self.subTest(path=path):
                self.assertEqual(expected, digest_bytes((ROOT / path).read_bytes()), REVIEWED_BASE)
        self.assertEqual(
            {"servers/ramfs/seed/etc/motd"},
            {str(path.relative_to(ROOT)) for path in (ROOT / "servers/ramfs/seed").rglob("*") if path.is_file()},
        )
        self.assertFalse(any(path.is_symlink() for path in (ROOT / "servers/ramfs/seed").rglob("*")))

    def test_generator_is_frozen_before_its_semantic_split(self):
        self.assertEqual(BASELINE_GENERATOR, digest_bytes(source(GENERATOR_PATH).encode()))

    def test_fixture_output_matches_the_independent_reviewed_base_record(self):
        from tests.host.test_generate_bootstrap_fixture import bootstrap_image
        from tests.host.test_generate_ramfs_service_fixture import vfs_image
        from tests.host.test_generate_vm_handoff_fixture import vm_image
        from tools.generate_ramfs_service_fixture import render_fixture

        output = render_fixture(
            bootstrap_image(), vm_image(), bootstrap_image(),
            bootstrap_image(), bootstrap_image(), vfs_image(),
        )
        self.assertEqual(BASELINE_GENERATOR_OUTPUT, digest_bytes(output.encode()))

    def test_split_paths_inherit_all_four_original_metadata_dimensions(self):
        qemu = ["test-qemu-ramfs", "test-qemu-vfs", "test-qemu-elf-spawn"]
        expected_dimensions = (
            (False, qemu, ["test-ramfs-model"], ["build-ramfs-service-image"]),
            (True, [], [], []),
            (True, [], [], []),
            (False, qemu, ["test-ramfs-model"], []),
            (False, qemu, [], []),
            (False, qemu, [], []),
        )
        for family, (original, aliases) in enumerate(alias_rows()):
            expected = expected_dimensions[family]
            self.assertEqual(expected, (
                validation_plan.requires_full_tier([original]),
                validation_plan.affected_workflows([original]),
                validation_plan.affected_slow_models([original]),
                validation_plan.affected_image_workflows([original]),
            ))
            self.assertEqual(1, validation_plan.PATH_CLASSIFICATION_EQUIVALENTS.count((original, aliases)))
            for path in aliases:
                with self.subTest(path=path):
                    self.assertEqual(expected, (
                        validation_plan.requires_full_tier([path]),
                        validation_plan.affected_workflows([path]),
                        validation_plan.affected_slow_models([path]),
                        validation_plan.affected_image_workflows([path]),
                    ))

    def test_structural_gate_is_explicitly_host_unit_only(self):
        self.assertEqual(1, validation_plan.HOST_UNIT_ONLY_INPUTS.count(STRUCTURAL_TEST_PATH))
        self.assertFalse(validation_plan.requires_full_tier([STRUCTURAL_TEST_PATH]))
        self.assertEqual(
            [validation_plan.UNIT_FAST, *validation_plan.diff_commands("main")],
            validation_plan.plan([STRUCTURAL_TEST_PATH], "fast", "main"),
        )

    def test_recipe_reconstruction_and_closed_declaration_maintenance(self):
        cmake = source("CMakeLists.txt")
        self.assertEqual(BASE_CMAKE_DIGEST, digest_bytes(restored_cmake(cmake).encode()))
        expected = BASE_CMAKE_DIGEST if SYMBOL_OWNER_MIGRATION else digest_bytes(cmake.encode())
        self.assertEqual(expected, validation_context.RECIPE_DIGESTS["CMakeLists.txt"])

    def test_unreviewed_recipe_identity_is_rejected_before_any_build_metadata(self):
        with patch.dict("tools.validation_context.os.environ", {
            "PYTHONNOUSERSITE": "1", "CLANG_NO_DEFAULT_CONFIG": "1",
        }, clear=True), \
                patch("tools.validation_context.platform_identity", return_value={}), \
                patch("tools.validation_context._file_digest", return_value="unreviewed-recipe-bytes"), \
                patch("tools.validation_context._cache") as cache:
            with self.assertRaisesRegex(validation_context.UnsupportedContext, "build recipe"):
                validation_context.collect(ROOT, "test-unit")
            cache.assert_not_called()

    def test_recipe_reconstruction_rejects_unreviewed_sources_and_commands(self):
        cmake = source("CMakeLists.txt")
        restored_cmake(cmake)
        for changed in (
            cmake.replace("ramfs_codec.c", "ramfs_unreviewed.c", 1),
            cmake + "\nadd_custom_target(unreviewed COMMAND true)\n",
            cmake.replace("${MICROS_RAMFS_CORE_SOURCES}", "servers/ramfs/ramfs_core.c", 1),
            cmake.replace("-Wall", "-Wno-all", 1),
        ):
            with self.subTest(changed=changed[:80]), self.assertRaises(AssertionError):
                restored_cmake(changed)

    def test_body_fingerprint_rejects_literal_authority_and_order_mutations(self):
        baseline = 'if (source != endpoint) return ERROR; copy(state, "a b", \'x\'); ++state->count;'
        for changed in (
            baseline.replace("!=", "=="),
            baseline.replace('"a b"', '"ab"'),
            baseline.replace("'x'", "'y'"),
            baseline.replace("++", "--"),
            '++state->count; if (source != endpoint) return ERROR; copy(state, "a b", \'x\');',
        ):
            with self.subTest(changed=changed):
                self.assertNotEqual(body_digest(baseline), body_digest(changed))
        self.assertEqual(body_digest(baseline), body_digest("/* explanation */\n" + baseline))
        self.assertEqual(
            body_digest("copy(state);"),
            body_digest("ramfs_copy(state);", {"ramfs_copy": "copy"}),
        )

    def test_missing_and_duplicate_original_owners_fail_closed(self):
        original = family_paths(0)[0]
        final = family_paths(0)[1]
        with patch(f"{__name__}.SYMBOL_OWNER_MIGRATION", ()):
            self.assertEqual(
                (final, "ramfs_zero_bytes"),
                original_location(0, 1, "zero_bytes", {(final, "ramfs_zero_bytes"): 9}),
            )
            for definitions in (
                {},
                {(original, "zero_bytes"): 9},
                {(final, "ramfs_zero_bytes"): 9, (original, "zero_bytes"): 9},
            ):
                with self.subTest(definitions=definitions), self.assertRaises(AssertionError):
                    original_location(0, 1, "zero_bytes", definitions)

    def test_typed_fixture_transport_preserves_fields_calls_literals_and_caller_exits(self):
        original = '{ state_snapshot = state; if (!load_seed(path)) return false; bus.calls = 0; }'
        transported = (
            '{ fixture->state_snapshot = fixture->state; '
            'if (!ramfs_test_load_seed(fixture, path)) return false; fixture->bus.calls = 0; }'
        )
        normalized = fixture_transport_tokens(1, "case", body_tokens(transported))
        self.assertEqual(
            body_tokens(original),
            tuple(normal_names(1).get(token, token) for token in normalized),
        )
        for changed in (
            transported.replace("fixture, path", "other, path"),
            transported.replace("fixture->state_snapshot", "fixture->unreviewed"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                fixture_transport_tokens(1, "case", body_tokens(changed))
        for changed in (
            transported.replace("return false", "return true"),
            transported.replace("= 0", "= 1"),
            transported.replace("state_snapshot = fixture->state", "state = fixture->state_snapshot"),
        ):
            with self.subTest(changed=changed):
                self.assertNotEqual(normalized, fixture_transport_tokens(1, "case", body_tokens(changed)))
        with self.assertRaises(AssertionError):
            fixture_transport_tokens(1, "main", body_tokens('{ if (!load_seed(path)) return false; }'))

    def test_seed_transport_cannot_clear_or_alias_the_wrong_fixture_storage(self):
        original = "{ memcpy(mutated_image, seed_image, seed_image_size); }"
        transported = "{ memcpy(fixture->mutated_image, fixture->seed_image, fixture->seed_image_size); }"
        self.assertEqual(
            body_tokens(original), fixture_transport_tokens(2, "case", body_tokens(transported)),
        )
        with self.assertRaises(AssertionError):
            fixture_transport_tokens(2, "case", body_tokens("{ fixture->bus.calls = 0; }"))
        with self.assertRaises(AssertionError):
            fixture_transport_tokens(2, "main", body_tokens(
                "{ struct ramfs_test_fixture *fixture = ramfs_test_context(); return 0; }"
            ))
        changed = transported.replace("fixture->mutated_image", "fixture->seed_image")
        self.assertNotEqual(
            fixture_transport_tokens(2, "case", body_tokens(transported)),
            fixture_transport_tokens(2, "case", body_tokens(changed)),
        )

    def test_fixture_transport_never_renames_a_member_as_a_global_constant(self):
        with self.assertRaises(AssertionError):
            original_body_digest(
                1, "case", "{ fixture->corrupt_state.ramfs_test_vfs_endpoint = 5; }",
            )

    def test_model_borrow_transport_cannot_reset_seed_or_drop_comparison_and_failure(self):
        bindings = (
            "struct ramfs_model *selected = ramfs_model_context(); "
            "const struct ramfs_model_seed *seed_info = ramfs_model_seed_context(); "
        )
        body = (
            "{ " + bindings
            + "memset(selected, 0, sizeof(*selected)); selected->seed = seed; "
            "initialize(seed_info->seed_image, seed_info->seed_image_size); "
            "if (!compare(selected)) return false; }"
        )
        expected = (
            "{ memset(&model, 0, sizeof(model)); model.seed = seed; "
            "initialize(seed_image, seed_image_size); if (!compare(&model)) return false; }"
        )
        self.assertEqual(body_tokens(expected), model_transport_tokens("run_model", body_tokens(body)))
        for changed in (
            body.replace("selected->seed =", "selected->unreviewed ="),
            body.replace("seed_info->seed_image,", "seed_info->production,"),
            body.replace("const struct ramfs_model_seed", "struct ramfs_model"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                model_transport_tokens("run_model", body_tokens(changed))
        for changed in (
            body.replace("memset(selected,", "memset(seed_info,"),
            body.replace("return false", "return true"),
            body.replace("if (!compare(selected)) return false;", ""),
        ):
            with self.subTest(changed=changed):
                self.assertNotEqual(
                    body_tokens(expected), model_transport_tokens("run_model", body_tokens(changed)),
                )

    def test_peer_borrow_preserves_volatile_read_order_and_buffer_extents(self):
        body = (
            "{ struct vfs_service_fixture *fixture = vfs_service_context(); "
            "if (vfs_initialized_data_value() != MAGIC || !vfs_validate_configuration(fixture)) stop(); "
            "clear(fixture->vfs_ramfs_buffer, sizeof(fixture->vfs_ramfs_buffer)); }"
        )
        expected = (
            "{ if (vfs_initialized_data != MAGIC || !vfs_validate_configuration()) stop(); "
            "clear(vfs_ramfs_buffer, sizeof(vfs_ramfs_buffer)); }"
        )
        normalized = fixture_transport_tokens(4, "micros_service_main", body_tokens(body))
        self.assertEqual(body_tokens(expected), peer_read_tokens("micros_service_main", normalized))
        with self.assertRaises(AssertionError):
            fixture_transport_tokens(
                4, "micros_service_main", body_tokens(body.replace("fixture->vfs_ramfs_buffer", "fixture->bus")),
            )
        changed = body.replace("vfs_initialized_data_value() != MAGIC", "MAGIC != MAGIC")
        self.assertNotEqual(
            body_tokens(expected),
            peer_read_tokens(
                "micros_service_main", fixture_transport_tokens(4, "micros_service_main", body_tokens(changed)),
            ),
        )

    def test_dispatch_projection_preserves_pointer_bindings_default_and_caller_exit(self):
        caller = (
            "{ enum micros_ramfs_core_error error; result = OK; "
            "error = dispatch_request(state, &request, io, &fields, &result); "
            "if (error != CORE_OK) { return error; } validate(state); }"
        )
        phase = (
            "{ enum micros_ramfs_core_error error = MICROS_RAMFS_CORE_OK; "
            "switch (request->type) { case MOUNT: *result = mount(state, fields); break; "
            "case READ: error = read(state, request, io, fields, result); break; "
            "default: return INVARIANT; } return error; }"
        )
        expected = (
            "{ enum micros_ramfs_core_error error; result = OK; error = MICROS_RAMFS_CORE_OK; "
            "switch (request.type) { case MOUNT: result = mount(state, &fields); break; "
            "case READ: error = read(state, &request, io, &fields, &result); break; "
            "default: return INVARIANT; } if (error != CORE_OK) { return error; } validate(state); }"
        )
        self.assertEqual(body_tokens(expected), dispatcher_body_tokens(caller, phase))
        for changed in (
            caller.replace("&request", "&other"),
            caller.replace("error = dispatch_request", "ignored = dispatch_request"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                dispatcher_body_tokens(changed, phase)
        for changed in (
            phase.replace("return error;", "return MICROS_RAMFS_CORE_OK;"),
            phase.replace("enum micros_ramfs_core_error error", "int error"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                dispatcher_body_tokens(caller, changed)
        for changed in (
            phase.replace("return INVARIANT", "return CORE_OK"),
            phase.replace("fields, result", "fields, fields"),
            phase.replace("request->type", "request->source"),
        ):
            with self.subTest(changed=changed):
                self.assertNotEqual(body_tokens(expected), dispatcher_body_tokens(caller, changed))
        self.assertNotEqual(
            body_tokens(expected),
            dispatcher_body_tokens(caller.replace("return error;", "return CORE_OK;"), phase),
        )


if __name__ == "__main__":
    unittest.main()

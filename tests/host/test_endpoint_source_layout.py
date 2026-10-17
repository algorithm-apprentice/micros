"""Bounded source-contract regression for Accepted ADR-0067."""

import ast
import hashlib
import json
from pathlib import Path
import re
import unittest

from tests.host.ast_evidence import ast_dump
from tests.host.test_frame_ownership_source_layout import (
    c_function_definitions,
    c_lexical_text,
    tokens,
)
from tests.host.test_kernel_bootstrap_source_layout import c_function_body
from tests.host.test_kernel_object_source_layout import (
    kernel_object_cmake_projection,
    restored_kernel_objects_source,
)
from tests.host.test_pm_service_source_layout import cmake_commands
from tests.host.test_vm_service_source_layout import file_scope_objects
from tools.check_docs import topological_order
from tools import validation_context, validation_plan
from tests.host import kernel_layout_source


ROOT = Path(__file__).resolve().parents[2]
REVIEWED_BASE = "7e107f5189cbf6f3aa44dfc05652d041e803c60c"
BASE_CMAKE_DIGEST = "cd615eaa75141a5626a024de1ad7772e254c47129ed34829a9b01a472da28dd6"
BASE_CTOKENS_AST = "3e378fcb1e32c96d05240c3d80401d159715196d92de372d8ca92cdddd9a3cd8"
MODEL_TYPES_DIGEST = "d6da66f1eb42b10fd121f8316a01b5cedbf6c80bb31696a5b3693779f349fb2e"
MODEL_OPERATION_NAMES_DIGEST = "7978fb4c6b345f5f3a67f77f681acedd35a62e5b97bd49c5b62899e8b96a8083"
MODEL_SCRIPT_DATA_DIGEST = "d7dc952e504696855635bcc3630210ea953bfdc54bf7466ac6cb10718ce4435e"
MODEL_RUN_FIELDS = ("model", "coverage", "random_state", "step")
MODEL_ITERATION_FIELDS = ("random", "operation", "slot", "value")
MODEL_RUN_DECLARATIONS = (
    "struct model_slot model[MODEL_SLOT_COUNT]; struct model_coverage coverage; "
    "uint32_t random_state = seed; size_t step;"
)
MODEL_RUN_HELPERS = (
    "endpoint_model_setup_run", "endpoint_model_draw_iteration", "endpoint_model_apply_operation",
    "endpoint_model_cleanup_run", "endpoint_model_coverage_complete",
)
MODEL_OPERATION_HELPERS = {
    "MODEL_OPERATION_" + operation.upper(): "endpoint_model_" + operation
    for operation in ("create", "reserve", "install", "activate", "authorize", "close", "release", "stale")
}
MODEL_AUTHORIZATION_HELPERS = (
    "endpoint_model_authorize_reply_receive", "endpoint_model_authorize_reply",
    "endpoint_model_select_authorization_target", "endpoint_model_authorize_selected_target",
)
MODEL_MUTABLE_ARGUMENTS = (
    "struct endpoint_model_fixture *fixture, struct endpoint_model_run *run, "
    "struct endpoint_model_iteration *iteration"
)
MODEL_TASK_SIGNATURES = {
    "endpoint_model_setup_run": (
        "static bool", "struct endpoint_model_fixture *fixture, struct endpoint_model_run *run",
    ),
    "endpoint_model_draw_iteration": (
        "static void", "struct endpoint_model_run *run, struct endpoint_model_iteration *iteration",
    ),
    "endpoint_model_apply_operation": ("static bool", MODEL_MUTABLE_ARGUMENTS),
    "endpoint_model_cleanup_run": (
        "static bool", "struct endpoint_model_fixture *fixture, struct endpoint_model_run *run",
    ),
    "endpoint_model_coverage_complete": ("static bool", "const struct endpoint_model_run *run"),
    **{name: ("bool", MODEL_MUTABLE_ARGUMENTS) for name in MODEL_OPERATION_HELPERS.values()},
    **{
        name: (
            "static bool", MODEL_MUTABLE_ARGUMENTS + ", uint32_t *authorization_operation, "
            "enum micros_endpoint_error *expected, enum micros_endpoint_error *error",
        )
        for name in MODEL_AUTHORIZATION_HELPERS[:2]
    },
    "endpoint_model_select_authorization_target": (
        "static void", "const struct endpoint_model_run *run, const struct endpoint_model_iteration *iteration, "
        "uint32_t target_random, size_t *target, uint32_t *authorization_operation",
    ),
    "endpoint_model_authorize_selected_target": (
        "static bool", MODEL_MUTABLE_ARGUMENTS + ", size_t target, uint32_t authorization_operation, "
        "enum micros_endpoint_error *expected, enum micros_endpoint_error *error",
    ),
}
MODEL_CONTEXT_FIELDS = ("registry", "objects", "hart", "trace", "trace_count")
MODEL_CONTEXT_HELPERS = {
    "record_transition": False, "model_fail": False, "setup_fixture": True, "validate_model": False,
}
NATIVE_ASSERTION_DIGEST = "a6a8819676540e04acd7f9d209aacfca43a90794c5f37f5462f32b2b91d205a1"
NATIVE_RUNNER_TABLE_DIGEST = "2c71f3886b3dc3a68d7269ccd129036db53da625993828c8a12697eeb109f9e1"
REJECTED_TABLE_MACRO_DIGEST = "e5ecf37d829a27ad4c2b16a6c4ae3d129da83a8816af2bb54e2e4fe71e142915"
QEMU_PROFILE_DATA_DIGEST = "1689296a2a7a3923bc0e89fa318d5636694d0e214342117fd27cf915d5a5a169"
QEMU_WORK_DECLARATIONS = (
    "struct micros_kernel_objects *objects; struct micros_endpoint_registry *registry; "
    "struct micros_grant_registry *grant_registry; struct micros_grant_cancel_plan cancel_plan; "
    "struct micros_grant_record grant_record; struct micros_process_handle processes[2]; "
    "struct micros_process_handle replacement_process; struct micros_thread_handle threads[2]; "
    "struct micros_thread_handle replacement_thread; micros_endpoint_t endpoints[2]; "
    "micros_endpoint_t replacement_endpoint; micros_grant_t read_grant; micros_grant_t write_grant; "
    "micros_grant_t revoked_grant; const struct micros_endpoint_record *record; "
    "size_t baseline_processes; size_t baseline_threads; size_t baseline_harts;"
)
QEMU_WORK_FIELDS = (
    "objects", "registry", "grant_registry", "cancel_plan", "grant_record", "processes",
    "replacement_process", "threads", "replacement_thread", "endpoints", "replacement_endpoint",
    "read_grant", "write_grant", "revoked_grant", "record", "baseline_processes", "baseline_threads", "baseline_harts",
)
QEMU_FIXTURE_TASKS = (
    "endpoint_qemu_capture_baseline", "endpoint_qemu_initialize_runtimes", "endpoint_qemu_reserve_clients",
)
QEMU_SCENARIO_TASKS = (
    "endpoint_qemu_require_held_owners", "endpoint_qemu_publish_and_check_authority",
    "endpoint_qemu_prepare_grant_cancellation", "endpoint_qemu_close_commit_and_reuse",
    "endpoint_qemu_reject_stale_and_cleanup",
)
NATIVE_CONTEXT_FIELDS = ("registry", "objects", "processes", "threads", "hart")
NATIVE_CONTEXT_CASES = (
    "test_profile_table_is_immutable_and_specific", "test_invalid_profile_tables_preserve_storage",
    "test_validator_rejects_corruption", "test_endpoint_lifecycle_and_authorization",
    "test_profile_install_and_close_require_held_threads", "test_resolution_rejects_unimplemented_slots",
    "test_endpoint_reuse_rejects_stale_generation", "test_source_only_seal_preserves_staged_source",
    "test_relationship_validator_rejects_corruption",
)
STRUCTURAL_TEST_PATH = "tests/host/test_endpoint_source_layout.py"
CORE_SOURCES = (
    "kernel/endpoint.c",
    "kernel/endpoint_codec.c",
    "kernel/endpoint_identity.c",
    "kernel/endpoint_registry.c",
    "kernel/endpoint_privilege.c",
    "kernel/endpoint_completion_validation.c",
    "kernel/endpoint_queue_validation.c",
    "kernel/endpoint_object_validation.c",
    "kernel/endpoint_publication.c",
    "kernel/endpoint_close.c",
)
SOURCE_SETS = {
    "MICROS_ENDPOINT_CORE_SOURCES": CORE_SOURCES,
    "MICROS_ENDPOINT_HOST_TEST_SOURCES": (
        "tests/host/endpoint_test.c",
        "tests/host/endpoint_test_fixture.c",
        "tests/host/endpoint_identity_test.c",
        "tests/host/endpoint_profile_test.c",
        "tests/host/endpoint_validation_test.c",
        "tests/host/endpoint_lifecycle_test.c",
        "tests/host/endpoint_source_only_test.c",
    ),
    "MICROS_ENDPOINT_MODEL_HOST_TEST_SOURCES": (
        "tests/host/endpoint_model_test.c",
        "tests/host/endpoint_model_fixture.c",
        "tests/host/endpoint_model_reference.c",
        "tests/host/endpoint_model_projection.c",
        "tests/host/endpoint_model_operations.c",
    ),
    "MICROS_ENDPOINT_QEMU_TEST_SOURCES": (
        "kernel/endpoint_test.c",
        "kernel/endpoint_test_fixture.c",
        "kernel/endpoint_test_scenarios.c",
    ),
}
PRIVATE_HEADERS = (
    "kernel/endpoint_private.h",
    "tests/host/endpoint_test_internal.h",
    "tests/host/endpoint_model_internal.h",
    "kernel/endpoint_test_internal.h",
)
ORIGINAL_FUNCTIONS = {
    0: {
        0: (
            "micros_endpoint_resolve_internal micros_endpoint_resolve_active "
            "micros_endpoint_authorize_operation micros_endpoint_authorize_target "
            "micros_endpoint_authorize_kernel_operation"
        ).split(),
        1: (
            "storage_is_zero clear_bytes copy_bytes bytes_equal bytes_are_zero names_equal profile_is_zero "
            "endpoint_record_is_zero endpoint_ipc_state_is_zero thread_handle_is_zero "
            "thread_handle_is_shallow_valid queue_pair_is_shallow_valid "
            "pending_notification_state_is_shallow_valid clear_endpoint_record canonicalize_profile"
        ).split(),
        2: (
            "process_handles_equal thread_handles_equal require_initialized micros_endpoint_pack "
            "micros_endpoint_unpack process_resolve_error endpoint_record_resolve_validated "
            "process_has_current_thread process_threads_are_held process_threads_have_clear_ipc_state"
        ).split(),
        3: ("micros_endpoint_registry_initialize micros_endpoint_registry_validate").split(),
        4: (
            "micros_privilege_profile_resolve micros_privilege_profile_allows_operation "
            "micros_privilege_profile_allows_target micros_privilege_profile_allows_kernel_operation"
        ).split(),
        5: (
            "delivery_state_is_clear reply_wait_state_is_valid source_only_process_threads_are_valid "
            "staged_reply_token_binding_is_valid notification_message_is_valid read_u32_le "
            "source_only_message_is_valid staged_delivery_state_is_valid staged_error_state_is_valid "
            "staged_no_message_success_state_is_valid"
        ).split(),
        6: (
            "sender_queue_state_is_valid receiver_queue_state_is_valid endpoint_queue_is_valid "
            "pending_notification_state_is_valid receiver_matches_pending_notification endpoint_has_matchable_work"
        ).split(),
        7: ("thread_has_no_ipc_flags micros_endpoint_registry_validate_objects").split(),
        8: (
            "micros_endpoint_reserve micros_endpoint_install_profile micros_endpoint_activate "
            "micros_endpoint_preflight_publish micros_endpoint_commit_publish_prevalidated"
        ).split(),
        9: (
            "micros_endpoint_preflight_source_only_for_teardown micros_endpoint_preflight_source_only "
            "micros_endpoint_commit_source_only_prevalidated micros_endpoint_seal_source_only "
            "endpoint_has_foreign_waiters endpoint_is_pending_notification_source "
            "micros_endpoint_close_commit_prevalidated micros_endpoint_close"
        ).split(),
    },
    1: {
        0: ["main"],
        1: ("profile context_pattern write_u32_le setup_lifecycle_fixture").split(),
        2: ("test_endpoint_encoding_boundaries test_resolution_rejects_unimplemented_slots").split(),
        3: ("test_profile_table_is_immutable_and_specific test_invalid_profile_tables_preserve_storage").split(),
        4: ("test_validator_rejects_corruption test_relationship_validator_rejects_corruption").split(),
        5: (
            "test_endpoint_lifecycle_and_authorization test_profile_install_and_close_require_held_threads "
            "test_endpoint_reuse_rejects_stale_generation"
        ).split(),
        6: ["test_source_only_seal_preserves_staged_source"],
    },
    2: {
        0: ["micros_endpoint_model_test_run"],
        1: ("record_transition model_fail setup_fixture").split(),
        2: (
            "phase_has_endpoint find_slot slot_is_live slot_has_endpoint slot_has_nonactive_endpoint "
            "slot_is_releasable slot_has_stale_endpoint find_active_profile target_is_allowed"
        ).split(),
        3: ["validate_model"],
        4: ["next_random"],
    },
    3: {0: ["micros_endpoint_runtime_run_self_test"], 1: [], 2: []},
}
CORE_PRIVATE_EXPORTS = (
    "bytes_are_zero bytes_equal canonicalize_profile clear_bytes clear_endpoint_record copy_bytes "
    "delivery_state_is_clear endpoint_has_matchable_work endpoint_ipc_state_is_zero endpoint_queue_is_valid "
    "endpoint_record_is_zero endpoint_record_resolve_validated names_equal "
    "pending_notification_state_is_shallow_valid pending_notification_state_is_valid process_handles_equal "
    "process_has_current_thread process_resolve_error process_threads_are_held "
    "process_threads_have_clear_ipc_state profile_is_zero queue_pair_is_shallow_valid "
    "reply_wait_state_is_valid require_initialized source_only_process_threads_are_valid "
    "staged_delivery_state_is_valid staged_error_state_is_valid staged_no_message_success_state_is_valid "
    "storage_is_zero thread_handle_is_zero thread_handles_equal"
).split()
LONG_FUNCTIONS = (
    (0, "micros_endpoint_registry_validate"),
    (0, "micros_endpoint_registry_validate_objects"),
    (1, "test_invalid_profile_tables_preserve_storage"),
    (1, "test_endpoint_lifecycle_and_authorization"),
    (1, "test_profile_install_and_close_require_held_threads"),
    (1, "test_source_only_seal_preserves_staged_source"),
    (1, "main"),
    (2, "validate_model"),
    (2, "micros_endpoint_model_test_run"),
    (3, "micros_endpoint_runtime_run_self_test"),
)
INITIAL_SYMBOL_OWNER_MIGRATION = tuple(
    (family, name) for family, owners in ORIGINAL_FUNCTIONS.items()
    for owner, names in owners.items() if owner != 0 for name in names
)
SYMBOL_OWNER_MIGRATION = ()
LONG_FUNCTION_MIGRATION = ()
REGISTRY_VALIDATION_DECLARATIONS = (
    "uint32_t installed = 0; size_t observed_count = 0; size_t index; size_t other_index;"
)
VALIDATOR_PHASES = {
    "micros_endpoint_registry_validate": (
        3,
        (
            ("profile_table_is_canonical", ("registry",), REGISTRY_VALIDATION_DECLARATIONS, 1),
            ("endpoint_records_are_canonical", ("registry",), "size_t index;", 0),
        ),
    ),
    "micros_endpoint_registry_validate_objects": (
        7,
        (
            ("process_relationships_are_canonical", ("registry", "objects", "queued_threads"), "size_t index;", 0),
            ("thread_relationships_are_canonical", ("registry", "objects", "queued_threads"), "size_t index;", 0),
        ),
    ),
}
VALIDATOR_HELPER_OWNERS = {
    specification[0]: family_paths_index
    for family_paths_index, specifications in VALIDATOR_PHASES.values() for specification in specifications
}
INITIALIZER_HELPERS = (
    "prepare_profile_candidates", "candidate_targets_are_closed", "publish_profile_table",
)
MODEL_PROJECTION_HELPERS = ("endpoint_model_live_process_and_thread_match", "endpoint_model_phase_matches")
NATIVE_CASE_WORK = {
    "test_invalid_profile_tables_preserve_storage": {
        "owner": 3,
        "tag": "endpoint_rejected_profile_case",
        "fields": ("profiles", "before"),
        "declarations": (
            "struct micros_privilege_profile profiles[2]; struct micros_endpoint_registry before;"
        ),
        "original": "struct micros_privilege_profile profiles[2]; struct micros_endpoint_registry before;",
        "outlined": "struct endpoint_rejected_profile_case work;",
        "phases": (
            "endpoint_test_rejected_profiles_setup", "endpoint_test_rejected_duplicate_profiles",
            "endpoint_test_rejected_profile_shapes", "endpoint_test_rejected_profile_targets",
            "endpoint_test_rejected_profile_capacities",
        ),
        "work_only": ("endpoint_test_rejected_profiles_setup",),
        "macro_before": 1,
        "undef_before": 4,
    },
    "test_source_only_seal_preserves_staged_source": {
        "owner": 6,
        "tag": "endpoint_source_only_case",
        "fields": ("endpoints", "record", "receiver", "index"),
        "declarations": (
            "micros_endpoint_t endpoints[2]; const struct micros_endpoint_record *record; "
            "struct micros_thread *receiver; size_t index;"
        ),
        "original": (
            "micros_endpoint_t endpoints[2]; const struct micros_endpoint_record *record = NULL; "
            "struct micros_thread *receiver; size_t index;"
        ),
        "outlined": "struct endpoint_source_only_case work; work.record = NULL;",
        "phases": (
            "endpoint_test_source_only_prepare_acknowledgment", "endpoint_test_source_only_seal_and_validate_source",
            "endpoint_test_source_only_reject_new_authority", "endpoint_test_source_only_drain_and_close",
        ),
    },
    "test_endpoint_lifecycle_and_authorization": {
        "owner": 5,
        "tag": "endpoint_lifecycle_case",
        "fields": ("endpoints", "stale_endpoint", "stale_target", "record"),
        "declarations": (
            "micros_endpoint_t endpoints[2]; micros_endpoint_t stale_endpoint; "
            "struct micros_process_handle stale_target; const struct micros_endpoint_record *record;"
        ),
        "original": (
            "micros_endpoint_t endpoints[2] = {0, 0}; micros_endpoint_t stale_endpoint; "
            "struct micros_process_handle stale_target; const struct micros_endpoint_record *record = NULL;"
        ),
        "outlined": (
            "struct endpoint_lifecycle_case work; work.endpoints[0] = 0; work.endpoints[1] = 0; work.record = NULL;"
        ),
        "phases": (
            "endpoint_test_lifecycle_hidden_reservation", "endpoint_test_lifecycle_profile_publication",
            "endpoint_test_lifecycle_asymmetric_authority", "endpoint_test_lifecycle_stale_and_operation_precedence",
            "endpoint_test_lifecycle_final_relationship",
        ),
        "fixture_only": ("endpoint_test_lifecycle_final_relationship",),
    },
    "test_profile_install_and_close_require_held_threads": {
        "owner": 5,
        "tag": "endpoint_held_case",
        "fields": ("endpoint", "context"),
        "declarations": "micros_endpoint_t endpoint; struct micros_user_context context;",
        "original": (
            "micros_endpoint_t endpoint; "
            "struct micros_user_context context = endpoint_test_context_pattern(UINT64_C(0x1000));"
        ),
        "outlined": (
            "struct endpoint_held_case work; work.context = endpoint_test_context_pattern(UINT64_C(0x1000));"
        ),
        "phases": (
            "endpoint_test_held_installation_denied", "endpoint_test_held_profile_publication",
            "endpoint_test_held_close_and_cleanup",
        ),
    },
}
CORE_DAG = {
    0: {2, 4, 7}, 1: set(), 2: set(), 3: {1, 2}, 4: {3},
    5: {1, 2}, 6: {1, 2, 5}, 7: {3, 4, 5, 6},
    8: {1, 2, 4, 7}, 9: {1, 2, 7},
}
BASELINE_BODIES = {
    0: {
        "storage_is_zero": "ce6a535b0bb78c3b7f8ef47b8baa1ff148a615016d61a7f961bcc9f0d7ed13e5",
        "clear_bytes": "1bfe896da396160656d79ce0e67c19f9a3ed382a4d894e12c0abdce0c797e1a0",
        "copy_bytes": "4b480638cdb607c5cc090e482b8427a0a73209fe3d249c13e8cf513b8528a036",
        "bytes_equal": "2cf2a64c99b80613e115a17e7c95e674a8fe695a665679f138c0fc4d520a984a",
        "bytes_are_zero": "6a6327e7774d4d0381673fcee5103704058d8b7004c210a70a93a6c0ace932f8",
        "names_equal": "ef7fea86fdcfd77b66286ffc82c02629e6ca93fee910e6cde5a3da40cc68c011",
        "profile_is_zero": "761f68556749775ba4c5f3d36467504043152d7ec479ce3ceede8857aea9dd35",
        "endpoint_record_is_zero": "d39596883511b84c438bc6bf982f1f139807229e51c221cace15b564beb787bb",
        "endpoint_ipc_state_is_zero": "b09d2ce358259123c32df75301a40027d706d70a550d33a9c903e3f5d42232db",
        "thread_handle_is_zero": "d2a7e5a954c3f0a4f0fc190b08efcf57f399c91194cc9ad75961c8a9a8ade3b9",
        "thread_handle_is_shallow_valid": "6d1ec96b91e22e394ce1c53d8078a267dfcfcd34e3293e6d573c2d8eaf9b827e",
        "queue_pair_is_shallow_valid": "821e784bc29ba6dae397dd439264c07a173a836171a1b90f7b3b4f20d8df413d",
        "pending_notification_state_is_shallow_valid": "35f4d2cba5df663d60ae4d72707958645866a8a4fbff66cd3d0cded50f29e189",
        "clear_endpoint_record": "813415bed3efa10882f244e32839c5a80dc780b53db0d3d6d7caf49020534258",
        "process_handles_equal": "1eb9ca3d14ed288168beacc0916c15eb03aa6ecf2e283e022b4d3ccb7e92f678",
        "thread_handles_equal": "1eb9ca3d14ed288168beacc0916c15eb03aa6ecf2e283e022b4d3ccb7e92f678",
        "canonicalize_profile": "f9d678d7e3677088f5b93ae77cf9c749262e003d89a084070237c59003e008df",
        "require_initialized": "089a103162bc65411f680f018d3a91921bae3f949da2d9a20be5c982a1a4fdc3",
        "micros_endpoint_pack": "47763584ff09470d018247fc7028fa26b8346cffca41235dafde2ae16a484c07",
        "micros_endpoint_unpack": "6d4bca7673f4219e751208129b47097125a1018a6f3d4742950c9565eb058cba",
        "micros_endpoint_registry_initialize": "36f99de5df6382b6837dcddd183bc3270127b4eda9067d714610aa491207c462",
        "micros_endpoint_registry_validate": "bbef9acd820d48752460606e0336405f0190e3f5af8171a17c4c8ff143ed1505",
        "micros_privilege_profile_resolve": "9e59321507346df6c1d8e986bc5c1190bfa823127ee5c4eedade0191dff0ee82",
        "micros_privilege_profile_allows_operation": "ca6fe20cb13cf5bd74bcb954c5c6521869b549f1406cb2c5b1bb12504855179a",
        "micros_privilege_profile_allows_target": "332da51e1f15a381d2e87308ceeb31eafeba2f288aa0a4f0e2c0ed26231c4901",
        "micros_privilege_profile_allows_kernel_operation": "5d18d6d14573bc9e4d0a124dbca2943e35bf81a7c4644bcdc76aeed35a27fd66",
        "process_resolve_error": "dabaca078270a7d3797491fc50c981e2bc3786120ba11636894fc98e4ef276ed",
        "endpoint_record_resolve_validated": "e4a7dbe35c93f9bdc7b4209a1423cf0cba10e2eeadd586f038e48dbc0b77acce",
        "process_has_current_thread": "2bb0d23258ac81dd73bafbc9eea7cb6ceaa84b316d77db2de652e1672d82e1d5",
        "process_threads_are_held": "6ffb1c40af6589874e18ad01bcf2ce7be20f0fa7671b68c4f02de1e5836ff0c1",
        "process_threads_have_clear_ipc_state": "04966d5dc9a8b932c8c5979ba8a75bf8b226536bedd441bfeb2dba7cc0d69534",
        "delivery_state_is_clear": "dd75048eae11724a34fcb5e08c4c44f2b0367437d3cbef1fe45b88da0b20ceea",
        "sender_queue_state_is_valid": "58b49d47e43499158c0611176edbd0a572700e19c28572b121c8b5e64896f468",
        "reply_wait_state_is_valid": "b839d1e6ea7a567eb9fd6668a526f72ab1af28d5923bb07b3a8b2fb9a6dad983",
        "source_only_process_threads_are_valid": "7422a162fad6fb85143b48b7afa320a59177696d2668022ca2079e6e47912863",
        "staged_reply_token_binding_is_valid": "f8df38be2d36d197fad92f4139e2227aefa9537b701a5dbdeac322b45a25d032",
        "notification_message_is_valid": "a99e4d504731b93a66a786334d89f9208f96c86f1a1d1a75e59411c64ca7d61e",
        "read_u32_le": "150bf59e50562ae2367a42a4b041af482d0e150f4a76e4b5ba0844ffd60ea172",
        "source_only_message_is_valid": "8025a71e6cbcf9ff281ed4cb470aa1eb2d945e3050c7ab07ee7504f3ae22ce05",
        "staged_delivery_state_is_valid": "1ca20a0467dee4ed962abc269e92f83351e3eab35b6495027a95a7363c54fd8f",
        "staged_error_state_is_valid": "a6627b631c17dde618f7b1df69acb60b5ea04bb78c0d5987eb126f5438ab6f72",
        "staged_no_message_success_state_is_valid": "06604397fa3b3077bb04cf4222badd207412869e943ca0b87e0b98df9ccde5cd",
        "thread_has_no_ipc_flags": "24170d66d706493e63e0578bbea46ece85aaf13dc3eba357ef5f51a0bb9ec638",
        "receiver_queue_state_is_valid": "f6d3ef3f6c738a38d9c2e702c937b4f2599d0218a01ba2b5a8c41978ae85c014",
        "endpoint_queue_is_valid": "a90356a3648fdcafb478ddc5d07ec7bc051bd30822b19ef8fa35bd51eb90c6a1",
        "pending_notification_state_is_valid": "6737adedc046d4c9f825c7c2bc73a08630cabaae743a2a93a9eed88ae62b8e50",
        "receiver_matches_pending_notification": "9c77b0597fb3c369d6f74859a7cd854d5463f5b708c2cf4bb6de18775b1ae0c5",
        "endpoint_has_matchable_work": "cbec16ae6fc1b537e32b71ac322238cbde4c3e9509aff91243b6be90564509a5",
        "micros_endpoint_registry_validate_objects": "9e369e69b5c609a2551ada44779c37e2677beabc0f9a68cf4fe2a86bdb128cbe",
        "micros_endpoint_reserve": "b3c4162bfcdf7a2bfb2b5c38b65c5b0c29d51100fbf0aa97e54c7ed207c91c25",
        "micros_endpoint_install_profile": "c6ef4cf284bf6e8d91191cd22171e0cf37dc0cfb46547723e9c2c7c99b64ca5c",
        "micros_endpoint_activate": "7a24c0b97af6969ffd60102476998aee6f4e23acd16dec904f326132b931b993",
        "micros_endpoint_preflight_publish": "62ceb05e484762ef80a2c53915d914eac26d6557be7f440642e6ce45e514a720",
        "micros_endpoint_commit_publish_prevalidated": "0c66249348626dbb22a733b55950fe0346dc606c0159f38c135635822be1fdd8",
        "micros_endpoint_preflight_source_only_for_teardown": "d931c9edde29b37ab73ace8e6ade6eb23f2f4f79833e7b331468ffcd162afa6f",
        "micros_endpoint_preflight_source_only": "8236830b93ef26c14773794410ee25459e196a9d3c79d9b92db51e9b62ed5d2c",
        "micros_endpoint_commit_source_only_prevalidated": "0510f2c0c974b8f6fa6b6cd7e332bc2e78e998d43ad9fead1d3e7c83bcfb1faf",
        "micros_endpoint_seal_source_only": "7e7921a29301ed705795a66ad3c8996b5ce77e6e7051f968950ef9b79bf1d4b0",
        "micros_endpoint_resolve_internal": "8aef483fa258cdf17982e5865417b595ca77184ba6ddd0fb00d1172e856b07ee",
        "micros_endpoint_resolve_active": "71f19dbc49f41172d904929c97e6096299a5fc9b5d6ac1a37ae1e53d3c25a3cb",
        "endpoint_has_foreign_waiters": "33352a5482643303bbd8835feabf0c7b6366d78ed4f2b5988c94685dd676313c",
        "endpoint_is_pending_notification_source": "08fb36f05df5fb36bd3cef2b411d85cc599581f9f2b040886e510d1e1e698983",
        "micros_endpoint_close_commit_prevalidated": "b474953f2dec89d73cf9c19724d9073b8a011878d994e726e819912abd31f281",
        "micros_endpoint_close": "8b09d97d3dce88bfec98ccb42497f54fcd6cbf85c0724b430a43980809d1946a",
        "micros_endpoint_authorize_operation": "344262dd2e4239f5e7da7eb96ade547790da9cb70c1e96f69c593e4854fca3b3",
        "micros_endpoint_authorize_target": "deccb03421ec01f7da206b00d9219677d458894e07d46616b391408ef09380b4",
        "micros_endpoint_authorize_kernel_operation": "b1f67da1b7a3051fb629cc7e23b218c25e761f50fee2ee2a62b4f9dc59696170",
    },
    1: {
        "profile": "defa6a0a91b3ab2b3f8280c6364fdcf2c2388e159309680dbe43baa6165012e4",
        "context_pattern": "3f6a242e713f17830c5f0dbbb17f623169f99345a04d1afea87d72739545ee85",
        "write_u32_le": "e8f941db33e782362b97a427da21a66baa14b3f007243e42f90ad8b386a9d811",
        "setup_lifecycle_fixture": "76b557abe77321a4ee16138f547f48cc71821c9270be106f6bd4abebf3e105d0",
        "test_endpoint_encoding_boundaries": "3d4d26911cf3ffad6ca3e2aa791f593db0f7da05fdb2cabd91d1822cf56ecf74",
        "test_profile_table_is_immutable_and_specific": "c92bb44870ad469a4ec731d5d2012db7cb3147b13e7770c90730d8bfc0207bdf",
        "test_invalid_profile_tables_preserve_storage": "bfcf55e53427604dc3c1150411dc7661b1dc732d6a5320363ed00bac4f574d91",
        "test_validator_rejects_corruption": "3bbe0654dc7e0fbffc753503eb6b02fc96b115381c52cf895d68f019ed8b9467",
        "test_endpoint_lifecycle_and_authorization": "a6222d1c3a2bba46ffee55e58c30c18b7b3b00ef46aa809e1f49f277c3b0e8a3",
        "test_profile_install_and_close_require_held_threads": "587c901cdba3bc8c9b7d4f5deb8f600e9df048f3383bdab7422c67dd665e99e6",
        "test_resolution_rejects_unimplemented_slots": "cbf057bcf30a7524ff457ec8d33c723ab149526e9ad804695d3c2591e7dff934",
        "test_endpoint_reuse_rejects_stale_generation": "5c64c983cec2804a8ce609dc10032bbdd4d4de7d8fb2e960c0b04781a9f6defb",
        "test_source_only_seal_preserves_staged_source": "b7fdd34a3854835e9198e8c64cee5eb68bc4fe2e692412f618f5369ff2b4fe4f",
        "test_relationship_validator_rejects_corruption": "7f1177c07f24a34287796477c8b77328238515e0c6b39178d7f3437f725b14c5",
        "main": "25223aa768f9edb0a2a32d6c29876ee4661f5f54f42ad1951c8c64e96978be9b",
    },
    2: {
        "next_random": "88d510643c80dabe81f46f60351cf9cbf6fbb71a36caa967392e9c72ad448cca",
        "record_transition": "8e2cbf5c3c04282896a8b29fbf10e1d3cf4d509da1e5ff120b48a3a659b86f64",
        "model_fail": "facbcaeadb0d6a364855ce623defa690a9a249ce084a091ef6f1c2bfe5e9df36",
        "setup_fixture": "c52e49efb637c05a9942ec9f975736ec6b390a62261e946b6d19b6cae049b8dc",
        "phase_has_endpoint": "280e52e8f9b6426440ea304d646de80efd41d61241e5c8a0e7a7146c056d5105",
        "find_slot": "69d326ff2956681e3b244235459ada501435235f692fa8abb04265eeef203892",
        "slot_is_live": "cba00e84c0cfa599ede53cf5235cf29cf7f106b00556780f299481b2c2c022eb",
        "slot_has_endpoint": "0343706efc3740b4bf1226207ff0805007ab2c417b17f028035f3de5873521e3",
        "slot_has_nonactive_endpoint": "f96e9bdd07c3da173bf2628162d7112b0917f120647b7578250966d1cd7e21f2",
        "slot_is_releasable": "af0aa57cb498e30fc8e1fecd9b39184dc3388388ba92cfd8a52b1ea6e714a57d",
        "slot_has_stale_endpoint": "6bdf3ddcca63a5d70fa98c6f2865f604266c0da53366fa5b95f81817f2defb2d",
        "find_active_profile": "a4291fe6e03d673bad0e8b9b95e059b1efeaaa1fd024b360f8bb8a25f573f9f4",
        "target_is_allowed": "d5aacf3f424825972d72d6acbc3ad43865c1fe22fe2c2b386a79846714a76fae",
        "validate_model": "0bf979165663dead8ba6668e828a04c90ca1a8ab28c8e46e5d62fcab923e323b",
        "micros_endpoint_model_test_run": "5cf37e63a33cdde472e3da390a70c1fe5f6b5466b2f2f48e1e18da6c8f197bb2",
    },
    3: {
        "micros_endpoint_runtime_run_self_test": "ebd3fad5a36e078747d2dabcf78bf29234c4bcf95b3a8794b7541a788885770f",
    },
}
STABLE_HASHES = {
    "include/micros/endpoint.h": "57e8c5b5536ddc270015be7e2ccef07008e6d00ae514e27fb3c7ebc55c1f871c",
    "kernel/endpoint_internal.h": "fa8bc084ed5b6777a805abb4def2d8d20f747a3b193cbc9fa46a9b6c1145caac",
    "kernel/ipc_runtime.c": "c602cbf941bde404a27d88e4b19391d7c2ffa795f1dc730ae51b79e759b8ac41",
    "kernel/ipc_runtime_internal.h": "da59e3c038efd7214ab914041ee06ae77c74bc350b60eead84f84e09d422eb6f",
    "include/micros/ipc_runtime.h": "995cabdf5ffd3faf64a39617c010dc0879f06e93e57039f506568e7209172fc2",
    "kernel/grant_runtime.c": "3e9ca759068b2f0f7e4c2c4a75a0a9b65d86a4717bb95e543dbfa0ef9c5ae267",
    "kernel/grant_runtime_internal.h": "03ada0e660616efea7c6d97cffbd803a4a07c253fbe7a515dbbd25459a2cc816",
    "include/micros/grant_runtime.h": "1c1d153c4a8935f22963a506c93a8b62d72303b89d671634f4ac2672d8f7abb2",
    "kernel/kernel_objects.c": "453c3cfbfb02d1c014226a315106a157676a7b195be06f561bb3abbda042db4a",
    "include/micros/kernel_objects.h": "106fe5aaa03b06b1746c73aa24588749d4cfe02c6b1e4a11efc1f5317f7523e3",
    "kernel/grant.c": "00da274e5ec7ef45e03a68392bf33d32ba7b9eb2b5218e8ca9908a582291e707",
    "include/micros/grant.h": "9a3723ad14c99db1d54b5b2cb4d051a42a191379f33547a44b5ff769d8346d29",
    "tests/host/ipc_model_main.c": "451170f1a93a19ac85d347121257d89817de1ff2e928990d514fc3b719a813f3",
    "CMakePresets.json": "eae6c86ce5ac8f0e1b39605ebbd6816135558f0b4f8ea293b3680d7ecb79df86",
    "tools/run_qemu_smoke.py": "9e54f744074d077d05b52880495b73f7c74ae1db0a6ad751f8b7024688bf79e3",
}


def source(path):
    return kernel_layout_source.read_path(ROOT / path)


def digest_bytes(data):
    return hashlib.sha256(data).hexdigest()


def family_paths(family):
    return tuple(SOURCE_SETS.values())[family]


def alias_rows():
    return tuple(
        (paths[0], (*paths[1:], PRIVATE_HEADERS[family]))
        for family, paths in enumerate(SOURCE_SETS.values())
    )


def private_name(name):
    return name if name.startswith("endpoint_") else "endpoint_" + name


def native_name(name):
    if name == "main":
        return name
    if name.startswith("test_endpoint_"):
        name = name[len("test_endpoint_"):]
    elif name.startswith("test_"):
        name = name[len("test_"):]
    return "endpoint_test_" + name


def normal_names(family):
    if family == 0:
        return {private_name(name): name for name in CORE_PRIVATE_EXPORTS}
    if family == 1:
        return {native_name(name): name for names in ORIGINAL_FUNCTIONS[1].values() for name in names}
    if family == 2:
        return {
            "endpoint_model_" + name: name for names in ORIGINAL_FUNCTIONS[2].values() for name in names
            if name != "micros_endpoint_model_test_run"
        }
    return {}


def body_digest(text, renames=None):
    names = renames or {}
    body = tuple(names.get(token, token) for token in tokens(text))
    return digest_bytes(json.dumps(body, separators=(",", ":")).encode())


def owned_definitions(family):
    return {
        (path, name): lines
        for path in family_paths(family) if kernel_layout_source.is_file_path(ROOT / path)
        for name, lines in c_function_definitions(source(path)).items()
    }


def original_locations(family):
    expected = {name for names in ORIGINAL_FUNCTIONS[family].values() for name in names}
    renames = normal_names(family)
    locations = {}
    for path, spelling in owned_definitions(family):
        if family == 1 and any(
            spelling in specification["phases"] for specification in NATIVE_CASE_WORK.values()
        ):
            owners = [
                family_paths(1)[specification["owner"]] for specification in NATIVE_CASE_WORK.values()
                if spelling in specification["phases"]
            ]
            if owners != [path]:
                raise AssertionError("native case phases retain their exact case owner")
            continue
        if family == 0 and spelling in INITIALIZER_HELPERS:
            if path != family_paths(0)[3]:
                raise AssertionError("profile initialization phases retain one exact owner")
            continue
        if family == 0 and spelling in VALIDATOR_HELPER_OWNERS:
            if path != family_paths(0)[VALIDATOR_HELPER_OWNERS[spelling]]:
                raise AssertionError("validation phases retain their exact file-local owners")
            continue
        if family == 1 and spelling == "endpoint_test_context":
            if path != family_paths(1)[1]:
                raise AssertionError("native fixture getter has one exact owner")
            continue
        if family == 2 and spelling == "endpoint_model_context":
            if path != family_paths(2)[1]:
                raise AssertionError("model fixture getter has one separate exact owner")
            continue
        if family == 2 and spelling in MODEL_PROJECTION_HELPERS:
            if path != family_paths(2)[3]:
                raise AssertionError("complete model projection tasks retain their exact owner")
            continue
        if family == 2 and spelling in (*MODEL_RUN_HELPERS, *MODEL_OPERATION_HELPERS.values(), *MODEL_AUTHORIZATION_HELPERS):
            owner = 0 if spelling in MODEL_RUN_HELPERS else 4
            if path != family_paths(2)[owner]:
                raise AssertionError("model run/operation tasks retain their exact control or operation owner")
            continue
        if family == 3 and spelling in (*QEMU_FIXTURE_TASKS, *QEMU_SCENARIO_TASKS):
            owner = 1 if spelling in QEMU_FIXTURE_TASKS else 2
            if path != family_paths(3)[owner]:
                raise AssertionError("target preparation/scenario tasks retain their exact owners")
            continue
        name = renames.get(spelling, spelling)
        if name not in expected or name in locations:
            raise AssertionError(f"unknown/duplicate original or unreviewed phase: {family}/{spelling}")
        locations[name] = (path, spelling)
    if set(locations) != expected:
        raise AssertionError("missing original definitions")
    return locations


def fixture_field_tokens(body, fields):
    body = list(body)
    result = []
    index = 0
    while index < len(body):
        if body[index:index + 2] == ["fixture", "->"]:
            if index != 0 and body[index - 1] in (".", "->"):
                raise AssertionError("fixture transport cannot qualify a structure member as a global root")
            field = body[index + 2:index + 3]
            if not field or field[0] not in fields:
                raise AssertionError("fixture may transport only its five original fields")
            result.extend(field)
            index += 3
        else:
            if body[index] == "fixture":
                raise AssertionError("fixture cannot be copied, blanket-cleared, published or retained")
            result.append(body[index])
            index += 1
    return tuple(result)


def native_fixture_body_tokens(name, body):
    body = list(body)
    borrow = list(tokens("struct endpoint_test_fixture *fixture = endpoint_test_context();"))
    if name in NATIVE_CONTEXT_CASES:
        if body[:1 + len(borrow)] != ["{"] + borrow:
            raise AssertionError("native case roots borrow exactly one fixture without resetting it")
        del body[1:1 + len(borrow)]
    result = []
    index = 0
    call = list(tokens("setup_lifecycle_fixture(fixture)"))
    while index < len(body):
        if body[index:index + len(call)] == call:
            result.extend(tokens("setup_lifecycle_fixture()"))
            index += len(call)
        else:
            if body[index:index + 2] == ["setup_lifecycle_fixture", "("]:
                raise AssertionError("native setup must receive the exact borrowed fixture")
            result.append(body[index])
            index += 1
    return fixture_field_tokens(result, NATIVE_CONTEXT_FIELDS)


def model_fixture_body_tokens(name, body):
    body = list(body)
    borrow = list(tokens("struct endpoint_model_fixture *fixture = endpoint_model_context();"))
    if name == "micros_endpoint_model_test_run":
        if body[:1 + len(borrow)] != ["{"] + borrow:
            raise AssertionError("model entry borrows only its separate existing production/trace fixture")
        del body[1:1 + len(borrow)]
    result = []
    index = 0
    while index < len(body):
        token = body[index]
        if token in MODEL_CONTEXT_HELPERS and body[index + 1:index + 2] == ["("]:
            delimiter = ")" if MODEL_CONTEXT_HELPERS[token] else ","
            if body[index + 2:index + 4] != ["fixture", delimiter]:
                raise AssertionError("model helper must receive the exact fixture as first argument")
            result.extend((token, "("))
            index += 3 if delimiter == ")" else 4
        else:
            result.append(token)
            index += 1
    return fixture_field_tokens(result, MODEL_CONTEXT_FIELDS)


def validation_phase_body_tokens(name, caller, phases):
    caller = list(tokens(caller))
    if name == "micros_endpoint_registry_validate":
        current = list(tokens("enum micros_endpoint_error error;"))
        if caller[:1 + len(current)] != ["{"] + current:
            raise AssertionError("shallow validation retains only its original caller error temporary")
        caller[1:1] = tokens(REGISTRY_VALIDATION_DECLARATIONS)
    for (phase, arguments, declarations, first_index), body in phases:
        call = list(tokens(
            "if (!" + phase + "(" + ", ".join(arguments) + ")) { return MICROS_ENDPOINT_ERROR_INVARIANT; }"
        ))
        positions = [index for index in range(len(caller)) if caller[index:index + len(call)] == call]
        if len(positions) != 1:
            raise AssertionError("validation phases retain exact caller error and witness bindings")
        body = list(tokens(body))
        locals_ = list(tokens(declarations))
        success = list(tokens("return true;"))
        if body[:1 + len(locals_)] != ["{"] + locals_ or body[-len(success) - 1:] != success + ["}"]:
            raise AssertionError("validation phases own only original automatic counters and terminal success")
        inner = body[1 + len(locals_):-len(success) - 1]
        first = inner.index("index") if "index" in inner else None
        if first is None or inner[first - 2:first + 3] != ["for", "(", "index", "=", str(first_index)]:
            raise AssertionError("validation counter must reset to its original first bound before use")
        expanded = []
        index = 0
        while index < len(inner):
            if inner[index] == "return":
                if inner[index:index + 3] != ["return", "false", ";"]:
                    raise AssertionError("helper-local success must never bypass caller validation")
                expanded.extend(tokens("return MICROS_ENDPOINT_ERROR_INVARIANT;"))
                index += 3
            else:
                expanded.append(inner[index])
                index += 1
        position = positions[0]
        caller[position:position + len(call)] = expanded
    return tuple(caller)


def initialization_body_tokens(caller, prepare, closed, publish):
    caller = list(tokens(caller))
    prefix = list(tokens(
        "struct micros_privilege_profile candidate_profiles[MICROS_PRIVILEGE_PROFILE_CAPACITY]; "
        "uint32_t installed = 0;"
    ))
    if caller[:1 + len(prefix)] != ["{"] + prefix:
        raise AssertionError("initialization retains its one caller-owned candidate array and empty mask")
    caller[1 + len(prefix):1 + len(prefix)] = tokens("size_t source_index; size_t other_index;")
    specifications = (
        (
            "prepare_profile_candidates(profiles, profile_count, candidate_profiles, &installed)",
            prepare, "size_t source_index; size_t other_index;", 0,
        ),
        (
            "candidate_targets_are_closed(candidate_profiles, installed)",
            closed, "size_t source_index;", 1,
        ),
    )
    for call_text, body, declarations, first_index in specifications:
        call = list(tokens("if (!" + call_text + ") { return MICROS_ENDPOINT_ERROR_PROFILE; }"))
        positions = [index for index in range(len(caller)) if caller[index:index + len(call)] == call]
        if len(positions) != 1:
            raise AssertionError("candidate preparation/closure preserve exact caller inputs and profile failure")
        body = list(tokens(body))
        locals_ = list(tokens(declarations))
        success = list(tokens("return true;"))
        if body[:1 + len(locals_)] != ["{"] + locals_ or body[-len(success) - 1:] != success + ["}"]:
            raise AssertionError("candidate phases retain only original automatic counters and terminal success")
        inner = body[1 + len(locals_):-len(success) - 1]
        first = inner.index("source_index") if "source_index" in inner else None
        if first is None or inner[first - 2:first + 3] != ["for", "(", "source_index", "=", str(first_index)]:
            raise AssertionError("candidate counter retains its original initial bound")
        expanded = []
        index = 0
        while index < len(inner):
            if inner[index] == "return":
                if inner[index:index + 3] != ["return", "false", ";"]:
                    raise AssertionError("candidate-local success cannot skip caller validation")
                expanded.extend(tokens("return MICROS_ENDPOINT_ERROR_PROFILE;"))
                index += 3
            elif first_index == 0 and inner[index:index + 2] == ["*", "installed"]:
                expanded.append("installed")
                index += 2
            else:
                if first_index == 0 and inner[index] == "installed":
                    raise AssertionError("candidate mask transport must dereference its exact scalar output")
                expanded.append(inner[index])
                index += 1
        position = positions[0]
        caller[position:position + len(call)] = expanded
    call = list(tokens("publish_profile_table(registry, candidate_profiles, profile_count);"))
    positions = [index for index in range(len(caller)) if caller[index:index + len(call)] == call]
    publish = list(tokens(publish))
    if len(positions) != 1 or publish[:1] != ["{"] or publish[-1:] != ["}"] or "return" in publish:
        raise AssertionError("publication is one exact non-failing caller commit")
    size = list(tokens("sizeof(registry->profiles)"))
    sizes = [index for index in range(len(publish)) if publish[index:index + len(size)] == size]
    if len(sizes) != 1:
        raise AssertionError("array publication must use the exact frozen full registry profile extent")
    publish[sizes[0]:sizes[0] + len(size)] = tokens("sizeof(candidate_profiles)")
    caller[positions[0]:positions[0] + len(call)] = publish[1:-1]
    return tuple(caller)


def raw_native_runner_table(text):
    lexical = c_lexical_text(text)
    candidates = tuple(re.finditer(
        r"(?m)^static const struct\s*\{[\s\S]*?\}\s*tests\[\]\s*=\s*\{", lexical,
    ))
    if len(candidates) != 1:
        raise AssertionError("native runner owns one exact file-static readonly case table")
    match = candidates[0]
    if lexical.count("{", 0, match.start()) != lexical.count("}", 0, match.start()):
        raise AssertionError("runner data must have file scope rather than remain a long entry prefix")
    start = match.end() - 1
    expected = tokens("static const struct { const char *name; bool (*run)(void); } tests[] = {")
    if tokens(text[match.start():start + 1]) != expected:
        raise AssertionError("runner table retains exact const, callback and label types")
    depth = 0
    for index in range(start, len(lexical)):
        if lexical[index] == "{":
            depth += 1
        elif lexical[index] == "}":
            depth -= 1
            if depth == 0:
                end = index + 1
                while end < len(lexical) and lexical[end].isspace():
                    end += 1
                if end >= len(lexical) or lexical[end] != ";":
                    raise AssertionError("runner table must end at its exact declaration")
                return text[match.start():end + 1]
    raise AssertionError("unterminated readonly native case table")


def native_runner_table(text):
    raw = raw_native_runner_table(text)
    if body_digest(raw, normal_names(1)) != NATIVE_RUNNER_TABLE_DIGEST:
        raise AssertionError("actual raw native runner labels, functions or order differ from immutable baseline")
    return raw


def native_work_field_tokens(body, specification):
    body = list(body)
    result = []
    index = 0
    while index < len(body):
        if body[index:index + 2] in (["work", "->"], ["work", "."]):
            if index != 0 and body[index - 1] in (".", "->"):
                raise AssertionError("work transport cannot turn an existing structure member into a global root")
            field = body[index + 2:index + 3]
            if not field or field[0] not in specification["fields"]:
                raise AssertionError("native work contains only the original typed automatic objects")
            result.extend(field)
            index += 3
        else:
            if body[index] == "work":
                raise AssertionError("native work cannot be copied, blanket-cleared, retained or published")
            result.append(body[index])
            index += 1
    return tuple(result)


def rejected_table_macro(text, specification):
    matches = tuple(re.finditer(r"(?m)^#define EXPECT_REJECTED_TABLE\(", text))
    if len(matches) != 1:
        raise AssertionError("rejection cases own one exact file-local mutation/preservation macro")
    start = matches[0].start()
    lexical = c_lexical_text(text)
    if lexical.count("{", 0, start) != lexical.count("}", 0, start):
        raise AssertionError("outlined rejection macro has file scope only in its owning case module")
    end = start
    while True:
        newline = text.find("\n", end)
        if newline == -1:
            raise AssertionError("unterminated rejection macro")
        continued = text[end:newline].endswith("\\")
        end = newline + 1
        if not continued:
            break
    raw = text[start:end].rstrip()
    restored = native_work_field_tokens(tokens(raw), specification)
    restored = fixture_field_tokens(restored, NATIVE_CONTEXT_FIELDS)
    actual = digest_bytes(json.dumps(restored, separators=(",", ":")).encode())
    if actual != REJECTED_TABLE_MACRO_DIGEST:
        raise AssertionError("actual rejection macro changed reset/snapshot/action/comparison or literal behavior")
    if len(re.findall(r"(?m)^#undef EXPECT_REJECTED_TABLE\s*$", text)) != 1:
        raise AssertionError("rejection macro has exactly one bounded file-local lifetime")
    return raw


def native_case_body_tokens(caller, specification, bodies, macro=None):
    caller = list(tokens(caller))
    borrow = list(tokens("struct endpoint_test_fixture *fixture = endpoint_test_context();"))
    outlined = list(tokens(specification["outlined"]))
    if caller[:1 + len(borrow) + len(outlined)] != ["{"] + borrow + outlined:
        raise AssertionError("native work is automatic and initializes only its original declared values")
    tail = list(tokens(
        "return " + " && ".join(
            phase + (
                "(fixture)" if phase in specification.get("fixture_only", ())
                else "(&work)" if phase in specification.get("work_only", ())
                else "(fixture, &work)"
            )
            for phase in specification["phases"]
        ) + ";"
    ))
    if caller != ["{"] + borrow + outlined + tail + ["}"]:
        raise AssertionError("native case phases preserve ordered first-failure AND and exact work binding")
    result = ["{"] + borrow + list(tokens(specification["original"]))
    success = list(tokens("return true;"))
    if len(bodies) != len(specification["phases"]):
        raise AssertionError("native case phase inventory is exact")
    for phase_index, body in enumerate(bodies):
        if specification.get("macro_before") == phase_index:
            if macro is None:
                raise AssertionError("macro reconstruction requires the verified actual source definition")
            result.extend(tokens(macro))
        if specification.get("undef_before") == phase_index:
            result.extend(tokens("#undef EXPECT_REJECTED_TABLE"))
        body = list(tokens(body))
        if body[:1] != ["{"] or body[-len(success) - 1:] != success + ["}"]:
            raise AssertionError("native phase retains one terminal success")
        inner = body[1:-len(success) - 1]
        for index, token in enumerate(inner):
            if token == "return" and inner[index:index + 3] != ["return", "false", ";"]:
                raise AssertionError("callee-local success cannot bypass later native case work")
        if "goto" in inner:
            raise AssertionError("native case phases cannot borrow caller labels")
        result.extend(inner)
    result.extend(success + ["}"])
    return native_work_field_tokens(result, specification)


def model_projection_body_tokens(caller, live, endpoint):
    caller = list(tokens(caller))
    calls = (
        (
            "endpoint_model_live_process_and_thread_match(fixture, expected, process, step, operation, value)",
            live, False,
        ),
        (
            "endpoint_model_phase_matches(fixture, expected, process, endpoint, step, operation, value)",
            endpoint, True,
        ),
    )
    success = list(tokens("return true;"))
    for call_text, body, phase_exits in calls:
        call = list(tokens("if (!" + call_text + ") { return false; }"))
        positions = [index for index in range(len(caller)) if caller[index:index + len(call)] == call]
        if len(positions) != 1:
            raise AssertionError("model projection preserves exact borrowed objects and caller first failure")
        body = list(tokens(body))
        if body[:1] != ["{"] or body[-len(success) - 1:] != success + ["}"]:
            raise AssertionError("model projection task retains one terminal success")
        inner = body[1:-len(success) - 1]
        if "continue" in inner or "goto" in inner:
            raise AssertionError("model helpers cannot borrow caller loop or label scope")
        if phase_exits:
            for phase in ("MODEL_ENDPOINT_UNUSED", "MODEL_ENDPOINT_CLOSED"):
                head = list(tokens("if (expected->endpoint_phase == " + phase + ") {"))
                starts = [index for index in range(len(inner)) if inner[index:index + len(head)] == head]
                if len(starts) != 1:
                    raise AssertionError("model phase early success has one exact caller-continue context")
                opening = starts[0] + len(head) - 1
                depth = 0
                end = None
                for index in range(opening, len(inner)):
                    depth += int(inner[index] == "{") - int(inner[index] == "}")
                    if depth == 0:
                        end = index
                        break
                if end is None or inner[end - len(success):end] != success:
                    raise AssertionError("unused/closed success must terminate only its original slot phase")
                inner[end - len(success):end] = tokens("continue;")
        if any(inner[index:index + len(success)] == success for index in range(len(inner))):
            raise AssertionError("helper success cannot bypass remaining slots or aggregate comparison")
        caller[positions[0]:positions[0] + len(call)] = inner
    return tuple(caller)


def model_script_data(text):
    lexical = c_lexical_text(text)
    declarations = []
    for name, type_ in (("scripted_operations", "enum model_operation"), ("scripted_values", "uint32_t")):
        matches = tuple(re.finditer(
            r"(?m)^static const " + type_ + r"\s+" + name + r"\[\]\s*=\s*\{[^{}]*\};", lexical,
        ))
        if len(matches) != 1:
            raise AssertionError("model scripts retain one exact file-static readonly array each")
        match = matches[0]
        if lexical.count("{", 0, match.start()) != lexical.count("}", 0, match.start()):
            raise AssertionError("outlined draw tasks share readonly scripts only within their original owner")
        declarations.append(text[match.start():match.end()])
    raw = "\n".join(declarations)
    if body_digest(raw) != MODEL_SCRIPT_DATA_DIGEST:
        raise AssertionError("actual model script types, seventeen operations/values or order changed")
    return raw


def replace_one_token_sequence(body, current, replacement):
    body = list(body)
    current = list(tokens(current)) if isinstance(current, str) else list(current)
    positions = [index for index in range(len(body)) if body[index:index + len(current)] == current]
    if len(positions) != 1:
        raise AssertionError("finite phase expansion requires one exact typed caller binding and exit")
    start = positions[0]
    body[start:start + len(current)] = replacement
    return body


def phase_inner_tokens(body, allow_success=False):
    body = list(tokens(body)) if isinstance(body, str) else list(body)
    success = list(tokens("return true;"))
    if body[:1] != ["{"] or body[-len(success) - 1:] != success + ["}"]:
        raise AssertionError("model tasks have one terminal helper success")
    inner = body[1:-len(success) - 1]
    if not allow_success and any(inner[index:index + len(success)] == success for index in range(len(inner))):
        raise AssertionError("model helper success may replace only its original switch break")
    if "goto" in inner:
        raise AssertionError("model helpers cannot borrow another caller's label scope")
    return inner


def scalar_output_tokens(body, outputs):
    body = list(body)
    result = []
    index = 0
    while index < len(body):
        if body[index] == "*" and body[index + 1:index + 2] and body[index + 1] in outputs:
            result.append(body[index + 1])
            index += 2
        else:
            if body[index] in outputs:
                raise AssertionError("authorization scalar outputs retain exact dereferenced caller bindings")
            result.append(body[index])
            index += 1
    return result


def model_work_body_tokens(body):
    result = list(body)
    for variable, fields in (("run", MODEL_RUN_FIELDS), ("iteration", MODEL_ITERATION_FIELDS)):
        restored = []
        index = 0
        while index < len(result):
            if result[index:index + 2] in ([variable, "->"], [variable, "."]):
                if index != 0 and result[index - 1] in (".", "->"):
                    raise AssertionError("model automatic work cannot qualify an existing member as a root")
                field = result[index + 2:index + 3]
                if not field or field[0] not in fields:
                    raise AssertionError("model work contains only its original typed automatic fields")
                restored.extend(field)
                index += 3
            else:
                if result[index] == variable:
                    raise AssertionError("model automatic work cannot be copied, cleared, published or retained")
                restored.append(result[index])
                index += 1
        result = restored
    return tuple(result)


def model_operation_body_tokens(operation, body, authorization):
    authorize = operation == "MODEL_OPERATION_AUTHORIZE"
    inner = phase_inner_tokens(body, allow_success=authorize)
    if authorize:
        for helper in MODEL_AUTHORIZATION_HELPERS[:2]:
            call = (
                "return " + helper
                + "(fixture, run, iteration, &authorization_operation, &expected, &error);"
            )
            expanded = phase_inner_tokens(authorization[helper], allow_success=True) + list(tokens("return true;"))
            expanded = scalar_output_tokens(expanded, ("authorization_operation", "expected", "error"))
            inner = replace_one_token_sequence(inner, call, expanded)
        select = list(tokens(authorization[MODEL_AUTHORIZATION_HELPERS[2]]))
        if select[:1] != ["{"] or select[-1:] != ["}"] or "return" in select:
            raise AssertionError("authorization selection is the original non-failing target assignment sequence")
        select = scalar_output_tokens(select[1:-1], ("target", "authorization_operation"))
        inner = replace_one_token_sequence(
            inner,
            "endpoint_model_select_authorization_target(run, iteration, target_random, &target, &authorization_operation);",
            select,
        )
        selected = scalar_output_tokens(
            phase_inner_tokens(authorization[MODEL_AUTHORIZATION_HELPERS[3]]), ("expected", "error"),
        )
        inner = replace_one_token_sequence(
            inner,
            "if (!endpoint_model_authorize_selected_target(fixture, run, iteration, target, authorization_operation, "
            "&expected, &error)) { return false; }",
            selected,
        )
        success = list(tokens("return true;"))
        expanded = []
        index = 0
        while index < len(inner):
            if inner[index:index + len(success)] == success:
                expanded.extend(tokens("break;"))
                index += len(success)
            else:
                expanded.append(inner[index])
                index += 1
        return ("{", *expanded, *tokens("break;"), "}")
    return (*inner, *tokens("break;"))


def model_dispatch_body_tokens(caller, operations, authorization):
    body = list(tokens(caller))
    if body[:1] != ["{"] or body[-1:] != ["}"]:
        raise AssertionError("operation dispatcher retains its complete original switch")
    for operation, helper in MODEL_OPERATION_HELPERS.items():
        current = "case " + operation + ": return " + helper + "(fixture, run, iteration);"
        expanded = (*tokens("case " + operation + ":"), *model_operation_body_tokens(operation, operations[helper], authorization))
        body = replace_one_token_sequence(body, current, expanded)
    return body[1:-1]


def model_draw_body_tokens(body):
    body = list(tokens(body))
    if body[:1] != ["{"] or body[-1:] != ["}"] or "return" in body:
        raise AssertionError("model draw/script task retains its original non-failing sequence")
    inner = body[1:-1]
    index = 0
    for field, type_ in (
        ("random", "uint32_t"), ("operation", "enum model_operation"), ("slot", "size_t"), ("value", "uint32_t"),
    ):
        head = list(tokens("iteration->" + field + " ="))
        if inner[index:index + len(head)] != head:
            raise AssertionError("iteration initializes the original random/operation/slot/value in exact order")
        prefix = list(tokens(type_))
        inner[index:index] = prefix
        index = inner.index(";", index + len(prefix) + len(head)) + 1
    return inner


def model_runner_body_tokens(caller, phases, operations, authorization, scripts):
    body = list(tokens(caller))
    borrow = "struct endpoint_model_fixture *fixture = endpoint_model_context();"
    seed = "const uint32_t seed = UINT32_C(0xe029cafe);"
    current = list(tokens(borrow + seed + "struct endpoint_model_run run; run.random_state = seed;"))
    if body[:1 + len(current)] != ["{"] + current:
        raise AssertionError("model root keeps its single fixture borrow, constant seed and original seed initialization")
    body[1:1 + len(current)] = tokens(borrow + seed + scripts + MODEL_RUN_DECLARATIONS)
    body = replace_one_token_sequence(
        body, "if (!endpoint_model_setup_run(fixture, &run)) { return false; }",
        phase_inner_tokens(phases["endpoint_model_setup_run"]),
    )
    body = replace_one_token_sequence(
        body, "{ struct endpoint_model_iteration iteration; endpoint_model_draw_iteration(&run, &iteration);",
        ("{", *model_draw_body_tokens(phases["endpoint_model_draw_iteration"])),
    )
    body = replace_one_token_sequence(
        body, "if (!endpoint_model_apply_operation(fixture, &run, &iteration)) { return false; }",
        model_dispatch_body_tokens(phases["endpoint_model_apply_operation"], operations, authorization),
    )
    body = replace_one_token_sequence(
        body, "if (!endpoint_model_cleanup_run(fixture, &run)) { return false; }",
        phase_inner_tokens(phases["endpoint_model_cleanup_run"]),
    )
    coverage = list(tokens(phases["endpoint_model_coverage_complete"]))
    phase_inner_tokens(coverage)
    body = replace_one_token_sequence(body, "return endpoint_model_coverage_complete(&run);", coverage[1:-1])
    return model_work_body_tokens(body)


def qemu_profile_data(text):
    lexical = c_lexical_text(text)
    objects = []
    for name in (r"profiles\[2\]", "invalid_profile"):
        matches = tuple(re.finditer(
            r"(?m)^static const struct micros_privilege_profile " + name + r"\s*=\s*\{", lexical,
        ))
        if len(matches) != 1:
            raise AssertionError("target fixture owns one exact readonly profile table and invalid profile")
        match = matches[0]
        if lexical.count("{", 0, match.start()) != lexical.count("}", 0, match.start()):
            raise AssertionError("target readonly profiles share only their fixture owner's file scope")
        opening = match.end() - 1
        depth = 0
        closing = None
        for index in range(opening, len(lexical)):
            depth += int(lexical[index] == "{") - int(lexical[index] == "}")
            if depth == 0:
                closing = index
                break
        if closing is None:
            raise AssertionError("unterminated target profile initializer")
        end = closing + 1
        while end < len(lexical) and lexical[end].isspace():
            end += 1
        if lexical[end:end + 1] != ";":
            raise AssertionError("target profile objects end at their exact original declarations")
        objects.append(text[match.start():end + 1])
    raw = "\n".join(objects)
    if body_digest(raw) != QEMU_PROFILE_DATA_DIGEST:
        raise AssertionError("actual target profile names, types, masks, literals or order changed")
    return raw


def qemu_failure_condition_tokens(body):
    inner = phase_inner_tokens(body)
    if inner[:2] != ["if", "("]:
        raise AssertionError("split runtime predicates contain only their one original failure condition")
    depth = 0
    closing = None
    for index in range(1, len(inner)):
        depth += int(inner[index] == "(") - int(inner[index] == ")")
        if depth == 0:
            closing = index
            break
    if closing is None or inner[closing + 1:] != list(tokens("{ return false; }")):
        raise AssertionError("split runtime predicate preserves its sole failure and non-failing completion")
    return inner[2:closing]


def qemu_phase_body_tokens(body):
    inner = phase_inner_tokens(body)
    result = []
    index = 0
    while index < len(inner):
        if inner[index] == "return":
            if inner[index:index + 3] != ["return", "false", ";"]:
                raise AssertionError("target task failure returns only to the entry's common done label")
            result.extend(tokens("goto done;"))
            index += 3
        else:
            result.append(inner[index])
            index += 1
    return result


def qemu_entry_body_tokens(caller, tasks, profiles):
    body = list(tokens(caller))
    work = list(tokens("struct endpoint_qemu_work work;"))
    if body[:1 + len(work)] != ["{"] + work:
        raise AssertionError("target entry keeps one uninitialized original-field-only automatic work lifetime")
    body[1:1 + len(work)] = tokens(profiles + QEMU_WORK_DECLARATIONS)
    for task in (QEMU_FIXTURE_TASKS[0], *QEMU_SCENARIO_TASKS):
        body = replace_one_token_sequence(
            body, "if (!" + task + "(&work)) { goto done; }", qemu_phase_body_tokens(tasks[task]),
        )
    condition = (
        *qemu_failure_condition_tokens(tasks[QEMU_FIXTURE_TASKS[1]]),
        "||",
        *qemu_failure_condition_tokens(tasks[QEMU_FIXTURE_TASKS[2]]),
    )
    body = replace_one_token_sequence(
        body, "!endpoint_qemu_initialize_runtimes(&work) || !endpoint_qemu_reserve_clients(&work)", condition,
    )
    return native_work_field_tokens(body, {"fields": QEMU_WORK_FIELDS})


def original_body_digest(family, name, text):
    names = normal_names(family)
    body = tokens(text)
    if family == 0 and name in VALIDATOR_PHASES and (0, name) not in LONG_FUNCTION_MIGRATION:
        owner, specifications = VALIDATOR_PHASES[name]
        phases = [
            (specification, c_function_body(source(family_paths(0)[owner]), specification[0]))
            for specification in specifications
        ]
        body = validation_phase_body_tokens(name, text, phases)
    if family == 0 and name == "micros_endpoint_registry_initialize":
        owner = source(family_paths(0)[3])
        body = initialization_body_tokens(
            text, *(c_function_body(owner, helper) for helper in INITIALIZER_HELPERS),
        )
    if family == 1 and name == "main" and (1, "main") not in LONG_FUNCTION_MIGRATION:
        raw = native_runner_table(source(family_paths(1)[0]))
        body = ("{", *tokens(raw), *body[1:])
    if family == 1 and name in NATIVE_CASE_WORK and (1, name) not in LONG_FUNCTION_MIGRATION:
        specification = NATIVE_CASE_WORK[name]
        owner = source(family_paths(1)[specification["owner"]])
        macro = rejected_table_macro(owner, specification) if "macro_before" in specification else None
        body = native_case_body_tokens(
            text, specification, [c_function_body(owner, phase) for phase in specification["phases"]], macro,
        )
    if family == 2 and name == "validate_model" and (2, name) not in LONG_FUNCTION_MIGRATION:
        owner = source(family_paths(2)[3])
        body = model_projection_body_tokens(
            text, *(c_function_body(owner, helper) for helper in MODEL_PROJECTION_HELPERS),
        )
    if family == 2 and name == "micros_endpoint_model_test_run" and (2, name) not in LONG_FUNCTION_MIGRATION:
        owner = source(family_paths(2)[0])
        operations = source(family_paths(2)[4])
        body = model_runner_body_tokens(
            text,
            {helper: c_function_body(owner, helper) for helper in MODEL_RUN_HELPERS},
            {helper: c_function_body(operations, helper) for helper in MODEL_OPERATION_HELPERS.values()},
            {helper: c_function_body(operations, helper) for helper in MODEL_AUTHORIZATION_HELPERS},
            model_script_data(owner),
        )
    if family == 3 and (3, name) not in LONG_FUNCTION_MIGRATION:
        owners = (source(family_paths(3)[1]), source(family_paths(3)[2]))
        tasks = {
            task: c_function_body(owners[owner], task)
            for owner, names in enumerate((QEMU_FIXTURE_TASKS, QEMU_SCENARIO_TASKS)) for task in names
        }
        body = qemu_entry_body_tokens(text, tasks, qemu_profile_data(owners[0]))
    body = tuple(names.get(token, token) for token in body)
    if family == 1 and any(
        spelling == "endpoint_test_context" for _, spelling in owned_definitions(1)
    ):
        body = native_fixture_body_tokens(name, body)
    if family == 2 and any(
        spelling == "endpoint_model_context" for _, spelling in owned_definitions(2)
    ):
        body = model_fixture_body_tokens(name, body)
    return digest_bytes(json.dumps(body, separators=(",", ":")).encode())


def source_set_block(name, paths):
    return "set(\n    " + name + "\n" + "".join("    " + path + "\n" for path in paths) + ")"


def endpoint_cmake_projection(text):
    text = kernel_object_cmake_projection(text)
    blocks = [(name, paths, cmake_commands(text, "set", name)) for name, paths in SOURCE_SETS.items()]
    if not any(found for _, _, found in blocks):
        if any("${" + name + "}" in text for name in SOURCE_SETS):
            raise AssertionError("endpoint source variables require all four exact declarations")
        return text
    for family, (name, paths, found) in enumerate(blocks):
        expected = source_set_block(name, paths)
        if len(found) != 1 or found[0] != expected:
            raise AssertionError(f"wrong complete endpoint source set: {name}")
        insertion = expected + "\n\n"
        if text.count(insertion) != 1:
            raise AssertionError(f"wrong endpoint source-set insertion boundary: {name}")
        text = text.replace(insertion, "", 1)
        variable = "${" + name + "}"
        if text.count(variable) != (5, 1, 2, 1)[family]:
            raise AssertionError(f"wrong endpoint source-set consumer count: {name}")
        text = text.replace(variable, paths[0])
    return text


def restored_cmake(text):
    restored = endpoint_cmake_projection(text)
    if digest_bytes(restored.encode()) != BASE_CMAKE_DIGEST:
        raise AssertionError("recipe changed outside the exact four sets/nine source operands")
    return restored


class EndpointSourceLayoutTest(unittest.TestCase):
    def test_canonical_storage_has_its_reviewed_leaf_owner(self):
        names = {"storage_is_zero", "endpoint_storage_is_zero"}
        owners = [
            path for path in CORE_SOURCES
            if kernel_layout_source.is_file_path(ROOT / path)
            and names & set(c_function_definitions(kernel_layout_source.read_path(ROOT / path)))
        ]
        self.assertEqual(["kernel/endpoint_codec.c"], owners)

    def test_immutable_inventory_and_explicit_migrations_are_exact(self):
        self.assertEqual((67, 15, 15, 1), tuple(len(items) for items in BASELINE_BODIES.values()))
        self.assertEqual(90, len(INITIAL_SYMBOL_OWNER_MIGRATION))
        self.assertEqual(10, len(LONG_FUNCTIONS))
        self.assertEqual(31, len(CORE_PRIVATE_EXPORTS))
        for active, original in (
            (SYMBOL_OWNER_MIGRATION, INITIAL_SYMBOL_OWNER_MIGRATION),
            (LONG_FUNCTION_MIGRATION, LONG_FUNCTIONS),
        ):
            self.assertIsInstance(active, tuple)
            self.assertEqual(len(active), len(set(active)))
            self.assertLessEqual(set(active), set(original))
        for family, groups in ORIGINAL_FUNCTIONS.items():
            flattened = tuple(name for names in groups.values() for name in names)
            self.assertEqual(len(flattened), len(set(flattened)))
            self.assertEqual(set(BASELINE_BODIES[family]), set(flattened))

    def test_completed_migrations_are_literal_empty_and_evidence_namespaces_are_exact(self):
        tree = ast.parse(source(STRUCTURAL_TEST_PATH))
        for name in ("SYMBOL_OWNER_MIGRATION", "LONG_FUNCTION_MIGRATION"):
            assignments = [
                node.value for node in tree.body if isinstance(node, ast.Assign)
                and any(isinstance(target, ast.Name) and target.id == name for target in node.targets)
            ]
            self.assertEqual(1, len(assignments))
            self.assertIsInstance(assignments[0], ast.Tuple)
            self.assertEqual([], assignments[0].elts)
        entries = {"main", "micros_endpoint_model_test_run", "micros_endpoint_runtime_run_self_test"}
        for family, prefix in ((1, "endpoint_test_"), (2, "endpoint_model_"), (3, "endpoint_qemu_")):
            for _, name in owned_definitions(family):
                with self.subTest(family=family, name=name):
                    self.assertTrue(name in entries or name.startswith(prefix))

    def test_every_original_definition_has_its_exact_current_and_final_owner(self):
        for family, groups in ORIGINAL_FUNCTIONS.items():
            locations = original_locations(family)
            for owner, names in groups.items():
                for name in names:
                    with self.subTest(family=family, name=name):
                        current = 0 if (family, name) in SYMBOL_OWNER_MIGRATION else owner
                        self.assertEqual(family_paths(family)[current], locations[name][0])

    def test_complete_original_bodies_preserve_tokens_literals_predicates_effects_and_order(self):
        for family in ORIGINAL_FUNCTIONS:
            for name, (path, spelling) in original_locations(family).items():
                with self.subTest(family=family, name=name):
                    self.assertEqual(
                        BASELINE_BODIES[family][name],
                        original_body_digest(family, name, c_function_body(source(path), spelling)),
                        REVIEWED_BASE,
                    )

    def test_only_the_ten_explicit_long_migrations_remain(self):
        actual = {
            (family, normal_names(family).get(name, name))
            for family in ORIGINAL_FUNCTIONS
            for (_, name), lines in owned_definitions(family).items() if lines > 120
        }
        self.assertEqual(set(LONG_FUNCTION_MIGRATION), actual)

    def test_all_reviewed_sources_headers_and_real_consumers_are_present(self):
        self.assertEqual(25, sum(map(len, SOURCE_SETS.values())))
        self.assertEqual(25, len({path for paths in SOURCE_SETS.values() for path in paths}))
        cmake = source("CMakeLists.txt")
        for family, (name, paths) in enumerate(SOURCE_SETS.items()):
            with self.subTest(name=name):
                self.assertEqual((source_set_block(name, paths),), cmake_commands(cmake, "set", name))
                self.assertEqual((5, 1, 2, 1)[family], cmake.count("${" + name + "}"))
                for path in (*paths, PRIVATE_HEADERS[family]):
                    self.assertTrue(kernel_layout_source.is_file_path(ROOT / path), path)
        consumers = {
            "endpoint_test": (0, 1, 2), "ipc_model_test": (0, 2),
            "bootstrap_control_test": (0,), "pm_test": (0,), "micros": (0,),
        }
        for target, families in consumers.items():
            blocks = cmake_commands(cmake, "add_executable", target)
            self.assertEqual(1, len(blocks))
            for family, name in enumerate(SOURCE_SETS):
                self.assertEqual(int(family in families), blocks[0].count("${" + name + "}"))
        target = cmake_commands(cmake, "target_sources", "micros")
        self.assertEqual(
            1, sum(block.count("${MICROS_ENDPOINT_QEMU_TEST_SOURCES}") for block in target),
        )
        guard = re.findall(
            r"if\(MICROS_BUILD_ENDPOINT_TEST\)([\s\S]*?)\nendif\(\)", cmake,
        )
        self.assertEqual(1, sum("${MICROS_ENDPOINT_QEMU_TEST_SOURCES}" in block for block in guard))
        self.assertEqual(BASE_CMAKE_DIGEST, digest_bytes(restored_cmake(cmake).encode()))
        self.assertEqual(
            digest_bytes(cmake.encode()),
            kernel_layout_source.original_cmake_recipe_digest(
                validation_context.RECIPE_DIGESTS["CMakeLists.txt"]
            ),
        )

    def test_private_includes_preserve_the_existing_native_search_roots(self):
        for family, paths in enumerate(SOURCE_SETS.values()):
            for path in paths if family == 0 else paths[1:]:
                with self.subTest(path=path):
                    self.assertIn(
                        '#include "' + Path(PRIVATE_HEADERS[family]).name + '"', source(path),
                    )
                    self.assertNotIn('#include "' + PRIVATE_HEADERS[family] + '"', source(path))

    def test_current_and_projected_portable_dependencies_are_acyclic(self):
        locations = original_locations(0)
        names = normal_names(0)
        final = {
            name: family_paths(0)[owner] for owner, functions in ORIGINAL_FUNCTIONS[0].items()
            for name in functions
        }
        current_edges = set()
        final_edges = set()
        actual_exports = set()
        actual = {spelling: path for path, spelling in owned_definitions(0)}
        for spelling, path in actual.items():
            name = names.get(spelling, spelling)
            calls = set(re.findall(r"\b([A-Za-z_]\w*)\s*\(", c_lexical_text(c_function_body(source(path), spelling))))
            for call in calls:
                callee = names.get(call, call)
                if call not in actual:
                    continue
                dependency = actual[call]
                if dependency != path:
                    current_edges.add((dependency, path))
                    if not call.startswith("micros_"):
                        actual_exports.add(call)
                source_path = final[name] if name in final else path
                dependency_path = final[callee] if callee in final else dependency
                source_owner = family_paths(0).index(source_path)
                dependency_owner = family_paths(0).index(dependency_path)
                if source_owner != dependency_owner:
                    self.assertIn(dependency_owner, CORE_DAG[source_owner], (name, callee))
                    final_edges.add((dependency_path, source_path))
        paths = set(family_paths(0))
        self.assertEqual(paths, set(topological_order(paths, current_edges)))
        self.assertEqual(paths, set(topological_order(paths, final_edges)))
        header = source(PRIVATE_HEADERS[0])
        declarations = re.findall(r"\b(endpoint_\w+)\s*\([^;{}]*\)\s*;", c_lexical_text(header))
        self.assertEqual(len(declarations), len(set(declarations)))
        self.assertEqual(actual_exports, set(declarations))
        self.assertEqual({}, c_function_definitions(header))

    def test_public_runtime_driver_and_neighbor_artifacts_remain_byte_exact(self):
        for path, expected in STABLE_HASHES.items():
            with self.subTest(path=path):
                from tests.host.test_qemu_runner_source_layout import restored_runner_source

                value = (
                    restored_kernel_objects_source().encode() if path == "kernel/kernel_objects.c"
                    else restored_runner_source().encode() if path == "tools/run_qemu_smoke.py"
                    else kernel_layout_source.read_bytes_path(ROOT / path)
                )
                self.assertEqual(expected, digest_bytes(value))

    def test_native_fixture_has_exact_original_fields_resets_and_single_root_borrows(self):
        header = source(PRIVATE_HEADERS[1])
        fields = re.findall(r"struct endpoint_test_fixture\s*\{([^{}]*)\}", header)
        self.assertEqual(1, len(fields))
        self.assertEqual(tokens(
            "struct micros_endpoint_registry registry; struct micros_kernel_objects objects; "
            "struct micros_process_handle processes[2]; struct micros_thread_handle threads[2]; "
            "struct micros_hart_handle hart;"
        ), tokens(fields[0]))
        fixture = source(family_paths(1)[1])
        self.assertEqual(
            tokens("{ return &fixture_state; }"), tokens(c_function_body(fixture, "endpoint_test_context")),
        )
        self.assertEqual(1, fixture.count("static struct endpoint_test_fixture fixture_state;"))
        completed_phases = sum(
            len(specification["phases"]) for name, specification in NATIVE_CASE_WORK.items()
            if (1, name) not in LONG_FUNCTION_MIGRATION
        )
        self.assertEqual(16 + completed_phases, len(owned_definitions(1)))
        for path in family_paths(1):
            expected = ("static struct endpoint_test_fixture fixture_state;",) if path == family_paths(1)[1] else ()
            if path == family_paths(1)[0] and (1, "main") not in LONG_FUNCTION_MIGRATION:
                expected = file_scope_objects(native_runner_table(source(path)))
            self.assertEqual(tuple(map(tokens, expected)), tuple(map(tokens, file_scope_objects(source(path)))))
            self.assertNotRegex(source(path), r'#include\s*"[^"]*endpoint_private\.h"')
        macro = header[header.index("#define EXPECT_TRUE"):header.index("struct endpoint_test_fixture {")]
        self.assertEqual(NATIVE_ASSERTION_DIGEST, body_digest(macro))
        for name, (path, spelling) in original_locations(1).items():
            text = source(path)
            if name in NATIVE_CONTEXT_CASES:
                self.assertEqual(
                    1, c_function_body(text, spelling).count("endpoint_test_context()"), name,
                )
            elif name != "main":
                self.assertNotIn("endpoint_test_context(", c_function_body(text, spelling), name)
        setup = c_function_body(fixture, native_name("setup_lifecycle_fixture"))
        self.assertNotIn("sizeof(*fixture)", setup)
        self.assertNotRegex(setup, r"\bmemset\s*\(\s*fixture\s*,")
        self.assertEqual({}, c_function_definitions(header))
        self.assertNotIn('"kernel/endpoint_private.h"', header)

    def test_native_transport_distinguishes_global_roots_from_structure_members_and_scope(self):
        name = NATIVE_CONTEXT_CASES[0]
        current = (
            "{ struct endpoint_test_fixture *fixture = endpoint_test_context(); "
            "snapshot = fixture->objects.processes[index]; "
            "if (!setup_lifecycle_fixture(fixture)) { return false; } "
            "copy(&fixture->registry, sizeof(fixture->registry)); return true; }"
        )
        expected = (
            "{ snapshot = objects.processes[index]; if (!setup_lifecycle_fixture()) { return false; } "
            "copy(&registry, sizeof(registry)); return true; }"
        )
        self.assertEqual(tokens(expected), native_fixture_body_tokens(name, tokens(current)))
        for changed in (
            current.replace("fixture->objects", "fixture->unknown"),
            current.replace("fixture->objects.processes", "fixture->objects.fixture->processes"),
            current.replace("return true;", "memset(fixture, 0, sizeof(*fixture)); return true;"),
            current.replace("struct endpoint_test_fixture *fixture", "static struct endpoint_test_fixture *fixture"),
            current.replace("setup_lifecycle_fixture(fixture)", "setup_lifecycle_fixture(other)"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                native_fixture_body_tokens(name, tokens(changed))
        changed = current.replace("return false;", "return true;")
        self.assertNotEqual(tokens(expected), native_fixture_body_tokens(name, tokens(changed)))

    def test_native_runner_exposes_one_raw_const_table_and_short_ordered_control(self):
        text = source(family_paths(1)[0])
        self.assertLessEqual(c_function_definitions(text)["main"], 120)
        raw = native_runner_table(text)
        self.assertEqual(25, len(re.findall(r'\{\s*"[^"\n]+"', raw)))
        for changed in (
            text.replace('"endpoint encoding boundaries"', '"changed encoding"'),
            text.replace("endpoint_test_encoding_boundaries,", "endpoint_test_resolution_rejects_unimplemented_slots,", 1),
            text.replace("static const struct", "static struct", 1),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                native_runner_table(changed)

    def test_native_case_work_exposes_exact_automatic_objects_and_named_tasks(self):
        for original, specification in NATIVE_CASE_WORK.items():
            text = source(family_paths(1)[specification["owner"]])
            definitions = c_function_definitions(text)
            fields = re.findall(r"struct " + specification["tag"] + r"\s*\{([^{}]*)\}", text)
            self.assertEqual(1, len(fields))
            self.assertEqual(tokens(specification["declarations"]), tokens(fields[0]))
            self.assertLessEqual(definitions[native_name(original)], 120)
            for phase in specification["phases"]:
                self.assertIn(phase, definitions)
                self.assertLessEqual(definitions[phase], 120)
                signatures = re.findall(rf"(?m)^static bool {phase}\([^;{{}}]*\)\s*\{{", text)
                self.assertEqual(1, len(signatures))
                self.assertEqual(tokens(
                    "static bool " + phase + "("
                    + (
                        "struct " + specification["tag"] + " *work" if phase in specification.get("work_only", ())
                        else "struct endpoint_test_fixture *fixture"
                        + ("" if phase in specification.get("fixture_only", ()) else ", struct " + specification["tag"] + " *work")
                    )
                    + ") {"
                ), tokens(signatures[0]))
            if "macro_before" in specification:
                rejected_table_macro(text, specification)
                undef = text.index("#undef EXPECT_REJECTED_TABLE")
                self.assertTrue(all(text.index("static bool " + phase + "(") < undef for phase in specification["phases"]))
                self.assertLess(undef, text.index("bool " + native_name(original) + "("))

    def test_native_work_projection_retains_initializer_lifetime_and_macro_failure_order(self):
        specification = NATIVE_CASE_WORK["test_profile_install_and_close_require_held_threads"]
        borrow = "struct endpoint_test_fixture *fixture = endpoint_test_context();"
        caller = "{ " + borrow + specification["outlined"] + "return " + " && ".join(
            phase + "(fixture, &work)" for phase in specification["phases"]
        ) + "; }"
        bodies = (
            "{ reserve(&work->endpoint); attach(&work->context); return true; }",
            "{ if (bad()) { return false; } publish(work->endpoint); return true; }",
            "{ close_and_cleanup(work->endpoint); return true; }",
        )
        expected = (
            "{ " + borrow + specification["original"]
            + "reserve(&endpoint); attach(&context); if (bad()) { return false; } "
            "publish(endpoint); close_and_cleanup(endpoint); return true; }"
        )
        self.assertEqual(tokens(expected), native_case_body_tokens(caller, specification, bodies))
        for changed in (
            caller.replace("struct endpoint_held_case work", "static struct endpoint_held_case work"),
            caller.replace("endpoint_test_held_profile_publication(fixture, &work)", "endpoint_test_held_profile_publication(fixture, &copy)"),
            caller.replace("&& endpoint_test_held_close", "|| endpoint_test_held_close"),
            caller.replace("return endpoint_test_held_installation", "memset(&work, 0, sizeof(work)); return endpoint_test_held_installation"),
        ):
            self.assertNotEqual(caller, changed)
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                native_case_body_tokens(changed, specification, bodies)
        for changed in (
            bodies[1].replace("return false;", "return true;"),
            bodies[1].replace("publish(work->endpoint);", "save(work);"),
            bodies[1].replace("work->endpoint", "work->unknown"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                native_case_body_tokens(caller, specification, (bodies[0], changed, bodies[2]))
        self.assertNotEqual(
            tokens(expected),
            native_case_body_tokens(caller, specification, (bodies[0], bodies[1].replace("publish", "changed_publish"), bodies[2])),
        )

    def test_rejection_macro_keeps_raw_reset_snapshot_action_and_error_order(self):
        specification = NATIVE_CASE_WORK["test_invalid_profile_tables_preserve_storage"]
        raw = (
            "#define EXPECT_REJECTED_TABLE(expected, statement) \\\n"
            "    do { \\\n"
            "        memset(&fixture->registry, 0, sizeof(fixture->registry)); \\\n"
            "        work->before = fixture->registry; \\\n"
            "        statement; \\\n"
            "        EXPECT_ERROR( \\\n"
            "            (expected), \\\n"
            "            micros_endpoint_registry_initialize( \\\n"
            "                &fixture->registry, \\\n"
            "                work->profiles, \\\n"
            "                2 \\\n"
            "            ) \\\n"
            "        ); \\\n"
            "        EXPECT_TRUE(memcmp(&fixture->registry, &work->before, sizeof(fixture->registry)) == 0); \\\n"
            "    } while (false)"
        )
        text = raw + "\n#undef EXPECT_REJECTED_TABLE\n"
        self.assertEqual(tokens(raw), tokens(rejected_table_macro(text, specification)))
        for changed in (
            text.replace("work->before = fixture->registry; \\\n        statement;", "statement; \\\n        work->before = fixture->registry;"),
            text.replace("work->before", "work->unknown"),
            text.replace("#undef EXPECT_REJECTED_TABLE", ""),
            "bool wrong_scope(void) {\n" + text + "}\n",
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                rejected_table_macro(changed, specification)

    def test_raw_runner_extraction_preserves_literals_callback_types_and_scope(self):
        raw = (
            'static const struct { const char *name; bool (*run)(void); } tests[] = {'
            '{"first { label", case_a}, {"second", case_b}, };'
        )
        text = raw + "\nint main(void) { run(tests); }"
        self.assertEqual(tokens(raw), tokens(raw_native_runner_table(text)))
        for changed in (
            text.replace("static const", "static"),
            text.replace("bool (*run)(void)", "int (*run)(void)"),
            text.replace("const char *name", "char *name"),
            "int main(void) {\n" + raw + "\n}",
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                raw_native_runner_table(changed)

    def test_model_fixture_preserves_types_separate_storage_trace_data_and_exact_resets(self):
        header = source(PRIVATE_HEADERS[2])
        fields = re.findall(r"struct endpoint_model_fixture\s*\{([^{}]*)\}", header)
        self.assertEqual(1, len(fields))
        self.assertEqual(tokens(
            "struct micros_endpoint_registry registry; struct micros_kernel_objects objects; "
            "struct micros_hart_handle hart; struct model_trace_entry trace[MODEL_TRACE_COUNT]; size_t trace_count;"
        ), tokens(fields[0]))
        type_block = header[header.index("enum {"):header.index("struct endpoint_model_fixture {")]
        self.assertEqual(MODEL_TYPES_DIGEST, body_digest(type_block))
        fixture = source(family_paths(2)[1])
        self.assertEqual(
            tokens("{ return &model_fixture_state; }"),
            tokens(c_function_body(fixture, "endpoint_model_context")),
        )
        start = fixture.index("static const char *const operation_names[")
        raw_names = fixture[start:fixture.index("};", start) + 2]
        self.assertEqual(MODEL_OPERATION_NAMES_DIGEST, body_digest(raw_names))
        for path in family_paths(2):
            objects = list(file_scope_objects(source(path)))
            if path == family_paths(2)[1]:
                prefix = tokens("static const char *const operation_names[")
                names = [declaration for declaration in objects if tokens(declaration)[:len(prefix)] == prefix]
                self.assertEqual(1, len(names))
                objects.remove(names[0])
            if path == family_paths(2)[0] and (2, "micros_endpoint_model_test_run") not in LONG_FUNCTION_MIGRATION:
                self.assertEqual(
                    tuple(map(tokens, file_scope_objects(model_script_data(source(path))))),
                    tuple(map(tokens, objects)),
                )
                objects = []
            expected = ("static struct endpoint_model_fixture model_fixture_state;",) if path == family_paths(2)[1] else ()
            self.assertEqual(tuple(map(tokens, expected)), tuple(map(tokens, objects)))
            self.assertNotRegex(source(path), r'#include\s*"[^"]*endpoint_private\.h"')
        completed_projection = (2, "validate_model") not in LONG_FUNCTION_MIGRATION
        completed_runner = (2, "micros_endpoint_model_test_run") not in LONG_FUNCTION_MIGRATION
        self.assertEqual(
            16 + len(MODEL_PROJECTION_HELPERS) * completed_projection + len(MODEL_TASK_SIGNATURES) * completed_runner,
            len(owned_definitions(2)),
        )
        for name, (path, spelling) in original_locations(2).items():
            body = c_function_body(source(path), spelling)
            if name == "micros_endpoint_model_test_run":
                self.assertEqual(1, body.count("endpoint_model_context()"))
                if completed_runner:
                    self.assertIn("struct endpoint_model_run run;", body)
                    self.assertIn("const uint32_t seed = UINT32_C(0xe029cafe);", body)
                else:
                    self.assertIn("struct model_slot model[MODEL_SLOT_COUNT];", body)
                    self.assertIn("struct model_coverage coverage;", body)
            else:
                self.assertNotIn("endpoint_model_context(", body)
        setup = c_function_body(fixture, "endpoint_model_setup_fixture")
        self.assertNotRegex(setup, r"\bmemset\s*\(\s*fixture\s*,")
        self.assertNotIn("sizeof(*fixture)", setup)
        self.assertNotIn("memset(&fixture->hart", setup)
        self.assertEqual({}, c_function_definitions(header))

    def test_model_runner_has_only_original_typed_work_and_all_ordered_task_owners(self):
        header = source(PRIVATE_HEADERS[2])
        expected = {
            "endpoint_model_run": (
                "struct model_slot model[MODEL_SLOT_COUNT]; struct model_coverage coverage; "
                "uint32_t random_state; size_t step;"
            ),
            "endpoint_model_iteration": (
                "uint32_t random; enum model_operation operation; size_t slot; uint32_t value;"
            ),
        }
        for tag, declarations in expected.items():
            fields = re.findall(r"struct " + tag + r"\s*\{([^{}]*)\}", header)
            self.assertEqual(1, len(fields), tag)
            self.assertEqual(tokens(declarations), tokens(fields[0]))
        owners = {name: path for path, name in owned_definitions(2)}
        for owner, names in (
            (0, MODEL_RUN_HELPERS),
            (4, (*MODEL_OPERATION_HELPERS.values(), *MODEL_AUTHORIZATION_HELPERS)),
        ):
            for name in names:
                self.assertEqual(family_paths(2)[owner], owners.get(name), name)
                self.assertLessEqual(owned_definitions(2)[owners[name], name], 120)
                text = source(owners[name])
                storage, parameters = MODEL_TASK_SIGNATURES[name]
                signatures = re.findall(rf"(?m)^{storage} {name}\([^;{{}}]*\)\s*\{{", text)
                self.assertEqual(1, len(signatures), name)
                self.assertEqual(tokens(f"{storage} {name}({parameters}) {{"), tokens(signatures[0]))
        entry = c_function_body(source(family_paths(2)[0]), "micros_endpoint_model_test_run")
        self.assertEqual(1, entry.count("struct endpoint_model_run run;"))
        self.assertEqual(1, entry.count("struct endpoint_model_iteration iteration;"))
        self.assertNotIn("memset(&run", entry)
        self.assertNotIn("memset(&iteration", entry)

    def test_model_work_transport_preserves_original_extents_members_and_scope(self):
        current = (
            "memset(run->model, 0, sizeof(run->model)); memset(&run->coverage, 0, sizeof(run->coverage)); "
            "iteration->random = next_random(&run->random_state); run->model[iteration->slot].generation = generation; "
            "++run->coverage.authorize_state; record(run->step, iteration->operation, iteration->slot, iteration->value);"
        )
        expected = (
            "memset(model, 0, sizeof(model)); memset(&coverage, 0, sizeof(coverage)); "
            "random = next_random(&random_state); model[slot].generation = generation; "
            "++coverage.authorize_state; record(step, operation, slot, value);"
        )
        self.assertEqual(tokens(expected), model_work_body_tokens(tokens(current)))
        for changed in (
            current.replace("run->model", "run->other"),
            current.replace("iteration->slot", "iteration->unknown"),
            current.replace("run->model[iteration->slot].generation", "run->model[iteration->slot].run->model"),
            current + "save(run);",
            current + "memset(&iteration, 0, sizeof(iteration));",
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                model_work_body_tokens(tokens(changed))

    def test_model_draw_projection_keeps_two_draws_before_script_overrides(self):
        current = (
            "{ iteration->random = next_random(&run->random_state); "
            "iteration->operation = (enum model_operation)((iteration->random >> 24) % COUNT); "
            "iteration->slot = SLOTS; iteration->value = next_random(&run->random_state); "
            "if (run->step < sizeof(scripted_operations) / sizeof(scripted_operations[0])) { "
            "iteration->operation = scripted_operations[run->step]; iteration->value = scripted_values[run->step]; } }"
        )
        expected = (
            "uint32_t random = next_random(&random_state); "
            "enum model_operation operation = (enum model_operation)((random >> 24) % COUNT); "
            "size_t slot = SLOTS; uint32_t value = next_random(&random_state); "
            "if (step < sizeof(scripted_operations) / sizeof(scripted_operations[0])) { "
            "operation = scripted_operations[step]; value = scripted_values[step]; }"
        )
        self.assertEqual(tokens(expected), model_work_body_tokens(model_draw_body_tokens(current)))
        for changed in (
            current.replace("iteration->random =", "iteration->value =", 1),
            current.replace("iteration->value = next_random", "iteration->random = next_random", 1),
            current.replace("} }", "} return; }"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                model_draw_body_tokens(changed)
        self.assertNotEqual(
            tokens(expected),
            model_work_body_tokens(model_draw_body_tokens(current.replace(
                "iteration->value = next_random(&run->random_state);", "iteration->value = 0;",
            ))),
        )

    def test_model_script_data_uses_actual_raw_types_values_order_and_file_scope(self):
        text = source(family_paths(2)[0])
        raw = model_script_data(text)
        self.assertEqual(MODEL_SCRIPT_DATA_DIGEST, body_digest(raw))
        for changed in (
            text.replace("MODEL_OPERATION_CREATE,", "MODEL_OPERATION_CLOSE,", 1),
            text.replace("(UINT32_C(1) << 16) | 8", "(UINT32_C(1) << 16) | 4", 1),
            text.replace("static const uint32_t scripted_values", "static uint32_t scripted_values", 1),
            "void local(void) {\n" + text + "\n}",
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                model_script_data(changed)

    def test_model_operation_success_replaces_case_break_not_trace_or_outer_loop(self):
        body = (
            "{ for (iteration->slot = 0; iteration->slot < SLOTS; ++iteration->slot) { "
            "if (available()) { break; } } "
            "if (failed()) { return model_fail(fixture, run->step, iteration->operation, iteration->value, \"failed\"); } "
            "++run->coverage.create; return true; }"
        )
        expected = (
            "for (iteration->slot = 0; iteration->slot < SLOTS; ++iteration->slot) { "
            "if (available()) { break; } } "
            "if (failed()) { return model_fail(fixture, run->step, iteration->operation, iteration->value, \"failed\"); } "
            "++run->coverage.create; break;"
        )
        self.assertEqual(tokens(expected), model_operation_body_tokens("MODEL_OPERATION_CREATE", body, {}))
        for changed in (
            body.replace("return model_fail", "return true; return model_fail"),
            body.replace("return true;", "return false;"),
            body.replace("++run->coverage.create;", "goto caller_done;"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                model_operation_body_tokens("MODEL_OPERATION_CREATE", changed, {})
        self.assertEqual(
            tokens("authorization_operation = REPLY; expected = ERROR; error = authorize();"),
            tuple(scalar_output_tokens(tokens("*authorization_operation = REPLY; *expected = ERROR; *error = authorize();"),
                                       ("authorization_operation", "expected", "error"))),
        )
        with self.assertRaises(AssertionError):
            scalar_output_tokens(tokens("error = authorize();"), ("error",))

    def test_model_authorization_preserves_case_locals_extra_draws_priority_and_noop_exits(self):
        receive, reply, select, selected = MODEL_AUTHORIZATION_HELPERS
        phases = {
            receive: (
                "{ *authorization_operation = REPLY_RECEIVE; *expected = wanted(); "
                "if (iteration->slot == SLOTS) { return true; } *error = authorize(); "
                "iteration->value = *authorization_operation; "
                "if (*error != *expected) { return model_fail(); } ++run->coverage.reply_receive_allowed; return true; }"
            ),
            reply: (
                "{ *authorization_operation = REPLY; *expected = wanted(); "
                "if (iteration->slot == SLOTS) { return true; } *error = authorize(); "
                "iteration->value = *authorization_operation; "
                "if (*error != *expected) { return model_fail(); } ++run->coverage.reply_allowed; return true; }"
            ),
            select: "{ *target = SLOTS; if (active()) { *target = choose(target_random); *authorization_operation = CALL; } }",
            selected: (
                "{ *expected = wanted_target(); *error = authorize_target(); iteration->value = authorization_operation; "
                "if (*error != *expected) { return model_fail(); } ++run->coverage.authorize_allowed; return true; }"
            ),
        }
        caller = (
            "{ size_t target; uint32_t target_random = next_random(&run->random_state); "
            "uint32_t authorization_operation = operations[(next_random(&run->random_state) >> 16) % 3]; "
            "enum micros_endpoint_error expected; enum micros_endpoint_error error; "
            "iteration->slot = find_slot(run->model, iteration->value); "
            "if ((iteration->value & 8) != 0) { return " + receive
            + "(fixture, run, iteration, &authorization_operation, &expected, &error); } "
            "if ((iteration->value & 4) != 0) { return " + reply
            + "(fixture, run, iteration, &authorization_operation, &expected, &error); } "
            + select + "(run, iteration, target_random, &target, &authorization_operation); "
            "if (iteration->slot == SLOTS || target == SLOTS) { return true; } "
            "if (!" + selected + "(fixture, run, iteration, target, authorization_operation, &expected, &error)) "
            "{ return false; } return true; }"
        )
        expected = (
            "{ size_t target; uint32_t target_random = next_random(&run->random_state); "
            "uint32_t authorization_operation = operations[(next_random(&run->random_state) >> 16) % 3]; "
            "enum micros_endpoint_error expected; enum micros_endpoint_error error; "
            "iteration->slot = find_slot(run->model, iteration->value); "
            "if ((iteration->value & 8) != 0) { authorization_operation = REPLY_RECEIVE; expected = wanted(); "
            "if (iteration->slot == SLOTS) { break; } error = authorize(); iteration->value = authorization_operation; "
            "if (error != expected) { return model_fail(); } ++run->coverage.reply_receive_allowed; break; } "
            "if ((iteration->value & 4) != 0) { authorization_operation = REPLY; expected = wanted(); "
            "if (iteration->slot == SLOTS) { break; } error = authorize(); iteration->value = authorization_operation; "
            "if (error != expected) { return model_fail(); } ++run->coverage.reply_allowed; break; } "
            "target = SLOTS; if (active()) { target = choose(target_random); authorization_operation = CALL; } "
            "if (iteration->slot == SLOTS || target == SLOTS) { break; } "
            "expected = wanted_target(); error = authorize_target(); iteration->value = authorization_operation; "
            "if (error != expected) { return model_fail(); } ++run->coverage.authorize_allowed; break; }"
        )
        self.assertEqual(tokens(expected), model_operation_body_tokens("MODEL_OPERATION_AUTHORIZE", caller, phases))
        for changed in (
            caller.replace("&expected, &error", "&other, &error", 1),
            caller.replace("return " + receive, "return " + reply, 1),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                model_operation_body_tokens("MODEL_OPERATION_AUTHORIZE", changed, phases)
        for helper, changed in (
            (select, phases[select].replace("*target = SLOTS;", "return true; *target = SLOTS;")),
            (selected, phases[selected].replace("return model_fail();", "return true;")),
        ):
            with self.subTest(helper=helper), self.assertRaises(AssertionError):
                model_operation_body_tokens("MODEL_OPERATION_AUTHORIZE", caller, {**phases, helper: changed})
        for changed in (
            caller.replace("iteration->value & 8", "iteration->value & 4", 1),
            caller.replace("uint32_t target_random = next_random(&run->random_state);", "uint32_t target_random = 0;"),
            caller.replace("if (iteration->slot == SLOTS || target == SLOTS) { return true; }",
                           "if (iteration->slot == SLOTS || target == SLOTS) { return false; }"),
        ):
            self.assertNotEqual(tokens(expected), model_operation_body_tokens("MODEL_OPERATION_AUTHORIZE", changed, phases))

    def test_model_runner_projection_cannot_skip_record_comparison_or_change_failure_scope(self):
        root = source(family_paths(2)[0])
        operation_source = source(family_paths(2)[4])
        caller = c_function_body(root, "micros_endpoint_model_test_run")
        phases = {name: c_function_body(root, name) for name in MODEL_RUN_HELPERS}
        operations = {name: c_function_body(operation_source, name) for name in MODEL_OPERATION_HELPERS.values()}
        authorization = {name: c_function_body(operation_source, name) for name in MODEL_AUTHORIZATION_HELPERS}
        scripts = model_script_data(root)
        aliases = normal_names(2)

        def digest(current, selected):
            expanded = model_runner_body_tokens(current, selected, operations, authorization, scripts)
            expanded = tuple(aliases.get(token, token) for token in expanded)
            expanded = model_fixture_body_tokens("micros_endpoint_model_test_run", expanded)
            return digest_bytes(json.dumps(expanded, separators=(",", ":")).encode())

        expected = BASELINE_BODIES[2]["micros_endpoint_model_test_run"]
        self.assertEqual(expected, digest(caller, phases))
        for changed in (
            caller.replace("return false;", "return true;", 1),
            caller.replace("struct endpoint_model_iteration iteration;", "static struct endpoint_model_iteration iteration;"),
            caller.replace("run.random_state = seed;", "run.random_state = 0;"),
            caller.replace("fixture, &run, &iteration", "fixture, &other, &iteration"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                digest(changed, phases)
        for changed in (
            caller.replace("++run.step", "run.step += 2"),
            caller.replace("run.step, iteration.operation, iteration.slot", "run.step, iteration.operation, 0", 1),
            caller.replace("fixture, run.model, run.step, iteration.operation", "fixture, run.model, 0, iteration.operation"),
        ):
            with self.subTest(changed=changed):
                self.assertNotEqual(expected, digest(changed, phases))
        setup = phases["endpoint_model_setup_run"]
        with self.assertRaises(AssertionError):
            digest(caller, {**phases, "endpoint_model_setup_run": setup.replace(
                "memset(run->model, 0, sizeof(run->model));", "memset(run, 0, sizeof(*run));",
            )})

    def test_model_transport_retains_caller_bindings_trace_and_reference_lifetime(self):
        current = (
            "{ struct endpoint_model_fixture *fixture = endpoint_model_context(); "
            "struct model_slot model[SLOTS]; memset(model, 0, sizeof(model)); "
            "if (!setup_fixture(fixture)) { return false; } "
            "record_transition(fixture, step, operation, slot, value); "
            "if (!validate_model(fixture, model, step, operation, value)) { return false; } "
            "if (bad()) { return model_fail(fixture, step, operation, value, \"failed\"); } "
            "check(fixture->objects.threads[index], fixture->trace_count); return true; }"
        )
        expected = (
            "{ struct model_slot model[SLOTS]; memset(model, 0, sizeof(model)); "
            "if (!setup_fixture()) { return false; } record_transition(step, operation, slot, value); "
            "if (!validate_model(model, step, operation, value)) { return false; } "
            "if (bad()) { return model_fail(step, operation, value, \"failed\"); } "
            "check(objects.threads[index], trace_count); return true; }"
        )
        self.assertEqual(
            tokens(expected), model_fixture_body_tokens("micros_endpoint_model_test_run", tokens(current)),
        )
        for changed in (
            current.replace("record_transition(fixture,", "record_transition(other,"),
            current.replace("setup_fixture(fixture)", "setup_fixture(fixture, model)"),
            current.replace("fixture->trace_count", "fixture->unknown"),
            current.replace("fixture->objects.threads", "fixture->objects.fixture->trace"),
            current.replace("return true;", "save(fixture); return true;"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                model_fixture_body_tokens("micros_endpoint_model_test_run", tokens(changed))
        self.assertNotEqual(
            tokens(expected),
            model_fixture_body_tokens("micros_endpoint_model_test_run", tokens(current.replace("return false;", "return true;", 1))),
        )

    def test_model_projection_exposes_live_identity_and_endpoint_phase_tasks(self):
        text = source(family_paths(2)[3])
        definitions = c_function_definitions(text)
        base = (
            "const struct endpoint_model_fixture *fixture, const struct model_slot *expected, "
            "const struct micros_process *process, "
        )
        tail = "size_t step, enum model_operation operation, uint32_t value"
        expected = {
            MODEL_PROJECTION_HELPERS[0]: base + tail,
            MODEL_PROJECTION_HELPERS[1]: base + "const struct micros_endpoint_record *endpoint, " + tail,
        }
        for name, parameters in expected.items():
            with self.subTest(name=name):
                self.assertIn(name, definitions)
                self.assertLessEqual(definitions[name], 120)
                signatures = re.findall(rf"(?m)^static bool {name}\([^;{{}}]*\)\s*\{{", text)
                self.assertEqual(1, len(signatures))
                self.assertEqual(tokens(f"static bool {name}({parameters}) {{"), tokens(signatures[0]))
        self.assertLessEqual(definitions["endpoint_model_validate_model"], 120)

    def test_model_projection_expands_only_the_two_original_slot_continues(self):
        live_call = "if (!endpoint_model_live_process_and_thread_match(fixture, expected, process, step, operation, value)) { return false; }"
        endpoint_call = "if (!endpoint_model_phase_matches(fixture, expected, process, endpoint, step, operation, value)) { return false; }"
        caller = (
            "{ validate_header(); for (index = 0; index < SLOTS; ++index) { bind_original_objects(); "
            "++expected_processes; ++expected_threads; " + live_call + endpoint_call
            + " } compare_aggregate_counts(); return true; }"
        )
        live = "{ if (bad_process()) { return model_fail(); } if (bad_thread()) { return model_fail(); } return true; }"
        endpoint = (
            "{ if (expected->endpoint_phase == MODEL_ENDPOINT_UNUSED) { if (bad_unused()) { return model_fail(); } return true; } "
            "if (expected->endpoint_phase == MODEL_ENDPOINT_CLOSED) { if (bad_closed()) { return model_fail(); } return true; } "
            "if (bad_binding()) { return model_fail(); } return true; }"
        )
        expected = (
            "{ validate_header(); for (index = 0; index < SLOTS; ++index) { bind_original_objects(); "
            "++expected_processes; ++expected_threads; "
            "if (bad_process()) { return model_fail(); } if (bad_thread()) { return model_fail(); } "
            "if (expected->endpoint_phase == MODEL_ENDPOINT_UNUSED) { if (bad_unused()) { return model_fail(); } continue; } "
            "if (expected->endpoint_phase == MODEL_ENDPOINT_CLOSED) { if (bad_closed()) { return model_fail(); } continue; } "
            "if (bad_binding()) { return model_fail(); } } compare_aggregate_counts(); return true; }"
        )
        self.assertEqual(tokens(expected), model_projection_body_tokens(caller, live, endpoint))
        for changed in (
            caller.replace("expected, process, step", "expected, other, step"),
            caller.replace("return false;", "return true;", 1),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                model_projection_body_tokens(changed, live, endpoint)
        for changed in (
            live.replace("return model_fail();", "return true;", 1),
            live.replace("return model_fail();", "continue;", 1),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                model_projection_body_tokens(caller, changed, endpoint)
        with self.assertRaises(AssertionError):
            model_projection_body_tokens(caller, live, endpoint.replace("MODEL_ENDPOINT_UNUSED", "MODEL_ENDPOINT_ACTIVE"))
        self.assertNotEqual(
            tokens(expected), model_projection_body_tokens(caller, live, endpoint.replace("bad_binding", "weakened_binding")),
        )

    def test_model_private_edges_keep_reference_independent_and_include_address_taken_predicates(self):
        paths = family_paths(2)
        definitions = owned_definitions(2)
        owners = {name: path for path, name in definitions}
        self.assertEqual(len(definitions), len(owners))
        allowed = {0: {1, 2, 3, 4}, 1: set(), 2: set(), 3: {1, 2}, 4: {1, 2}}
        edges = set()
        exports = set()
        for name, path in owners.items():
            referenced = set(re.findall(r"[A-Za-z_]\w*", c_lexical_text(c_function_body(source(path), name))))
            for callee in referenced & set(owners):
                dependency = owners[callee]
                if dependency == path:
                    continue
                self.assertIn(paths.index(dependency), allowed[paths.index(path)], (name, callee))
                edges.add((dependency, path))
                exports.add(callee)
        self.assertEqual(set(paths), set(topological_order(set(paths), edges)))
        declarations = re.findall(
            r"\b(endpoint_model_\w*)\s*\([^;{}]*\)\s*;", c_lexical_text(source(PRIVATE_HEADERS[2])),
        )
        self.assertEqual(len(declarations), len(set(declarations)))
        self.assertEqual(exports, set(declarations))

    def test_qemu_tasks_keep_exact_original_automatic_work_and_irq_entry_lifetime(self):
        header = source(PRIVATE_HEADERS[3])
        fields = re.findall(r"struct endpoint_qemu_work\s*\{([^{}]*)\}", header)
        self.assertEqual(1, len(fields))
        self.assertEqual(tokens(QEMU_WORK_DECLARATIONS), tokens(fields[0]))
        owners = {name: path for path, name in owned_definitions(3)}
        for owner, names in ((1, QEMU_FIXTURE_TASKS), (2, QEMU_SCENARIO_TASKS)):
            for name in names:
                self.assertEqual(family_paths(3)[owner], owners.get(name), name)
                self.assertLessEqual(owned_definitions(3)[owners[name], name], 120)
                text = source(owners[name])
                signatures = re.findall(rf"(?m)^bool {name}\([^;{{}}]*\)\s*\{{", text)
                self.assertEqual(1, len(signatures), name)
                self.assertEqual(tokens(f"bool {name}(struct endpoint_qemu_work *work) {{"), tokens(signatures[0]))
                body = c_function_body(text, name)
                self.assertNotIn("riscv_irq_", body)
                self.assertNotIn("saved_status", body)
                self.assertNotIn("passed", body)
        entry = c_function_body(source(family_paths(3)[0]), "micros_endpoint_runtime_run_self_test")
        self.assertEqual(1, entry.count("struct endpoint_qemu_work work;"))
        self.assertEqual(1, entry.count("riscv_irq_save()"))
        self.assertEqual(1, entry.count("riscv_irq_restore(saved_status)"))
        self.assertEqual(1, len(re.findall(r"\bdone\s*:", entry)))
        self.assertIn("bool passed = false;", entry)
        self.assertNotIn("memset(", entry)
        self.assertEqual(9, len(owners))
        declarations = re.findall(r"\b(endpoint_qemu_\w+)\s*\([^;{}]*\)\s*;", c_lexical_text(header))
        self.assertEqual(set((*QEMU_FIXTURE_TASKS, *QEMU_SCENARIO_TASKS)), set(declarations))
        self.assertEqual(len(declarations), len(set(declarations)))
        self.assertEqual({}, c_function_definitions(header))

    def test_qemu_split_runtime_predicates_keep_short_circuit_actions_and_failure_scope(self):
        current = (
            "{ if (cold_probe() != NOT_INITIALIZED || "
            "(work->registry = acquire()) == NULL || initialize() != OK) { return false; } return true; }"
        )
        expected = (
            "cold_probe() != NOT_INITIALIZED || (work->registry = acquire()) == NULL || initialize() != OK"
        )
        self.assertEqual(tokens(expected), tuple(qemu_failure_condition_tokens(current)))
        for changed in (
            current.replace("{ if", "{ reset_runtime(); if", 1),
            current.replace("return false;", "return true;", 1),
            current.replace("return false;", "goto done;", 1),
            current.replace("return false;", "restore_interrupts(); return false;", 1),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                qemu_failure_condition_tokens(changed)
        changed = current.replace("initialize() != OK", "initialize() == OK")
        self.assertNotEqual(tokens(expected), tuple(qemu_failure_condition_tokens(changed)))

    def test_qemu_projection_keeps_one_irq_scope_first_failure_and_success_only_cleanup(self):
        tasks = {
            QEMU_FIXTURE_TASKS[0]: (
                "{ work->objects = acquire_objects(); if (work->objects == NULL) { return false; } "
                "work->baseline_processes = work->objects->live_process_count; return true; }"
            ),
            QEMU_FIXTURE_TASKS[1]: "{ if (cold() != NOT_READY || initialize() != OK) { return false; } return true; }",
            QEMU_FIXTURE_TASKS[2]: (
                "{ if (create(work->objects, &work->processes[0]) != OK || "
                "reserve(work->registry, &work->endpoints[0]) != OK) { return false; } return true; }"
            ),
            QEMU_SCENARIO_TASKS[0]: "{ if (held_release_denied()) { return false; } return true; }",
            QEMU_SCENARIO_TASKS[1]: "{ if (visibility_or_authority_failed()) { return false; } return true; }",
            QEMU_SCENARIO_TASKS[2]: "{ if (prepare_cancel(&work->cancel_plan) != OK) { return false; } return true; }",
            QEMU_SCENARIO_TASKS[3]: (
                "{ if (ipc_close(work->endpoints[0]) != OK || "
                "commit_cancel(&work->cancel_plan) != OK || reuse_generation() != OK) { return false; } return true; }"
            ),
            QEMU_SCENARIO_TASKS[4]: (
                "{ if (deny_stale() != STALE || cleanup() != OK || "
                "work->objects->live_process_count != work->baseline_processes) { return false; } return true; }"
            ),
        }
        profiles = "static const struct micros_privilege_profile profiles[2] = {{.id = 1}, {.id = 2}};"
        caller = (
            "{ struct endpoint_qemu_work work; uintptr_t saved_status; bool passed = false; "
            "saved_status = riscv_irq_save(); if (!endpoint_qemu_capture_baseline(&work)) { goto done; } "
            "if (!endpoint_qemu_initialize_runtimes(&work) || !endpoint_qemu_reserve_clients(&work)) { goto done; } "
            + " ".join("if (!" + task + "(&work)) { goto done; }" for task in QEMU_SCENARIO_TASKS)
            + " passed = true; done: riscv_irq_restore(saved_status); return passed; }"
        )
        expected = (
            "{ " + profiles + QEMU_WORK_DECLARATIONS + "uintptr_t saved_status; bool passed = false; "
            "saved_status = riscv_irq_save(); objects = acquire_objects(); if (objects == NULL) { goto done; } "
            "baseline_processes = objects->live_process_count; "
            "if (cold() != NOT_READY || initialize() != OK || create(objects, &processes[0]) != OK || "
            "reserve(registry, &endpoints[0]) != OK) { goto done; } "
            "if (held_release_denied()) { goto done; } if (visibility_or_authority_failed()) { goto done; } "
            "if (prepare_cancel(&cancel_plan) != OK) { goto done; } "
            "if (ipc_close(endpoints[0]) != OK || commit_cancel(&cancel_plan) != OK || reuse_generation() != OK) { goto done; } "
            "if (deny_stale() != STALE || cleanup() != OK || objects->live_process_count != baseline_processes) { goto done; } "
            "passed = true; done: riscv_irq_restore(saved_status); return passed; }"
        )
        self.assertEqual(tokens(expected), qemu_entry_body_tokens(caller, tasks, profiles))
        for changed in (
            caller.replace("goto done;", "return false;", 1),
            caller.replace("struct endpoint_qemu_work work;", "static struct endpoint_qemu_work work;"),
            caller.replace("endpoint_qemu_capture_baseline(&work)", "endpoint_qemu_capture_baseline(&other)"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                qemu_entry_body_tokens(changed, tasks, profiles)
        for changed in (
            caller.replace("riscv_irq_restore(saved_status);", ""),
            caller.replace("bool passed = false;", "bool passed = true;"),
            caller.replace(
                "if (!endpoint_qemu_reject_stale_and_cleanup(&work)) { goto done; }",
                "done: if (!endpoint_qemu_reject_stale_and_cleanup(&work)) { goto done; }",
            ),
        ):
            self.assertNotEqual(tokens(expected), qemu_entry_body_tokens(changed, tasks, profiles))
        task = QEMU_SCENARIO_TASKS[2]
        with self.assertRaises(AssertionError):
            qemu_entry_body_tokens(caller, {**tasks, task: tasks[task].replace("return false;", "return true;")}, profiles)

    def test_qemu_readonly_data_and_private_edges_admit_no_new_mutable_context(self):
        paths = family_paths(3)
        fixture = source(paths[1])
        raw = qemu_profile_data(fixture)
        for path in paths:
            expected = file_scope_objects(raw) if path == paths[1] else ()
            self.assertEqual(tuple(map(tokens, expected)), tuple(map(tokens, file_scope_objects(source(path)))))
            self.assertNotRegex(source(path), r'#include\s*"[^"]*endpoint_private\.h"')
            self.assertIn('#include "endpoint_test_internal.h"', source(path))
        for changed in (
            fixture.replace('"CLIENT"', '"WRONG"'),
            fixture.replace("static const struct", "static struct", 1),
            "void local(void) {\n" + fixture + "\n}",
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                qemu_profile_data(changed)
        owners = {name: path for path, name in owned_definitions(3)}
        edges = set()
        for name, path in owners.items():
            calls = set(re.findall(r"[A-Za-z_]\w*", c_lexical_text(c_function_body(source(path), name))))
            for callee in calls & set(owners):
                if owners[callee] != path:
                    self.assertEqual(paths[0], path)
                    edges.add((owners[callee], path))
        self.assertEqual(set(paths), set(topological_order(set(paths), edges)))

    def test_validators_expose_exact_profile_record_process_and_thread_passes(self):
        parameters = {
            "profile_table_is_canonical": "const struct micros_endpoint_registry *registry",
            "endpoint_records_are_canonical": "const struct micros_endpoint_registry *registry",
            "process_relationships_are_canonical": (
                "const struct micros_endpoint_registry *registry, const struct micros_kernel_objects *objects, "
                "bool queued_threads[MICROS_THREAD_CAPACITY]"
            ),
            "thread_relationships_are_canonical": (
                "const struct micros_endpoint_registry *registry, const struct micros_kernel_objects *objects, "
                "const bool queued_threads[MICROS_THREAD_CAPACITY]"
            ),
        }
        for name, owner in VALIDATOR_HELPER_OWNERS.items():
            with self.subTest(name=name):
                text = source(family_paths(0)[owner])
                definitions = c_function_definitions(text)
                self.assertIn(name, definitions)
                self.assertLessEqual(definitions[name], 120)
                signatures = re.findall(rf"(?m)^static bool {name}\([^;{{}}]*\)\s*\{{", text)
                self.assertEqual(1, len(signatures))
                self.assertEqual(
                    tokens(f"static bool {name}({parameters[name]}) {{"), tokens(signatures[0]),
                )

    def test_initialization_exposes_candidate_build_closure_and_nonfailing_full_array_publication(self):
        text = source(family_paths(0)[3])
        definitions = c_function_definitions(text)
        expected = {
            "prepare_profile_candidates": (
                "bool", "const struct micros_privilege_profile *profiles, size_t profile_count, "
                "struct micros_privilege_profile candidate_profiles[MICROS_PRIVILEGE_PROFILE_CAPACITY], uint32_t *installed",
            ),
            "candidate_targets_are_closed": (
                "bool", "const struct micros_privilege_profile candidate_profiles[MICROS_PRIVILEGE_PROFILE_CAPACITY], uint32_t installed",
            ),
            "publish_profile_table": (
                "void", "struct micros_endpoint_registry *registry, "
                "const struct micros_privilege_profile candidate_profiles[MICROS_PRIVILEGE_PROFILE_CAPACITY], size_t profile_count",
            ),
        }
        for name, (result, parameters) in expected.items():
            with self.subTest(name=name):
                self.assertIn(name, definitions)
                self.assertLessEqual(definitions[name], 120)
                signatures = re.findall(rf"(?m)^static {result} {name}\([^;{{}}]*\)\s*\{{", text)
                self.assertEqual(1, len(signatures))
                self.assertEqual(tokens(f"static {result} {name}({parameters}) {{"), tokens(signatures[0]))
        self.assertLessEqual(definitions["micros_endpoint_registry_initialize"], 120)

    def test_initialization_projection_preserves_mask_copy_extent_and_last_magic_publication(self):
        prefix = (
            "{ struct micros_privilege_profile candidate_profiles[MICROS_PRIVILEGE_PROFILE_CAPACITY]; "
            "uint32_t installed = 0;"
        )
        guard = "if (bad_input()) { return ARGUMENT; } clear(candidate_profiles);"
        caller = (
            prefix + guard
            + "if (!prepare_profile_candidates(profiles, profile_count, candidate_profiles, &installed)) { return MICROS_ENDPOINT_ERROR_PROFILE; } "
            "if (!candidate_targets_are_closed(candidate_profiles, installed)) { return MICROS_ENDPOINT_ERROR_PROFILE; } "
            "publish_profile_table(registry, candidate_profiles, profile_count); return OK; }"
        )
        prepare = (
            "{ size_t source_index; size_t other_index; for (source_index = 0; source_index < profile_count; ++source_index) { "
            "if (duplicate(*installed)) { return false; } *installed |= bit(); } return true; }"
        )
        closed = (
            "{ size_t source_index; for (source_index = 1; source_index < LIMIT; ++source_index) { "
            "if (foreign_targets(candidate_profiles, installed)) { return false; } } return true; }"
        )
        publish = (
            "{ copy(registry->profiles, candidate_profiles, sizeof(registry->profiles)); "
            "registry->profile_count = profile_count; registry->initialization_magic = MAGIC; }"
        )
        expected = (
            prefix + "size_t source_index; size_t other_index;" + guard
            + "for (source_index = 0; source_index < profile_count; ++source_index) { "
            "if (duplicate(installed)) { return MICROS_ENDPOINT_ERROR_PROFILE; } installed |= bit(); } "
            "for (source_index = 1; source_index < LIMIT; ++source_index) { "
            "if (foreign_targets(candidate_profiles, installed)) { return MICROS_ENDPOINT_ERROR_PROFILE; } } "
            "copy(registry->profiles, candidate_profiles, sizeof(candidate_profiles)); "
            "registry->profile_count = profile_count; registry->initialization_magic = MAGIC; return OK; }"
        )
        self.assertEqual(tokens(expected), initialization_body_tokens(caller, prepare, closed, publish))
        for changed in (
            caller.replace("candidate_profiles, &installed", "candidate_profiles, &other_mask"),
            caller.replace("return MICROS_ENDPOINT_ERROR_PROFILE;", "return OK;", 1),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                initialization_body_tokens(changed, prepare, closed, publish)
        for changed in (
            prepare.replace("size_t source_index", "static size_t source_index"),
            prepare.replace("source_index = 0", "source_index = 1"),
            prepare.replace("*installed", "installed"),
            prepare.replace("return false;", "return true;"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                initialization_body_tokens(caller, changed, closed, publish)
        for changed in (
            publish.replace("sizeof(registry->profiles)", "sizeof(candidate_profiles)"),
            publish.replace("registry->profile_count = profile_count;", "return;"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                initialization_body_tokens(caller, prepare, closed, changed)
        self.assertNotEqual(
            tokens(expected),
            initialization_body_tokens(caller, prepare, closed, publish.replace(
                "registry->profile_count = profile_count; registry->initialization_magic = MAGIC;",
                "registry->initialization_magic = MAGIC; registry->profile_count = profile_count;",
            )),
        )

    def test_validation_projection_preserves_original_counters_witness_and_caller_failures(self):
        name = "micros_endpoint_registry_validate"
        specifications = VALIDATOR_PHASES[name][1]
        guards = [
            "if (!" + phase + "(" + ", ".join(arguments) + ")) { return MICROS_ENDPOINT_ERROR_INVARIANT; }"
            for phase, arguments, _, _ in specifications
        ]
        caller = "{ enum micros_endpoint_error error; preflight();" + "".join(guards) + "return OK; }"
        bodies = (
            "{ " + REGISTRY_VALIDATION_DECLARATIONS
            + "for (index = 1; index < PROFILES; ++index) { if (bad()) { return false; } ++observed_count; } return true; }",
            "{ size_t index; for (index = 0; index < RECORDS; ++index) { if (bad_record()) { return false; } } return true; }",
        )
        expected = (
            "{ " + REGISTRY_VALIDATION_DECLARATIONS + "enum micros_endpoint_error error; preflight();"
            "for (index = 1; index < PROFILES; ++index) { if (bad()) { return MICROS_ENDPOINT_ERROR_INVARIANT; } ++observed_count; } "
            "for (index = 0; index < RECORDS; ++index) { if (bad_record()) { return MICROS_ENDPOINT_ERROR_INVARIANT; } } return OK; }"
        )
        phases = tuple(zip(specifications, bodies))
        self.assertEqual(tokens(expected), validation_phase_body_tokens(name, caller, phases))
        for changed in (
            caller.replace("return MICROS_ENDPOINT_ERROR_INVARIANT;", "return OK;", 1),
            caller.replace("endpoint_records_are_canonical(registry)", "endpoint_records_are_canonical(other)"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                validation_phase_body_tokens(name, changed, phases)
        for changed in (
            bodies[0].replace("size_t index;", "static size_t index;"),
            bodies[0].replace("index = 1", "index = 0"),
            bodies[0].replace("return false;", "return true;"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                validation_phase_body_tokens(name, caller, ((specifications[0], changed), phases[1]))
        self.assertNotEqual(
            tokens(expected),
            validation_phase_body_tokens(name, caller, ((specifications[0], bodies[0].replace("++observed_count;", "")), phases[1])),
        )
        object_name = "micros_endpoint_registry_validate_objects"
        object_specs = VALIDATOR_PHASES[object_name][1]
        object_guards = [
            "if (!" + phase + "(" + ", ".join(arguments) + ")) { return MICROS_ENDPOINT_ERROR_INVARIANT; }"
            for phase, arguments, _, _ in object_specs
        ]
        object_caller = "{ bool queued_threads[CAP]; clear_witness();" + "".join(object_guards) + "return OK; }"
        object_bodies = (
            "{ size_t index; for (index = 0; index < PROCESSES; ++index) { queued_threads[index] = true; } return true; }",
            "{ size_t index; for (index = 0; index < THREADS; ++index) { if (!queued_threads[index]) { return false; } } return true; }",
        )
        object_phases = tuple(zip(object_specs, object_bodies))
        object_expected = (
            "{ bool queued_threads[CAP]; clear_witness();"
            "for (index = 0; index < PROCESSES; ++index) { queued_threads[index] = true; } "
            "for (index = 0; index < THREADS; ++index) { if (!queued_threads[index]) { return MICROS_ENDPOINT_ERROR_INVARIANT; } } return OK; }"
        )
        self.assertEqual(tokens(object_expected), validation_phase_body_tokens(object_name, object_caller, object_phases))
        with self.assertRaises(AssertionError):
            validation_phase_body_tokens(
                object_name, object_caller.replace("objects, queued_threads", "objects, copied_witness", 1), object_phases,
            )

    def test_all_four_classification_dimensions_and_host_gate_are_unchanged(self):
        qemu = [
            "test-qemu-endpoint", "test-qemu-grant-syscall", "test-qemu-user-runtime",
            "test-qemu-bootstrap-launcher", "test-qemu-vm-handoff", "test-qemu-pm-service",
            "test-qemu-vm-scratch-preemption", "test-qemu-tty", "test-qemu-ramfs",
            "test-qemu-vfs", "test-qemu-elf-spawn", "test-qemu-vm-ready-early",
            "test-qemu-vm-self-fault", "test-qemu-vm-self-fault-sealed",
            "test-qemu-bootstrap-ready-timeout", "test-qemu-bootstrap-manifest-panic",
            "test-qemu-ipc", "test-qemu-ipc-syscall", "test-qemu-ipc-syscall-panic",
            "test-qemu-address-space-handoff",
        ]
        expected = (
            (False, qemu, ["test-ipc-model"], []), (True, [], [], []),
            (False, [], ["test-ipc-model"], []), (False, ["test-qemu-endpoint"], [], []),
        )
        for family, row in enumerate(alias_rows()):
            self.assertEqual(1, kernel_layout_source.original_path_classification_equivalents(validation_plan.PATH_CLASSIFICATION_EQUIVALENTS).count(row))
            for path in (row[0], *row[1]):
                with self.subTest(path=path):
                    self.assertEqual(expected[family], (
                        validation_plan.requires_full_tier([path]),
                        validation_plan.affected_workflows([path]),
                        validation_plan.affected_slow_models([path]),
                        validation_plan.affected_image_workflows([path]),
                    ))
        self.assertEqual(1, kernel_layout_source.original_host_unit_only_inputs(validation_plan.HOST_UNIT_ONLY_INPUTS).count(STRUCTURAL_TEST_PATH))
        self.assertEqual(
            [validation_plan.UNIT_FAST, *validation_plan.diff_commands("main")],
            validation_plan.plan([STRUCTURAL_TEST_PATH], "fast", "main"),
        )

    def test_relocated_literal_token_utility_has_the_entire_original_ast(self):
        module = ast.parse(source("tests/host/test_frame_ownership_source_layout.py"))
        node = next(node for node in module.body if isinstance(node, ast.FunctionDef) and node.name == "tokens")
        self.assertEqual(BASE_CTOKENS_AST, digest_bytes(ast_dump(node).encode()))
        self.assertEqual(('"a b"', "'/'"), tokens('"a b" /* ignored */ \'/\''))
        self.assertNotEqual(tokens("a + + b"), tokens("a ++ b"))
        self.assertNotEqual(tokens("x->field"), tokens("x - > field"))
        original = "{ if (owner != expected) { return STALE; } copy_bytes(); publish(); }"
        for changed in (
            original.replace("!=", "=="), original.replace("STALE", "OK"),
            original.replace("copy_bytes(); publish();", "publish(); copy_bytes();"),
        ):
            self.assertNotEqual(body_digest(original), body_digest(changed))
        self.assertEqual(body_digest(original), body_digest(original.replace("copy_bytes", "/* note */ copy_bytes")))

    def test_function_body_extraction_skips_earlier_calls_and_prototypes(self):
        expected = "{ if (owner != expected_owner) { return false; } return true; }"
        text = (
            "static bool selected(void); "
            "bool caller(void) { if (selected()) { return false; } return true; } "
            "static bool selected(void) " + expected
        )
        self.assertEqual(tokens(expected), tokens(c_function_body(text, "selected")))
        with self.assertRaises(AssertionError):
            c_function_body(text[:text.index("static bool selected(void) " + expected)], "selected")

    def test_four_set_projection_rejects_missing_duplicate_reordered_and_extra_consumers(self):
        before = "begin\n"
        current = before
        for family, (name, paths) in enumerate(SOURCE_SETS.items()):
            before += "".join("consumer(" + paths[0] + ")\n" for _ in range((5, 1, 2, 1)[family]))
            current += source_set_block(name, paths) + "\n\n"
        current += "".join(
            "consumer(${" + name + "})\n"
            for family, name in enumerate(SOURCE_SETS) for _ in range((5, 1, 2, 1)[family])
        )
        self.assertEqual(before, endpoint_cmake_projection(current))
        for changed in (
            current.replace("kernel/endpoint_codec.c", "kernel/endpoint.c", 1),
            current.replace("${MICROS_ENDPOINT_CORE_SOURCES}", "kernel/endpoint.c", 1),
            current + "consumer(${MICROS_ENDPOINT_CORE_SOURCES})\n",
            current.replace("    kernel/endpoint_codec.c\n", "", 1),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                endpoint_cmake_projection(changed)
        self.assertEqual(BASE_CMAKE_DIGEST, digest_bytes(restored_cmake(source("CMakeLists.txt")).encode()))
        with self.assertRaises(AssertionError):
            restored_cmake(source("CMakeLists.txt") + "\n# unreviewed\n")

if __name__ == "__main__":
    unittest.main()

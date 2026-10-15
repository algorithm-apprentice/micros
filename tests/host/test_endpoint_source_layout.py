"""Bounded source-contract regression for Accepted ADR-0067."""

import ast
import hashlib
import json
from pathlib import Path
import re
import unittest

from tests.host.test_frame_ownership_source_layout import (
    c_function_definitions,
    c_lexical_text,
    tokens,
)
from tests.host.test_kernel_bootstrap_source_layout import c_function_body
from tests.host.test_pm_service_source_layout import cmake_commands
from tools.check_docs import topological_order
from tools import validation_context, validation_plan


ROOT = Path(__file__).resolve().parents[2]
REVIEWED_BASE = "7e107f5189cbf6f3aa44dfc05652d041e803c60c"
BASE_CMAKE_DIGEST = "cd615eaa75141a5626a024de1ad7772e254c47129ed34829a9b01a472da28dd6"
BASE_CTOKENS_AST = "3e378fcb1e32c96d05240c3d80401d159715196d92de372d8ca92cdddd9a3cd8"
MODEL_TYPES_DIGEST = "d6da66f1eb42b10fd121f8316a01b5cedbf6c80bb31696a5b3693779f349fb2e"
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
SYMBOL_OWNER_MIGRATION = tuple(
    pair for pair in INITIAL_SYMBOL_OWNER_MIGRATION if pair[0] != 0
)
LONG_FUNCTION_MIGRATION = LONG_FUNCTIONS
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
    return (ROOT / path).read_text()


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


def normal_names(family):
    if family == 0:
        return {private_name(name): name for name in CORE_PRIVATE_EXPORTS}
    return {}


def body_digest(text, renames=None):
    names = renames or {}
    body = tuple(names.get(token, token) for token in tokens(text))
    return digest_bytes(json.dumps(body, separators=(",", ":")).encode())


def owned_definitions(family):
    return {
        (path, name): lines
        for path in family_paths(family) if (ROOT / path).is_file()
        for name, lines in c_function_definitions(source(path)).items()
    }


def original_locations(family):
    expected = {name for names in ORIGINAL_FUNCTIONS[family].values() for name in names}
    renames = normal_names(family)
    locations = {}
    for path, spelling in owned_definitions(family):
        name = renames.get(spelling, spelling)
        if name not in expected or name in locations:
            raise AssertionError(f"unknown/duplicate original or unreviewed phase: {family}/{spelling}")
        locations[name] = (path, spelling)
    if set(locations) != expected:
        raise AssertionError("missing original definitions")
    return locations


def source_set_block(name, paths):
    return "set(\n    " + name + "\n" + "".join("    " + path + "\n" for path in paths) + ")"


def endpoint_cmake_projection(text):
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
            if (ROOT / path).is_file()
            and names & set(c_function_definitions((ROOT / path).read_text()))
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
                        body_digest(c_function_body(source(path), spelling), normal_names(family)),
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
                    self.assertTrue((ROOT / path).is_file(), path)
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
        self.assertEqual(digest_bytes(cmake.encode()), validation_context.RECIPE_DIGESTS["CMakeLists.txt"])

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
        for name, (path, spelling) in locations.items():
            calls = set(re.findall(r"\b([A-Za-z_]\w*)\s*\(", c_lexical_text(c_function_body(source(path), spelling))))
            for call in calls:
                callee = names.get(call, call)
                if callee not in locations:
                    continue
                dependency = locations[callee][0]
                if dependency != path:
                    current_edges.add((dependency, path))
                    if not call.startswith("micros_"):
                        actual_exports.add(call)
                source_owner = family_paths(0).index(final[name])
                dependency_owner = family_paths(0).index(final[callee])
                if source_owner != dependency_owner:
                    self.assertIn(dependency_owner, CORE_DAG[source_owner], (name, callee))
                    final_edges.add((final[callee], final[name]))
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
                self.assertEqual(expected, digest_bytes((ROOT / path).read_bytes()))

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
            self.assertEqual(1, validation_plan.PATH_CLASSIFICATION_EQUIVALENTS.count(row))
            for path in (row[0], *row[1]):
                with self.subTest(path=path):
                    self.assertEqual(expected[family], (
                        validation_plan.requires_full_tier([path]),
                        validation_plan.affected_workflows([path]),
                        validation_plan.affected_slow_models([path]),
                        validation_plan.affected_image_workflows([path]),
                    ))
        self.assertEqual(1, validation_plan.HOST_UNIT_ONLY_INPUTS.count(STRUCTURAL_TEST_PATH))
        self.assertEqual(
            [validation_plan.UNIT_FAST, *validation_plan.diff_commands("main")],
            validation_plan.plan([STRUCTURAL_TEST_PATH], "fast", "main"),
        )

    def test_relocated_literal_token_utility_has_the_entire_original_ast(self):
        module = ast.parse(source("tests/host/test_frame_ownership_source_layout.py"))
        node = next(node for node in module.body if isinstance(node, ast.FunctionDef) and node.name == "tokens")
        self.assertEqual(BASE_CTOKENS_AST, digest_bytes(ast.dump(node, include_attributes=False).encode()))
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

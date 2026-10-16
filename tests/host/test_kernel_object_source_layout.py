"""Bounded source-contract checks for Accepted ADR-0068."""

import ast
import hashlib
import json
from pathlib import Path
import re
import unittest

from tests.host.c_source_records import function_records, matching_lexical
from tests.host.test_frame_ownership_source_layout import c_function_definitions, c_lexical_text, tokens
from tests.host.test_pm_service_source_layout import cmake_commands
from tests.host.test_vm_service_source_layout import file_scope_objects
from tools.check_docs import topological_order
from tools import validation_context, validation_plan


ROOT = Path(__file__).resolve().parents[2]
REVIEWED_BASE = "4eb1c4d68de3830b888a0d239a2874bd79792244"
BASE_CMAKE_DIGEST = "50cac89bbc8dfc0246b97d36be3585ff53f7e86c3dbc772539a6f8352e05db7d"
BASE_CORE_DIGEST = "453c3cfbfb02d1c014226a315106a157676a7b195be06f561bb3abbda042db4a"
STRUCTURAL_TEST_PATH = "tests/host/test_kernel_object_source_layout.py"
SHARED_INSPECTION_PATH = "tests/host/c_source_records.py"
HOST_MACROS_DIGEST = "cef5a866a045d5de47e43507e2dd21dc9c609dd30f096224ae36301633b972a1"
MODEL_TYPES_DIGEST = "d8e00a14a902b448209ed4f27d866534d4ef32a46baeb2d5407c83fff825a444"
HOST_CASE_TABLE_DIGEST = "b3566dbd412d23b759fc2fd7c3efaf6f083bb0dfd9617fce9fa35c84a164bf90"
REGISTRY_FIELDS = (
    "initialization_magic", "max_threads_per_process", "max_harts", "live_process_count",
    "live_thread_count", "registered_hart_count", "processes", "threads", "harts",
)
MODEL_PROJECTION_PHASES = (
    ("kernel_object_test_model_processes_match", ("fixture", "model"), "MICROS_PROCESS_CAPACITY"),
    ("kernel_object_test_model_threads_match", ("fixture", "model"), "MICROS_THREAD_CAPACITY"),
    ("kernel_object_test_model_harts_match", ("fixture",), "MICROS_HART_CAPACITY"),
)
HART_STACK_PHASES = (
    (
        "kernel_object_hart_stacks_avoid_harts",
        ("objects", "hart", "primary_stack_bottom", "primary_stack_top", "emergency_stack_bottom", "emergency_stack_top"),
        "MICROS_HART_CAPACITY",
    ),
    (
        "kernel_object_hart_stacks_avoid_threads",
        ("objects", "primary_stack_bottom", "primary_stack_top", "emergency_stack_bottom", "emergency_stack_top"),
        "MICROS_THREAD_CAPACITY",
    ),
)
CONTEXT_STACK_PHASES = (
    (
        "kernel_object_context_stack_avoids_threads",
        ("objects", "thread", "kernel_stack_bottom", "kernel_stack_top"),
        "MICROS_THREAD_CAPACITY",
    ),
    (
        "kernel_object_context_stack_avoids_harts",
        ("objects", "kernel_stack_bottom", "kernel_stack_top"),
        "MICROS_HART_CAPACITY",
    ),
)
STACK_CONFLICT_TASKS = {
    "micros_hart_install_trap_stacks": (7, "hart", HART_STACK_PHASES),
    "micros_thread_attach_execution_context": (6, "thread", CONTEXT_STACK_PHASES),
}
CORE_SOURCES = (
    "kernel/kernel_objects.c",
    "kernel/kernel_object_state.c",
    "kernel/kernel_object_identity.c",
    "kernel/kernel_object_stack.c",
    "kernel/kernel_object_process.c",
    "kernel/kernel_object_thread.c",
    "kernel/kernel_object_context.c",
    "kernel/kernel_object_hart.c",
    "kernel/kernel_object_validation.c",
)
SOURCE_SETS = {
    "MICROS_KERNEL_OBJECT_CORE_SOURCES": CORE_SOURCES,
    "MICROS_KERNEL_OBJECT_HOST_TEST_SOURCES": (
        "tests/host/kernel_objects_test.c",
        "tests/host/kernel_objects_test_fixture.c",
        "tests/host/kernel_objects_initialization_test.c",
        "tests/host/kernel_objects_process_test.c",
        "tests/host/kernel_objects_thread_test.c",
        "tests/host/kernel_objects_hart_test.c",
        "tests/host/kernel_objects_identity_test.c",
        "tests/host/kernel_objects_validation_test.c",
        "tests/host/kernel_objects_model_test.c",
        "tests/host/kernel_objects_model_reference.c",
        "tests/host/kernel_objects_model_projection.c",
        "tests/host/kernel_objects_model_operations.c",
    ),
    "MICROS_KERNEL_OBJECT_QEMU_TEST_SOURCES": (
        "kernel/object_model_test.c",
        "kernel/object_model_test_scenarios.c",
    ),
}
PRIVATE_HEADERS = (
    "kernel/kernel_object_private.h",
    "tests/host/kernel_objects_test_internal.h",
    "kernel/object_model_test_internal.h",
)
ORIGINAL_FUNCTIONS = {
    0: {
        0: "micros_kernel_objects_initialize micros_kernel_objects_validate".split(),
        1: (
            "bytes_are_zero clear_bytes storage_is_zero thread_scheduler_metadata_is_zero "
            "clear_thread_scheduler_metadata micros_thread_ipc_state_is_clear clear_thread_ipc_state "
            "copy_user_context clear_user_context user_context_is_zero trap_anchor_is_zero timer_state_is_initial"
        ).split(),
        2: (
            "process_handle_is_valid thread_handle_is_valid hart_handle_is_valid thread_handles_equal "
            "thread_handle_is_null hart_handle_is_null require_initialized resolve_process_mutable "
            "resolve_thread_mutable resolve_hart_mutable micros_process_next_generation micros_thread_next_generation"
        ).split(),
        3: "stack_range_is_valid ranges_overlap trap_anchor_stacks_are_valid".split(),
        4: (
            "micros_process_resolve micros_process_create micros_process_create_at micros_process_release "
            "micros_process_attach_address_space micros_process_detach_address_space"
        ).split(),
        5: "micros_thread_resolve micros_thread_create micros_thread_release".split(),
        6: (
            "micros_thread_attach_execution_context micros_thread_capture_execution_context "
            "micros_thread_inspect_execution_context micros_thread_detach_execution_context"
        ).split(),
        7: (
            "micros_hart_resolve micros_hart_register micros_hart_resolve_context "
            "micros_hart_install_trap_stacks micros_hart_current_thread"
        ).split(),
        8: ["micros_kernel_objects_validate_base"],
    },
    1: {
        0: ["main"],
        1: "reset_objects context_pattern contexts_equal".split(),
        2: (
            "test_initialization_is_checked_and_one_shot test_uninitialized_registry_rejects_every_operation "
            "test_fixed_capacities_report_exhaustion_atomically"
        ).split(),
        3: (
            "test_process_slots_reuse_with_new_generations test_process_exact_slot_creation_is_isolated "
            "test_process_address_space_lifecycle_is_exact"
        ).split(),
        4: (
            "test_thread_execution_context_lifecycle_is_exact test_thread_slots_reuse_with_new_generations "
            "test_thread_ownership_and_production_limit test_model_policy_supports_multiple_threads"
        ).split(),
        5: ["test_hart_policy_and_stack_validation_are_atomic"],
        6: "test_generation_boundaries_and_quarantine test_invalid_arguments_and_stale_handles_preserve_outputs".split(),
        7: ["test_validator_rejects_corrupt_relationships"],
        8: ["test_seeded_lifecycle_model"],
        9: (
            "process_handles_equal model_lowest_free_process model_lowest_free_thread "
            "model_process_handle model_thread_handle model_reports_expected_error"
        ).split(),
        10: ["lifecycle_model_matches_registry"],
        11: ["model_random"],
    },
    2: {0: ["micros_kernel_object_runtime_run_self_test"], 1: []},
}
CORE_PRIVATE_EXPORTS = (
    "storage_is_zero process_handle_is_valid thread_handle_is_valid hart_handle_is_valid thread_handles_equal "
    "thread_handle_is_null hart_handle_is_null thread_scheduler_metadata_is_zero clear_thread_scheduler_metadata "
    "clear_thread_ipc_state stack_range_is_valid ranges_overlap copy_user_context clear_user_context "
    "user_context_is_zero trap_anchor_is_zero trap_anchor_stacks_are_valid require_initialized "
    "resolve_process_mutable resolve_thread_mutable resolve_hart_mutable timer_state_is_initial"
).split()
LONG_FUNCTIONS = (
    (0, "micros_hart_install_trap_stacks"),
    (0, "micros_kernel_objects_validate_base"),
    (1, "test_uninitialized_registry_rejects_every_operation"),
    (1, "test_thread_execution_context_lifecycle_is_exact"),
    (1, "test_generation_boundaries_and_quarantine"),
    (1, "lifecycle_model_matches_registry"),
    (1, "test_seeded_lifecycle_model"),
    (2, "micros_kernel_object_runtime_run_self_test"),
)
SHORTER_TASKS = (
    (0, "micros_thread_attach_execution_context"),
    (1, "test_fixed_capacities_report_exhaustion_atomically"),
    (1, "test_process_address_space_lifecycle_is_exact"),
    (1, "test_validator_rejects_corrupt_relationships"),
)
REQUIRED_TASKS = (*LONG_FUNCTIONS, *SHORTER_TASKS)
INITIAL_SYMBOL_OWNER_MIGRATION = tuple(
    (family, name) for family, owners in ORIGINAL_FUNCTIONS.items()
    for owner, names in owners.items() if owner != 0 for name in names
)
SYMBOL_OWNER_MIGRATION = ()
FUNCTION_TASK_MIGRATION = (
    (0, "micros_kernel_objects_validate_base"),
    (1, "test_uninitialized_registry_rejects_every_operation"),
    (1, "test_thread_execution_context_lifecycle_is_exact"),
    (1, "test_generation_boundaries_and_quarantine"),
    (1, "test_seeded_lifecycle_model"),
    (2, "micros_kernel_object_runtime_run_self_test"),
    (1, "test_fixed_capacities_report_exhaustion_atomically"),
    (1, "test_process_address_space_lifecycle_is_exact"),
    (1, "test_validator_rejects_corrupt_relationships"),
)
CORE_DAG = {
    0: {1}, 1: {2}, 2: set(), 3: set(), 4: {2},
    5: {1, 2}, 6: {1, 2, 3, 5}, 7: {2, 3, 5}, 8: {1, 2, 3},
}
BASELINE_SIGNATURES = (
    "9010731d332ba9e936802a3e43feab1bae99af4bb80bd61e2e282d6ee2025db1",
    "b39fe6c7b6086a51bfcf3a8816438334d26de32fd12b07efea880e720ee40968",
    "7286deed76b4e7245c39433585d2d17a33e6919eab722c3f34166d05adeb2bcc",
)
BASELINE_BODIES = {
    0: {
        "bytes_are_zero": "6a6327e7774d4d0381673fcee5103704058d8b7004c210a70a93a6c0ace932f8",
        "clear_bytes": "1bfe896da396160656d79ce0e67c19f9a3ed382a4d894e12c0abdce0c797e1a0",
        "storage_is_zero": "d7a54408fe91b6861d2b937db0855867770e8a753345d65a042e10d89fff3f16",
        "process_handle_is_valid": "2bc62468bf177fa1f353c4ce344cbe58bcd3f7f89c3174202d21a957d9defa5f",
        "thread_handle_is_valid": "6d1ec96b91e22e394ce1c53d8078a267dfcfcd34e3293e6d573c2d8eaf9b827e",
        "hart_handle_is_valid": "74bbd2919a91715fdd6ded843c506b9ab52a149ce03ffee509cc99dcf5281cfc",
        "thread_handles_equal": "1eb9ca3d14ed288168beacc0916c15eb03aa6ecf2e283e022b4d3ccb7e92f678",
        "thread_handle_is_null": "2f4d5857a78d658059c3e3faa75d3ff69a56c09a4d573a89d695b633d7299d5e",
        "hart_handle_is_null": "2f4d5857a78d658059c3e3faa75d3ff69a56c09a4d573a89d695b633d7299d5e",
        "thread_scheduler_metadata_is_zero": "abc4ca92735c2f8aea744b0928256114cfbcdd5eb6214ee637fa2856a1f678ab",
        "clear_thread_scheduler_metadata": "2a1bc8a1b9bd6a27c8d6b3a1797de9deb2a1162f4338a8a8b1983113721d1de3",
        "micros_thread_ipc_state_is_clear": "7bcf82008f555152f6857df8771cbaf562ab7a511dd4cfc5e4f0a515c83f48c4",
        "clear_thread_ipc_state": "fe88b17b377077ac564b6b8bdd6f6528eef4bbf09836652c10ae54eb293dce99",
        "stack_range_is_valid": "d314e5a01cdf87c7cfc611b096912b1b61c3da78353ba85a42ce9d3a0f13bb05",
        "ranges_overlap": "67e80565d3bf4bfae293bbee016dfb6737738e44f817e33b801628afe5eb1d79",
        "copy_user_context": "2afe82178b061416299109a458ca503581360aef82fd57ab5c3c100b01cd8851",
        "clear_user_context": "e4a54f8a45d63685840a15073e83fd158071fecdf30cce5c8049ea74c62561cf",
        "user_context_is_zero": "67bb620d312715fa872be97103a06e0f54e8e5d201b8612ee69999e5e7b2ad45",
        "trap_anchor_is_zero": "be63065e19f73835854e19b9a3fa681f8a25593f1ff94d16d09f8e39ce428926",
        "trap_anchor_stacks_are_valid": "fb26862314688b37fbd165a94d42d1d923081251b2dff56fe855c2d5684d539e",
        "require_initialized": "23677722aea4e59474529dd7dbb1bfe327c75e0e0a546b7b91ca6e2be5676605",
        "resolve_process_mutable": "68825299ca5388fcf31eb60da190d33287fd8eeeb81aaf16520ec4bb341e845b",
        "resolve_thread_mutable": "3642aac7f900241b76f281669d71d51de16b77fd4ba52bdbbd66cc899ee05390",
        "resolve_hart_mutable": "f4f2f334bda00332040907ede742cd6ad1f3aa4a26732525bc194d8bf8325aba",
        "timer_state_is_initial": "be2904c1524fd362d34433da3305b35726a3d8ef997818956a6ce44f03b25e33",
        "micros_process_next_generation": "5d0cedb28266082ea79f53141b9e5e1234022de87ad81729c57fe77670531689",
        "micros_thread_next_generation": "6326dac0fd48837c1b403f4a6ca1e0e0a290d3f2ce54dabf4cb36413271b139e",
        "micros_kernel_objects_initialize": "77d09c42954a57293c4de8a79e7205833299d1813e233bd85de3789e099ba811",
        "micros_process_resolve": "8fe940a1d0f1ad3c0cc4ae345c50e0806c4fcfb90d01b0e877d5b64f30fb15bc",
        "micros_process_create": "5c1b3d6c60d0ee8f627cbccbf7e1530e7f6442ffeb00e98592b1c834c6e71ed8",
        "micros_process_create_at": "adc49cdbacd3c361f10a6c879c33fa1ec8e8befe47d7fda90f6afc98909f7edb",
        "micros_process_release": "afb1c120485b5b508e1bb6522f5b72e0bb1112743e6c5f63886147c02a022dc6",
        "micros_thread_resolve": "eb58dbc97be2deaf41313bcb8020884d7d6e53f1eb749ff97c5ffe0e7d1226c4",
        "micros_thread_attach_execution_context": "ce1ea34128a4c5d01865941bf66299836d6880979f9b160002d490a2983cc353",
        "micros_thread_capture_execution_context": "b629a2900321ff0b711322fc594eab9406760483751684ad4b0cfaaebc8be5f7",
        "micros_thread_inspect_execution_context": "89f556ca29b2292ec8015935c033c26e5a85f88ac846ac96150dd1eb0073c3ba",
        "micros_thread_detach_execution_context": "b8abc59908db98f7c21fe2a57b3d8c0148072708aacfe3b30124d330e8da1fd5",
        "micros_process_attach_address_space": "affa5ed93a38ef93cde624712e3e89903ed14119c557310d0ef7bd287207437b",
        "micros_process_detach_address_space": "ebf772fea15a08ece81e702dfcf7ccdf9021c47205f7a175011698bdd62d59a2",
        "micros_thread_create": "3cd94ad146d2854325c0867748771f4c6f5861771835122db0b30952c7c29d97",
        "micros_thread_release": "3f062646fcbdee574a23979c8fe942d12c9a347de7404ee89c1db7aa8bf019f0",
        "micros_hart_resolve": "768dd87c2e34c8a4fa3c1348e022b151d15b4834eea99c84a986acca6a6256a5",
        "micros_hart_register": "ea2e3d1a274dd04372d8236bd4d73ca2b6500026b7b940d109283d3386d53c3a",
        "micros_hart_resolve_context": "d8d14687170a56d35f12d5c254d5d318f4d3a9119890c2acb65cc7654238d46c",
        "micros_hart_install_trap_stacks": "fa317d8c197e950cb6e46867110e8936fa58c36a9fa43502535dbaba8acd7f2b",
        "micros_hart_current_thread": "ff46b0e0ff7c4f7bca4a908c7364aa914a14dffc83f0387ebe1176b9ce7327f4",
        "micros_kernel_objects_validate": "8198b41338088f95d960097d7e1c662da347e2c4c92750b5670696fd738e6079",
        "micros_kernel_objects_validate_base": "fe2cf26c2cf20f464ddb38aff6a29907b4db0fc755c4fcd343d520a555fc2eab",
    },
    1: {
        "reset_objects": "bcf98945593ede83ead0ccc8605cdb7fe8979a38e1e9a02f69b3b9e26da4c4d9",
        "process_handles_equal": "1eb9ca3d14ed288168beacc0916c15eb03aa6ecf2e283e022b4d3ccb7e92f678",
        "context_pattern": "27beff55faa7c13139dcbfb8ca672501f612ff42033bf579e2651fabbb46d06c",
        "contexts_equal": "edae7519639b5ac9d8623bb0d1491d55b1de8fb2ada7ef2729797d0410773115",
        "test_initialization_is_checked_and_one_shot": "dc6778f882120773227bf775d8723fd8a0abf63ac818ffeb09047803e0be9e1c",
        "test_uninitialized_registry_rejects_every_operation": "75227aacab44588a750f0b9590396b56219ba75fe0e08b095dff0d65aa2b8460",
        "test_fixed_capacities_report_exhaustion_atomically": "ac9f4fa6aa689838322470424703f83c709afce18544f13837ddbbe6459b3766",
        "test_process_slots_reuse_with_new_generations": "edcaac9e67c32b1a724ba785802fb50b5b7c2dac907ddb437c590ef600451ba3",
        "test_process_exact_slot_creation_is_isolated": "07d36d96ec7d5b3e8ffb84a15047adfd32fb733f84f5371e5961d75fa84b4f2c",
        "test_process_address_space_lifecycle_is_exact": "e3b323123c27ff60f9bb3345d81ade81745d2308c6d061376716dbba73af9097",
        "test_thread_execution_context_lifecycle_is_exact": "5ee589b0bc6ebfd5535e97a4949c32ed4286bdf9a50649ee15615e6483c2ef3f",
        "test_thread_slots_reuse_with_new_generations": "2a4fd406187a51fe7414d6b2a2ec4d9d9abeb699232cf7520c410458f8af4e03",
        "test_thread_ownership_and_production_limit": "a96475b300a3f87ecac73ea3d1eaa85f829542a546196e30ec00d023c11731c2",
        "test_model_policy_supports_multiple_threads": "2f1f2298077e2edb46b4e50efb4221f1c6f4a5277b561c77c9a702360a311702",
        "test_hart_policy_and_stack_validation_are_atomic": "f8e5650419eea59e653fb76cda940818d0a88ea26fae6517ae44ad4a2dbe5332",
        "test_generation_boundaries_and_quarantine": "0584f888942a8478b40593d31a512d9a4cf868bba75bea4beac21f7f49bf2ca0",
        "test_invalid_arguments_and_stale_handles_preserve_outputs": "e91d7f008e751cc9eb8c897ebabe9595685745626cb32d824185ead382f93502",
        "model_random": "7dcaf7a92f2e711f3c5895cc3c4c2254d1f1104a83fa875186db6f3da9489b92",
        "model_lowest_free_process": "4b2eeac086f57a6cf5c4140ec3583b23e13744538ba631d2d74f5b23c43d3d10",
        "model_lowest_free_thread": "a935acf6aedc965cf12b7d8def34a756887f8d4a78934c1b8ed0526617e60c77",
        "model_process_handle": "5e86f7b2395923d5ccd7b3d4fd1ec08ac7a02915d4d63fa9d11ea8d3603ffa3f",
        "model_thread_handle": "f55006c7793fa04383bab5bfe8f4666546255ab3f05d1ecf7f5b0cea757b85dc",
        "model_reports_expected_error": "1335362cc0b94845b3a90a7e74f21971043aa9ac525e32fdcb8eb79cb8ebf505",
        "lifecycle_model_matches_registry": "afcf0a20496858b95ea471d306df0fa0df43a96fdd4dc373d07d7806bd6a515b",
        "test_seeded_lifecycle_model": "96800ce8d488fe60684653f33b7cf79fae758aa2b7bd3bcf38e26e372d16767c",
        "test_validator_rejects_corrupt_relationships": "49d1f7a4b7ce6c5837d31cd7ff081f6af7205b37341c473eea0d9462d31c783e",
        "main": "685f3b5446ace3b45e224122fef19a20452a3c4bb3cf64da9644acc59dfa182b",
    },
    2: {
        "micros_kernel_object_runtime_run_self_test": "f5df9f3de5263baf8b1dcbb61e989982d03ec62e4c6f41320ded9de205d81579",
    },
}
STABLE_HASHES = {
    "include/micros/kernel_objects.h": "106fe5aaa03b06b1746c73aa24588749d4cfe02c6b1e4a11efc1f5317f7523e3",
    "include/micros/kernel_object_runtime.h": "46af15cfa0d00649ea2e481e6f529bf8aae1e63b89d23ffef602b98b0da3fd11",
    "kernel/kernel_object_runtime.c": "22925d14e6000b26a78403cc2dfbacc023bca6a4e239ec1bdcd1570b91a85a43",
    "kernel/kernel_object_runtime_internal.h": "23917cbd674d926ec7ffe8700747fc4626f3a31dc4620ed4f7d4ee8147f57c72",
    "include/micros/user_context.h": "92a1034064b570385d720849e0e42a975581fd595fd6ad41130cc0e10f82ca90",
    "include/micros/scheduler_core.h": "62976539f042a654003034157beefcefe25f90a5e92247461fb1fe3f704044f1",
    "kernel/scheduler_core_internal.h": "c9378e2e926b28400c474a35f6eb102aeed017a01fe1d367bb1f33687974bfb6",
    "kernel/scheduler_core_validate.c": "73bd02fdac06a05c0bdc33130258132e3fe90cfe8c2204f5502e054d5021f38c",
    "include/micros/endpoint.h": "57e8c5b5536ddc270015be7e2ccef07008e6d00ae514e27fb3c7ebc55c1f871c",
    "kernel/endpoint_internal.h": "fa8bc084ed5b6777a805abb4def2d8d20f747a3b193cbc9fa46a9b6c1145caac",
    "arch/riscv64/trap_context.h": "584a45235e8eea9a973ccaebb0ace4b84f22778f360625a68feecf1dc276e5d3",
    "arch/riscv64/scheduler_context.h": "3b854415cf846f8c940afbed956a975651b44a6630fcd77eed7470150beb81ae",
    "arch/riscv64/trap.S": "e541ebbc7c1ab6e68eb366ec9ea353bc381db55364b999c7516e30a56c96666d",
    "arch/riscv64/user_entry.S": "7dca2e414e36db2851f7f7ded760fd78ca8da0a74b9fee677aee8699b7f6d9d2",
    "arch/riscv64/scheduler.S": "4c75823725dc68cc8e9b5e5d7051946e2418b322014a6e3e5fe1d72df5bc85ea",
    "kernel/boot_component_tests.c": "6a22c42e953c6af39be80baee6334a583658e150f0dde2944d637692b4a53cc3",
    "CMakePresets.json": "eae6c86ce5ac8f0e1b39605ebbd6816135558f0b4f8ea293b3680d7ecb79df86",
    "tools/run_qemu_smoke.py": "9e54f744074d077d05b52880495b73f7c74ae1db0a6ad751f8b7024688bf79e3",
}
SHARED_INSPECTION_ASTS = {
    "compact": "b1c6f8d02e33ae3835898a1cc1983f029d1da7d0b30aae32cb42069a06a7138c",
    "matching": "582aff5ac86e8d1746f0328fbe519d5439c40393e3713fd48cce71bf5b4d3837",
    "matching_lexical": "060470315942e9444843ca5aafd193340085c98981a0a5292c5e9f6ee009be15",
    "cpp_guards": "4065926ac0c263d65cf4943286b94cf45d8e57c8335c116ce2e39a5895ca74c8",
    "Function": "312fdebc83fa138ed1822691b7a540be4d0f3330a2be8de5164fa74f9ec86eef",
    "function_records": "068cc6a2b9f64e99a0d937ac5c64a60291c23608ef1f637f170c54e851f5cf72",
}


def digest_bytes(value):
    return hashlib.sha256(value).hexdigest()


def digest_tokens(value):
    return digest_bytes(json.dumps(tuple(value), separators=(",", ":")).encode())


def family_paths(family):
    return tuple(SOURCE_SETS.values())[family]


def source(path):
    return (ROOT / path).read_text()


def private_name(name):
    return "kernel_object_" + name


def host_name(name):
    if name == "main":
        return name
    return "kernel_object_test_" + (name[5:] if name.startswith("test_") else name)


def normal_names(family):
    if family == 0:
        return {private_name(name): name for name in CORE_PRIVATE_EXPORTS}
    if family == 1:
        return {host_name(name): name for name in BASELINE_BODIES[1] if name != "main"}
    return {}


def owned_records(family, texts=None):
    texts = texts if texts is not None else {
        path: source(path) for path in family_paths(family) if (ROOT / path).is_file()
    }
    return {
        (path, name): record for path, text in texts.items()
        for name, record in function_records(text).items()
    }


def original_locations(family, texts=None):
    names = normal_names(family)
    locations = {}
    stack_owners = {
        phase[0]: CORE_SOURCES[owner] for owner, _, phases in STACK_CONFLICT_TASKS.values() for phase in phases
    }
    for (path, spelling), record in owned_records(family, texts).items():
        if family == 0 and spelling in stack_owners:
            if path != stack_owners[spelling]:
                raise AssertionError("stack conflict passes have their exact file-local owner")
            continue
        if family == 1 and spelling in {phase[0] for phase in MODEL_PROJECTION_PHASES}:
            if path != family_paths(1)[10]:
                raise AssertionError("model projection passes have their exact file-local owner")
            continue
        if family == 1 and spelling == "kernel_object_test_registry":
            if path != family_paths(1)[1]:
                raise AssertionError("the sole native registry getter has one exact fixture owner")
            continue
        name = names.get(spelling, spelling)
        if name not in BASELINE_BODIES[family] or name in locations:
            raise AssertionError(f"unknown/duplicate original or unreviewed phase: {family}/{spelling}")
        locations[name] = (path, record)
    if set(locations) != set(BASELINE_BODIES[family]):
        raise AssertionError("missing original definitions")
    return locations


def original_signature_tokens(family, original, record):
    names = normal_names(family)
    signature = tuple(names.get(token, token) for token in tokens(record.signature))
    moved_private = (
        family == 0 and original in CORE_PRIVATE_EXPORTS and record.name == private_name(original)
    ) or (family == 1 and original != "main" and record.name == host_name(original))
    if moved_private:
        if signature[:1] == ("static",):
            raise AssertionError("cross-owner private entries have their exact declared external scope")
        signature = ("static", *signature)
        if family == 1 and original == "reset_objects":
            if signature != tokens(
                "static void reset_objects(struct micros_kernel_objects *fixture)"
            ):
                raise AssertionError("reset receives exactly its original registry, never another fixture")
            signature = tokens("static void reset_objects(void)")
        elif family == 1 and original == "lifecycle_model_matches_registry":
            prefix = tokens(
                "static bool lifecycle_model_matches_registry(const struct micros_kernel_objects *fixture,"
            )
            if signature[:len(prefix)] != prefix:
                raise AssertionError("model projection receives the exact readonly borrowed registry first")
            signature = (*tokens("static bool lifecycle_model_matches_registry("), *signature[len(prefix):])
    return signature


def restore_registry_tokens(body):
    body = list(body)
    result = []
    index = 0
    while index < len(body):
        if body[index:index + 2] == ["fixture", "->"]:
            if index and body[index - 1] in (".", "->"):
                raise AssertionError("registry transport cannot qualify an existing member as a global root")
            field = body[index + 2:index + 3]
            if not field or field[0] not in REGISTRY_FIELDS:
                raise AssertionError("registry transport admits only the original public registry fields")
            result.extend(("objects", ".", field[0]))
            index += 3
        elif body[index:index + 2] == ["*", "fixture"] and body[index + 2:index + 3] != ["->"]:
            if index and body[index - 1] in (".", "->"):
                raise AssertionError("registry dereference cannot be an existing member's new authority")
            result.append("objects")
            index += 2
        else:
            if body[index] == "fixture":
                if index and body[index - 1] in (".", "->", "&"):
                    raise AssertionError("only the explicitly borrowed registry pointer may be transported")
                result.extend(("&", "objects"))
            else:
                result.append(body[index])
            index += 1
    return tuple(result)


def native_registry_body_tokens(original, body):
    body = list(body)
    borrow = list(tokens(
        "struct micros_kernel_objects *fixture = kernel_object_test_registry();"
    ))
    if original.startswith("test_"):
        if body[:1 + len(borrow)] != ["{"] + borrow:
            raise AssertionError("each native case borrows its one original registry once without a reset")
        del body[1:1 + len(borrow)]
    restored = []
    index = 0
    while index < len(body):
        token = body[index]
        if token in ("reset_objects", "lifecycle_model_matches_registry") and body[index + 1:index + 2] == ["("]:
            delimiter = ")" if token == "reset_objects" else ","
            if body[index + 2:index + 4] != ["fixture", delimiter]:
                raise AssertionError("registry helpers bind the exact caller-owned fixture as first argument")
            restored.extend((token, "("))
            index += 3 if delimiter == ")" else 4
        else:
            restored.append(token)
            index += 1
    return restore_registry_tokens(restored)


def model_projection_body_tokens(caller, phases):
    body = list(tokens(caller))
    declaration = list(tokens("size_t index;"))
    if body[:1] != ["{"] or "index" in body:
        raise AssertionError("model root retains no duplicate or hidden loop counter")
    guard = list(tokens(
        "if (!" + " || !".join(
            name + "(" + ", ".join(arguments) + ")"
            for name, arguments, _ in MODEL_PROJECTION_PHASES
        ) + ") { goto mismatch; }"
    ))
    positions = [index for index in range(len(body)) if body[index:index + len(guard)] == guard]
    if len(positions) != 1 or len(phases) != 3:
        raise AssertionError("three model passes retain exact caller arguments, order and mismatch exit")
    expanded = []
    for (_, _, bound), phase in zip(MODEL_PROJECTION_PHASES, phases):
        phase = list(tokens(phase))
        success = list(tokens("return true;"))
        if phase[:1 + len(declaration)] != ["{"] + declaration:
            raise AssertionError("model passes own only their original size_t counter")
        if phase[-len(success) - 1:] != success + ["}"]:
            raise AssertionError("model passes retain one terminal helper success")
        inner = phase[1 + len(declaration):-len(success) - 1]
        head = list(tokens("for (index = 0; index < " + bound + "; ++index) {"))
        if inner[:len(head)] != head or "goto" in inner:
            raise AssertionError("each pass retains its original bound and cannot borrow a caller label")
        index = 0
        while index < len(inner):
            if inner[index] == "return":
                if inner[index:index + 3] != ["return", "false", ";"]:
                    raise AssertionError("pass success cannot bypass remaining slots or caller validation")
                expanded.extend(tokens("goto mismatch;"))
                index += 3
            else:
                expanded.append(inner[index])
                index += 1
    start = positions[0]
    body[start:start + len(guard)] = expanded
    body[1:1] = declaration
    return tuple(body)


def stack_conflict_call_guard(original):
    calls = []
    for name, arguments, _ in STACK_CONFLICT_TASKS[original][2]:
        calls.append(name + "(\n" + ",\n".join("            " + arg for arg in arguments) + "\n        )")
    return (
        "    if (\n        !" + "\n        || !".join(calls)
        + "\n    ) {\n        return MICROS_KERNEL_OBJECT_ERROR_STACK;\n    }"
    )


def stack_conflict_body_text(original, caller, phases):
    _, pointer, specification = STACK_CONFLICT_TASKS[original]
    prefix = "\n    struct micros_" + pointer + " *" + pointer + ";\n    enum micros_kernel_object_error error;\n"
    declaration = "    size_t index;\n"
    phase_prefix = "\n" + declaration + "\n"
    success = "\n    return true;\n"
    guard = stack_conflict_call_guard(original)
    if not caller.startswith(prefix) or "index" in tokens(caller):
        raise AssertionError("stack root retains its original locals without a duplicate counter")
    if len(phases) != 2 or caller.count(guard) != 1:
        raise AssertionError("both conflict passes retain ordered checked calls with exact caller arguments")
    loops = []
    for (_, _, bound), phase in zip(specification, phases):
        if not phase.startswith(phase_prefix) or not phase.endswith(success):
            raise AssertionError("conflict passes own only the original counter and terminal success")
        loop = phase[len(phase_prefix):-len(success)]
        head = "    for (index = 0; index < " + bound + "; ++index) {"
        lexical = c_lexical_text(loop)
        if (
            not loop.startswith(head)
            or matching_lexical(lexical, lexical.index("{"), "{", "}") != len(loop) - 1
            or "goto" in tokens(loop)
        ):
            raise AssertionError("one complete original loop retains its bound and independent exit scope")
        edits = []
        for match in re.finditer(r"\breturn\b[^;]*;", lexical):
            if match.group() != "return false;":
                raise AssertionError("conflict passes cannot succeed before finishing the original loop")
            edits.append((match.start(), match.end()))
        for start, end in reversed(edits):
            loop = loop[:start] + "return MICROS_KERNEL_OBJECT_ERROR_STACK;" + loop[end:]
        loops.append(loop)
    caller = caller.replace(guard, "\n".join(loops), 1)
    return prefix + phases[0][1:len(declaration) + 1] + caller[len(prefix):]


def core_body_text(original, record, texts=None):
    if original in STACK_CONFLICT_TASKS and (0, original) not in FUNCTION_TASK_MIGRATION:
        index, _, phases = STACK_CONFLICT_TASKS[original]
        owner = function_records(source(CORE_SOURCES[index]) if texts is None else texts[CORE_SOURCES[index]])
        return stack_conflict_body_text(original, record.body, [owner[name].body for name, _, _ in phases])
    return record.body


def original_body_digest(family, original, record):
    names = normal_names(family)
    body = tokens("{" + (core_body_text(original, record) if family == 0 else record.body) + "}")
    if family == 1 and original == "lifecycle_model_matches_registry" and (1, original) not in FUNCTION_TASK_MIGRATION:
        owner = function_records(source(family_paths(1)[10]))
        body = model_projection_body_tokens(
            "{" + record.body + "}",
            ["{" + owner[name].body + "}" for name, _, _ in MODEL_PROJECTION_PHASES],
        )
    body = tuple(names.get(token, token) for token in body)
    if family == 1 and any(name == "kernel_object_test_registry" for _, name in owned_records(1)):
        body = native_registry_body_tokens(original, body)
    return digest_tokens(body)


def rename_core_identifiers(text):
    lexical = c_lexical_text(text)
    aliases = normal_names(0)
    edits = []
    for match in re.finditer(r"\b[A-Za-z_]\w*\b", lexical):
        if match.group() in aliases and not lexical[:match.start()].rstrip().endswith((".", "->")):
            edits.append((match.start(), match.end(), aliases[match.group()]))
    for start, end, replacement in reversed(edits):
        text = text[:start] + replacement + text[end:]
    return text


def restored_kernel_objects_source(texts=None, private_header=None):
    texts = texts if texts is not None else {
        path: source(path) for path in CORE_SOURCES if (ROOT / path).is_file()
    }
    locations = original_locations(0, texts)
    root = texts[CORE_SOURCES[0]]
    first = next(iter(function_records(root).values()))
    prefix = root[:root.index(first.signature)]
    include = '#include "kernel_object_private.h"'
    if include in prefix:
        if prefix.count(include) != 1:
            raise AssertionError("one exact portable private include replaces the old magic declaration")
        header = source(PRIVATE_HEADERS[0]) if private_header is None else private_header
        definitions = re.findall(
            r"(?m)^#define MICROS_KERNEL_OBJECTS_MAGIC UINT64_C\(0x4d4943524f534f42\)[ \t]*$", header,
        )
        if len(definitions) != 1:
            raise AssertionError("original object magic comes from its verified actual private declaration")
        prefix = prefix.replace(include, definitions[0], 1)
    definitions = []
    for original in BASELINE_BODIES[0]:
        _, record = locations[original]
        signature = rename_core_identifiers(record.signature)
        if original in CORE_PRIVATE_EXPORTS and record.name == private_name(original):
            if tokens(signature)[:1] == ("static",):
                raise AssertionError("unexpected static extraction-only definition")
            signature = "static " + signature
        definitions.append(signature + "{" + rename_core_identifiers(core_body_text(original, record, texts)) + "}")
    restored = prefix + "\n\n".join(definitions) + "\n"
    if digest_bytes(restored.encode()) != BASE_CORE_DIGEST:
        raise AssertionError("complete current-source inverse differs from immutable object source bytes")
    return restored


def source_set_block(name, paths):
    return "set(\n    " + name + "\n" + "".join("    " + path + "\n" for path in paths) + ")"


def alias_rows():
    return tuple(
        (paths[0], (*paths[1:], PRIVATE_HEADERS[family]))
        for family, paths in enumerate(SOURCE_SETS.values())
    )


def kernel_object_cmake_projection(text):
    blocks = [(name, paths, cmake_commands(text, "set", name)) for name, paths in SOURCE_SETS.items()]
    if not any(found for _, _, found in blocks):
        if any("${" + name + "}" in text for name in SOURCE_SETS):
            raise AssertionError("object variables require their complete ordered declarations")
        return text
    for family, (name, paths, found) in enumerate(blocks):
        expected = source_set_block(name, paths)
        if len(found) != 1 or found[0] != expected or text.count(expected + "\n\n") != 1:
            raise AssertionError("wrong complete object source-set insertion")
        text = text.replace(expected + "\n\n", "", 1)
        variable = "${" + name + "}"
        if text.count(variable) != (8, 1, 1)[family]:
            raise AssertionError("wrong exact object source-set consumer count")
        text = text.replace(variable, paths[0])
    return text


def restored_cmake(text):
    restored = kernel_object_cmake_projection(text)
    if digest_bytes(restored.encode()) != BASE_CMAKE_DIGEST:
        raise AssertionError("recipe changed outside three exact sets and ten operands")
    return restored


class KernelObjectSourceLayoutTest(unittest.TestCase):
    def test_portable_registry_storage_has_its_reviewed_state_owner(self):
        spellings = {"storage_is_zero", "kernel_object_storage_is_zero"}
        owners = [
            path for path in CORE_SOURCES
            if (ROOT / path).is_file()
            and spellings & set(c_function_definitions((ROOT / path).read_text()))
        ]
        self.assertEqual(["kernel/kernel_object_state.c"], owners)

    def test_shared_records_preserve_complete_reviewed_asts_and_reexports(self):
        path = ROOT / "tests/host/c_source_records.py"
        self.assertTrue(path.is_file())
        shared = ast.parse(path.read_text())
        definitions = {
            node.name: node for node in shared.body
            if isinstance(node, (ast.FunctionDef, ast.ClassDef))
        }
        self.assertEqual(set(SHARED_INSPECTION_ASTS), set(definitions))
        for name, expected in SHARED_INSPECTION_ASTS.items():
            with self.subTest(name=name):
                current = ast.dump(definitions[name], include_attributes=False)
                self.assertEqual(expected, hashlib.sha256(current.encode()).hexdigest())
        scheduler = ast.parse((ROOT / "tests/host/test_scheduler_source_layout.py").read_text())
        self.assertFalse(any(
            isinstance(node, (ast.FunctionDef, ast.ClassDef)) and node.name in SHARED_INSPECTION_ASTS
            for node in scheduler.body
        ))
        imported = {
            alias.name for node in scheduler.body if isinstance(node, ast.ImportFrom)
            and node.module == "tests.host.c_source_records" for alias in node.names
        }
        self.assertEqual(set(SHARED_INSPECTION_ASTS), imported)

    def test_immutable_inventories_and_explicit_pending_work_are_exact(self):
        self.assertEqual((48, 27, 1), tuple(len(items) for items in BASELINE_BODIES.values()))
        self.assertEqual(72, len(INITIAL_SYMBOL_OWNER_MIGRATION))
        self.assertEqual(8, len(LONG_FUNCTIONS))
        self.assertEqual(4, len(SHORTER_TASKS))
        self.assertEqual(12, len(REQUIRED_TASKS))
        self.assertEqual(22, len(CORE_PRIVATE_EXPORTS))
        for active, initial in (
            (SYMBOL_OWNER_MIGRATION, INITIAL_SYMBOL_OWNER_MIGRATION),
            (FUNCTION_TASK_MIGRATION, REQUIRED_TASKS),
        ):
            self.assertIsInstance(active, tuple)
            self.assertEqual(len(active), len(set(active)))
            self.assertLessEqual(set(active), set(initial))
        for family, groups in ORIGINAL_FUNCTIONS.items():
            flattened = tuple(name for names in groups.values() for name in names)
            self.assertEqual(len(flattened), len(set(flattened)))
            self.assertEqual(set(BASELINE_BODIES[family]), set(flattened))

    def test_every_original_has_its_exact_current_and_final_owner(self):
        for family, groups in ORIGINAL_FUNCTIONS.items():
            locations = original_locations(family)
            for owner, names in groups.items():
                for name in names:
                    with self.subTest(family=family, name=name):
                        current = 0 if (family, name) in SYMBOL_OWNER_MIGRATION else owner
                        self.assertEqual(family_paths(family)[current], locations[name][0])

    def test_complete_original_bodies_and_all_parameter_types_remain_exact(self):
        for family in ORIGINAL_FUNCTIONS:
            locations = original_locations(family)
            signatures = []
            for name, expected in BASELINE_BODIES[family].items():
                with self.subTest(family=family, name=name):
                    _, record = locations[name]
                    self.assertEqual(expected, original_body_digest(family, name, record), REVIEWED_BASE)
                    signatures.append((name, original_signature_tokens(family, name, record)))
            self.assertEqual(BASELINE_SIGNATURES[family], digest_tokens(signatures))

    def test_only_original_long_task_migrations_exceed_the_signal(self):
        actual = {
            (family, normal_names(family).get(name, name))
            for family in ORIGINAL_FUNCTIONS for (_, name), record in owned_records(family).items()
            if record.lines > 120
        }
        self.assertEqual(set(LONG_FUNCTIONS) & set(FUNCTION_TASK_MIGRATION), actual)

    def test_portable_complete_byte_inverse_is_independent_and_rejects_changed_code(self):
        current = {path: source(path) for path in CORE_SOURCES if (ROOT / path).is_file()}
        restored = restored_kernel_objects_source(current)
        self.assertEqual(BASE_CORE_DIGEST, digest_bytes(restored.encode()))
        path, record = original_locations(0, current)["require_initialized"]
        changed = current[path].replace(
            "objects->initialization_magic != MICROS_KERNEL_OBJECTS_MAGIC",
            "objects->initialization_magic == MICROS_KERNEL_OBJECTS_MAGIC",
        )
        self.assertNotEqual(current[path], changed)
        with self.assertRaises(AssertionError):
            restored_kernel_objects_source({**current, path: changed})
        path, _ = original_locations(0, current)["micros_process_create_at"]
        changed = current[path].replace("handle->generation = generation;", "handle->generation = 0;", 1)
        self.assertNotEqual(current[path], changed)
        with self.assertRaises(AssertionError):
            restored_kernel_objects_source({**current, path: changed})
        if '#include "kernel_object_private.h"' in current[CORE_SOURCES[0]]:
            with self.assertRaises(AssertionError):
                restored_kernel_objects_source(current, source(PRIVATE_HEADERS[0]).replace("0x4d4943524f534f42", "0x4d4943524f534f43"))

    def test_literal_aware_namespace_inverse_does_not_rewrite_strings_comments_or_members(self):
        current = (
            'kernel_object_require_initialized(objects); "kernel_object_require_initialized"; '
            "/* kernel_object_require_initialized */ other.kernel_object_require_initialized;"
        )
        expected = (
            'require_initialized(objects); "kernel_object_require_initialized"; '
            "/* kernel_object_require_initialized */ other.kernel_object_require_initialized;"
        )
        self.assertEqual(expected, rename_core_identifiers(current))

    def test_all_real_sources_headers_consumers_and_complete_recipe_are_exact(self):
        self.assertEqual(23, sum(map(len, SOURCE_SETS.values())))
        self.assertEqual(23, len({path for paths in SOURCE_SETS.values() for path in paths}))
        cmake = source("CMakeLists.txt")
        for family, (name, paths) in enumerate(SOURCE_SETS.items()):
            self.assertEqual((source_set_block(name, paths),), cmake_commands(cmake, "set", name))
            for path in (*paths, PRIVATE_HEADERS[family]):
                self.assertTrue((ROOT / path).is_file(), path)
            self.assertEqual((8, 1, 1)[family], cmake.count("${" + name + "}"))
        core, host, target = ("${" + name + "}" for name in SOURCE_SETS)
        for consumer in (
            "kernel_objects_test", "frame_ownership_test", "scheduler_test", "endpoint_test",
            "ipc_model_test", "bootstrap_control_test", "pm_test", "micros",
        ):
            blocks = cmake_commands(cmake, "add_executable", consumer)
            self.assertEqual(1, len(blocks))
            self.assertEqual(1, blocks[0].count(core))
            self.assertEqual(int(consumer == "kernel_objects_test"), blocks[0].count(host))
        guards = re.findall(r"if\(MICROS_BUILD_OBJECT_MODEL_TEST\)([\s\S]*?)\nendif\(\)", cmake)
        self.assertEqual(1, sum(target in guard for guard in guards))
        self.assertEqual(BASE_CMAKE_DIGEST, digest_bytes(restored_cmake(cmake).encode()))
        self.assertEqual(digest_bytes(cmake.encode()), validation_context.RECIPE_DIGESTS["CMakeLists.txt"])

    def test_portable_private_helpers_are_necessary_and_dependencies_stay_acyclic(self):
        current = owned_records(0)
        owners = {name: path for path, name in current}
        aliases = normal_names(0)
        final = {
            name: CORE_SOURCES[owner] for owner, names in ORIGINAL_FUNCTIONS[0].items() for name in names
        }
        final.update({
            name: CORE_SOURCES[owner] for owner, _, phases in STACK_CONFLICT_TASKS.values() for name, _, _ in phases
        })
        edges = set()
        projected = set()
        exports = set()
        for (path, spelling), record in current.items():
            caller = aliases.get(spelling, spelling)
            referenced = set(re.findall(r"\b[A-Za-z_]\w*\b", c_lexical_text(record.body)))
            for callee in referenced & set(owners):
                dependency = owners[callee]
                normalized = aliases.get(callee, callee)
                if dependency != path:
                    edges.add((dependency, path))
                    if not callee.startswith("micros_"):
                        exports.add(callee)
                source_owner = CORE_SOURCES.index(final[caller])
                dependency_owner = CORE_SOURCES.index(final[normalized])
                if source_owner != dependency_owner:
                    self.assertIn(dependency_owner, CORE_DAG[source_owner], (caller, normalized))
                    projected.add((final[normalized], final[caller]))
        self.assertEqual(set(CORE_SOURCES), set(topological_order(set(CORE_SOURCES), edges)))
        self.assertEqual(set(CORE_SOURCES), set(topological_order(set(CORE_SOURCES), projected)))
        header = source(PRIVATE_HEADERS[0])
        declarations = re.findall(r"\b(kernel_object_\w+)\s*\([^;{}]*\)\s*;", c_lexical_text(header))
        self.assertEqual(len(declarations), len(set(declarations)))
        self.assertEqual(exports, set(declarations))
        self.assertLessEqual(exports, {private_name(name) for name in CORE_PRIVATE_EXPORTS})
        self.assertEqual({}, c_function_definitions(header))
        for path in CORE_SOURCES:
            self.assertEqual((), file_scope_objects(source(path)))

    def test_public_runtime_architecture_driver_and_neighbor_artifacts_are_byte_exact(self):
        self.assertEqual(18, len(STABLE_HASHES))
        for path, expected in STABLE_HASHES.items():
            with self.subTest(path=path):
                self.assertEqual(expected, digest_bytes((ROOT / path).read_bytes()))

    def test_all_four_classification_dimensions_and_two_host_only_inputs_are_exact(self):
        qemu = [
            "test-qemu-object-model", "test-qemu-endpoint", "test-qemu-grant-syscall",
            "test-qemu-user-runtime", "test-qemu-bootstrap-launcher", "test-qemu-vm-handoff",
            "test-qemu-pm-service", "test-qemu-vm-scratch-preemption", "test-qemu-tty",
            "test-qemu-ramfs", "test-qemu-vfs", "test-qemu-elf-spawn", "test-qemu-vm-ready-early",
            "test-qemu-vm-self-fault", "test-qemu-vm-self-fault-sealed",
            "test-qemu-bootstrap-ready-timeout", "test-qemu-bootstrap-manifest-panic",
            "test-qemu-ipc", "test-qemu-ipc-syscall", "test-qemu-ipc-syscall-panic",
            "test-qemu-frame-ownership", "test-qemu-address-space-handoff", "test-qemu-scheduler",
        ]
        expected = (
            (False, qemu, ["test-ipc-model"], []),
            (True, [], [], []),
            (False, ["test-qemu-object-model"], [], []),
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
        for path in (STRUCTURAL_TEST_PATH, SHARED_INSPECTION_PATH):
            self.assertEqual(1, validation_plan.HOST_UNIT_ONLY_INPUTS.count(path))
            self.assertEqual(
                [validation_plan.UNIT_FAST, *validation_plan.diff_commands("main")],
                validation_plan.plan([path], "fast", "main"),
            )

    def test_complete_recipe_inverse_rejects_unknown_sets_members_operands_and_other_bytes(self):
        text = source("CMakeLists.txt")
        restored = restored_cmake(text)
        self.assertEqual(BASE_CMAKE_DIGEST, digest_bytes(restored.encode()))
        for changed in (
            text.replace("kernel/kernel_object_state.c", "kernel/unknown_object_state.c", 1),
            text.replace("${MICROS_KERNEL_OBJECT_CORE_SOURCES}", "kernel/kernel_objects.c", 1),
            text + "\nconsumer(${MICROS_KERNEL_OBJECT_HOST_TEST_SOURCES})\n",
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                kernel_object_cmake_projection(changed)
        with self.assertRaises(AssertionError):
            restored_cmake(text + "\n# unreviewed recipe byte\n")

    def test_host_fixture_owns_one_original_registry_and_a_nonresetting_getter(self):
        path = family_paths(1)[1]
        text = source(path)
        records = function_records(text)
        self.assertIn("kernel_object_test_registry", records)
        self.assertEqual(1, text.count("static struct micros_kernel_objects objects;"))
        self.assertEqual(
            tokens("{ return &objects; }"),
            tokens("{" + records["kernel_object_test_registry"].body + "}"),
        )
        self.assertEqual(
            tokens("struct micros_kernel_objects *kernel_object_test_registry(void)"),
            tokens(records["kernel_object_test_registry"].signature),
        )
        for owner in family_paths(1):
            expected = ("static struct micros_kernel_objects objects;",) if owner == path else ()
            self.assertEqual(tuple(map(tokens, expected)), tuple(map(tokens, file_scope_objects(source(owner)))))
            self.assertNotRegex(source(owner), r'#include\s*"[^"]*kernel_object_private\.h"')
        completed = (1, "lifecycle_model_matches_registry") not in FUNCTION_TASK_MIGRATION
        self.assertEqual(28 + 3 * completed, len(owned_records(1)))

    def test_registry_transport_retains_whole_snapshot_reset_extent_and_member_scope(self):
        current = (
            "{ struct micros_kernel_objects *fixture = kernel_object_test_registry(); "
            "struct micros_kernel_objects snapshot = *fixture; reset_objects(fixture); "
            "candidate = &fixture->processes[index]; compare(fixture, sizeof(*fixture)); "
            "if (!lifecycle_model_matches_registry(fixture, &model, step)) { return false; } return true; }"
        )
        expected = (
            "{ struct micros_kernel_objects snapshot = objects; reset_objects(); "
            "candidate = &objects.processes[index]; compare(&objects, sizeof(objects)); "
            "if (!lifecycle_model_matches_registry(&model, step)) { return false; } return true; }"
        )
        self.assertEqual(tokens(expected), native_registry_body_tokens("test_example", tokens(current)))
        for changed in (
            current.replace("fixture->processes", "fixture->unknown"),
            current.replace("fixture->processes", "other.fixture->processes"),
            current.replace("reset_objects(fixture)", "reset_objects(other)"),
            current.replace("lifecycle_model_matches_registry(fixture,", "lifecycle_model_matches_registry(other,"),
            current.replace("struct micros_kernel_objects *fixture", "static struct micros_kernel_objects *fixture"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                native_registry_body_tokens("test_example", tokens(changed))
        for changed in (
            current.replace("sizeof(*fixture)", "sizeof(fixture)"),
            current.replace("return false;", "return true;"),
            current.replace("candidate =", "reset_objects(fixture); candidate ="),
        ):
            with self.subTest(changed=changed):
                self.assertNotEqual(tokens(expected), native_registry_body_tokens("test_example", tokens(changed)))

    def test_original_registry_reset_and_macro_snapshot_are_not_new_bundle_copies(self):
        current = (
            "{ struct micros_kernel_objects expected_objects = *fixture; "
            "enum error actual_error = (expression); "
            "compare(fixture, &expected_objects, sizeof(*fixture)); return false; }"
        )
        expected = (
            "{ struct micros_kernel_objects expected_objects = objects; "
            "enum error actual_error = (expression); "
            "compare(&objects, &expected_objects, sizeof(objects)); return false; }"
        )
        self.assertEqual(tokens(expected), restore_registry_tokens(tokens(current)))
        self.assertEqual(
            tokens("{ memset(&objects, 0, sizeof(objects)); }"),
            restore_registry_tokens(tokens("{ memset(fixture, 0, sizeof(*fixture)); }")),
        )
        for changed in (
            current.replace("*fixture", "*other.fixture"),
            current.replace("compare(fixture,", "compare(&fixture,"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                restore_registry_tokens(tokens(changed))

    def test_host_macros_model_types_and_automatic_case_table_are_actual_original_data(self):
        header = source(PRIVATE_HEADERS[1])
        start = header.index("#define EXPECT_TRUE")
        model_start = header.index("struct lifecycle_model_process {")
        macro = restore_registry_tokens(tokens(header[start:model_start]))
        self.assertEqual(HOST_MACROS_DIGEST, digest_tokens(macro))
        end = header.index("\n};", header.index("struct lifecycle_model {")) + len("\n};")
        self.assertEqual(MODEL_TYPES_DIGEST, digest_tokens(tokens(header[model_start:end])))
        main = function_records(source(family_paths(1)[0]))["main"].body
        table = main[:main.index("    size_t index;")].strip()
        self.assertNotIn("static", tokens(table))
        aliases = normal_names(1)
        self.assertEqual(
            HOST_CASE_TABLE_DIGEST,
            digest_tokens(aliases.get(token, token) for token in tokens(table)),
        )
        self.assertEqual(15, len(re.findall(r'\{\s*"[^"\n]+"', table)))
        for original, (path, record) in original_locations(1).items():
            count = record.body.count("kernel_object_test_registry()")
            self.assertEqual(int(original.startswith("test_")), count, original)
        self.assertEqual({}, c_function_definitions(header))

    def test_host_private_edges_include_callbacks_and_keep_reference_independent(self):
        paths = family_paths(1)
        records = owned_records(1)
        owners = {name: path for path, name in records}
        self.assertEqual(len(records), len(owners))
        allowed = {
            0: {2, 3, 4, 5, 6, 7, 8}, 1: set(), 2: {1}, 3: {1}, 4: {1},
            5: {1}, 6: {1}, 7: {1}, 8: {1, 9, 10, 11}, 9: set(), 10: {1, 9}, 11: {1, 9},
        }
        edges = set()
        exports = set()
        for (path, name), record in records.items():
            referenced = set(re.findall(r"\b[A-Za-z_]\w*\b", c_lexical_text(record.body)))
            for callee in referenced & set(owners):
                dependency = owners[callee]
                if dependency != path:
                    self.assertIn(paths.index(dependency), allowed[paths.index(path)], (name, callee))
                    edges.add((dependency, path))
                    exports.add(callee)
        self.assertEqual(set(paths), set(topological_order(set(paths), edges)))
        declarations = re.findall(
            r"\b(kernel_object_test_\w+)\s*\([^;{}]*\)\s*;", c_lexical_text(source(PRIVATE_HEADERS[1])),
        )
        self.assertEqual(len(declarations), len(set(declarations)))
        self.assertEqual(exports, set(declarations))

    def test_model_projection_exposes_three_bounded_typed_passes(self):
        records = function_records(source(family_paths(1)[10]))
        for name, arguments, _ in MODEL_PROJECTION_PHASES:
            with self.subTest(phase=name):
                self.assertIn(name, records)
                parameters = "const struct micros_kernel_objects *fixture"
                if arguments == ("fixture", "model"):
                    parameters += ", const struct lifecycle_model *model"
                self.assertEqual(
                    tokens("static bool " + name + "(" + parameters + ")"),
                    tokens(records[name].signature),
                )
                self.assertLessEqual(records[name].lines, 120)
        self.assertLessEqual(records[host_name("lifecycle_model_matches_registry")].lines, 120)

    def test_model_projection_preserves_loop_bounds_first_mismatch_and_final_validation(self):
        calls = " || !".join(
            name + "(" + ", ".join(arguments) + ")" for name, arguments, _ in MODEL_PROJECTION_PHASES
        )
        caller = (
            "{ if (bad_header()) { goto mismatch; } if (!" + calls + ") { goto mismatch; } "
            "if (bad_complete_validation()) { goto mismatch; } return true; "
            'mismatch: report("original mismatch"); return false; }'
        )
        phases = tuple(
            "{ size_t index; for (index = 0; index < " + bound
            + "; ++index) { if (bad_" + label + "(index)) { return false; } } return true; }"
            for label, (_, _, bound) in zip(("process", "thread", "hart"), MODEL_PROJECTION_PHASES)
        )
        expected = (
            "{ size_t index; if (bad_header()) { goto mismatch; } "
            "for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) { if (bad_process(index)) { goto mismatch; } } "
            "for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) { if (bad_thread(index)) { goto mismatch; } } "
            "for (index = 0; index < MICROS_HART_CAPACITY; ++index) { if (bad_hart(index)) { goto mismatch; } } "
            "if (bad_complete_validation()) { goto mismatch; } return true; "
            'mismatch: report("original mismatch"); return false; }'
        )
        self.assertEqual(tokens(expected), model_projection_body_tokens(caller, phases))
        for changed in (
            caller.replace("(fixture, model)", "(fixture, other)", 1),
            caller.replace("|| !kernel_object_test_model_threads_match", "&& !kernel_object_test_model_threads_match"),
            caller.replace(") { goto mismatch; } if (bad_complete", ") { return true; } if (bad_complete"),
            caller.replace("{ if (bad_header", "{ size_t index; if (bad_header", 1),
        ):
            self.assertNotEqual(caller, changed)
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                model_projection_body_tokens(changed, phases)
        for changed in (
            phases[0].replace("return false;", "return true;"),
            phases[0].replace("index = 0", "index = 1"),
            phases[0].replace("MICROS_PROCESS_CAPACITY", "MICROS_THREAD_CAPACITY"),
            phases[0].replace("return false;", "goto mismatch;"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                model_projection_body_tokens(caller, (changed, *phases[1:]))
        self.assertNotEqual(
            tokens(expected),
            model_projection_body_tokens(caller.replace("bad_complete_validation", "weakened_validation"), phases),
        )

    def test_hart_stack_installation_exposes_two_typed_conflict_passes_before_publication(self):
        records = function_records(source(CORE_SOURCES[7]))
        for name, arguments, _ in HART_STACK_PHASES:
            with self.subTest(phase=name):
                self.assertIn(name, records)
                parameters = "const struct micros_kernel_objects *objects"
                if "hart" in arguments:
                    parameters += ", const struct micros_hart *hart"
                parameters += (
                    ", uintptr_t primary_stack_bottom, uintptr_t primary_stack_top, "
                    "uintptr_t emergency_stack_bottom, uintptr_t emergency_stack_top"
                )
                self.assertEqual(
                    tokens("static bool " + name + "(" + parameters + ")"),
                    tokens(records[name].signature),
                )
                self.assertLessEqual(records[name].lines, 120)
        self.assertLessEqual(records["micros_hart_install_trap_stacks"].lines, 120)

    def test_hart_stack_inverse_preserves_actual_bytes_counter_bounds_and_publication(self):
        def restore(caller, phases):
            return stack_conflict_body_text("micros_hart_install_trap_stacks", caller, phases)

        caller = """
    struct micros_hart *hart;
    enum micros_kernel_object_error error;

    admit();
    if (
        !kernel_object_hart_stacks_avoid_harts(
            objects,
            hart,
            primary_stack_bottom,
            primary_stack_top,
            emergency_stack_bottom,
            emergency_stack_top
        )
        || !kernel_object_hart_stacks_avoid_threads(
            objects,
            primary_stack_bottom,
            primary_stack_top,
            emergency_stack_bottom,
            emergency_stack_top
        )
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STACK;
    }

    publish();
"""
        phases = (
            "\n    size_t index;\n\n"
            "    for (index = 0; index < MICROS_HART_CAPACITY; ++index) {\n"
            "        if (bad_hart(index)) {\n            return false;\n        }\n    }"
            "\n    return true;\n",
            "\n    size_t index;\n\n"
            "    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {\n"
            "        if (bad_thread(index)) {\n            return false;\n        }\n    }"
            "\n    return true;\n",
        )
        expected = """
    struct micros_hart *hart;
    enum micros_kernel_object_error error;
    size_t index;

    admit();
    for (index = 0; index < MICROS_HART_CAPACITY; ++index) {
        if (bad_hart(index)) {
            return MICROS_KERNEL_OBJECT_ERROR_STACK;
        }
    }
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        if (bad_thread(index)) {
            return MICROS_KERNEL_OBJECT_ERROR_STACK;
        }
    }

    publish();
"""
        self.assertEqual(expected, restore(caller, phases))
        for changed in (
            caller.replace("            hart,", "            other,"),
            caller.replace("|| !kernel_object_hart_stacks_avoid_threads", "&& !kernel_object_hart_stacks_avoid_threads"),
            caller.replace("return MICROS_KERNEL_OBJECT_ERROR_STACK;", "return MICROS_KERNEL_OBJECT_OK;"),
            caller.replace("    admit();", "    size_t index;\n    admit();"),
        ):
            self.assertNotEqual(caller, changed)
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                restore(changed, phases)
        for changed in (
            phases[0].replace("size_t index", "uint16_t index"),
            phases[0].replace("index = 0", "index = 1"),
            phases[0].replace("MICROS_HART_CAPACITY", "MICROS_THREAD_CAPACITY"),
            phases[0].replace("return false;", "return true;"),
            phases[0].replace("return false;", "goto done;"),
            phases[0].replace("    return true;", "    unchecked();\n    return true;"),
        ):
            self.assertNotEqual(phases[0], changed)
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                restore(caller, (changed, phases[1]))
        self.assertNotEqual(expected, restore(caller.replace("publish();", "publish_early();"), phases))
        self.assertNotEqual(
            expected, restore(caller, (phases[0].replace("bad_hart", "weakened"), phases[1])),
        )
        self.assertNotEqual(
            expected, restore(caller, (phases[0].replace("        if (", "         if ("), phases[1])),
        )

    def test_context_attach_exposes_typed_peer_and_hart_conflicts_before_copy(self):
        records = function_records(source(CORE_SOURCES[6]))
        for name, arguments, _ in CONTEXT_STACK_PHASES:
            with self.subTest(phase=name):
                self.assertIn(name, set(records))
                parameters = "const struct micros_kernel_objects *objects"
                if "thread" in arguments:
                    parameters += ", const struct micros_thread *thread"
                parameters += ", uintptr_t kernel_stack_bottom, uintptr_t kernel_stack_top"
                self.assertEqual(
                    tokens("static bool " + name + "(" + parameters + ")"),
                    tokens(records[name].signature),
                )
        self.assertLess(records["micros_thread_attach_execution_context"].lines, 99)

    def test_context_stack_inverse_preserves_raw_peer_order_and_context_copy(self):
        caller = """
    struct micros_thread *thread;
    enum micros_kernel_object_error error;

    original_admission_and_geometry();
    if (
        !kernel_object_context_stack_avoids_threads(
            objects,
            thread,
            kernel_stack_bottom,
            kernel_stack_top
        )
        || !kernel_object_context_stack_avoids_harts(
            objects,
            kernel_stack_bottom,
            kernel_stack_top
        )
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STACK;
    }

    original_context_copy();
"""
        phases = (
            "\n    size_t index;\n\n"
            "    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {\n"
            "        if (bad_peer(index)) {\n            return false;\n        }\n    }"
            "\n    return true;\n",
            "\n    size_t index;\n\n"
            "    for (index = 0; index < MICROS_HART_CAPACITY; ++index) {\n"
            "        if (bad_hart(index)) {\n            return false;\n        }\n    }"
            "\n    return true;\n",
        )
        expected = """
    struct micros_thread *thread;
    enum micros_kernel_object_error error;
    size_t index;

    original_admission_and_geometry();
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        if (bad_peer(index)) {
            return MICROS_KERNEL_OBJECT_ERROR_STACK;
        }
    }
    for (index = 0; index < MICROS_HART_CAPACITY; ++index) {
        if (bad_hart(index)) {
            return MICROS_KERNEL_OBJECT_ERROR_STACK;
        }
    }

    original_context_copy();
"""
        original = "micros_thread_attach_execution_context"
        self.assertEqual(expected, stack_conflict_body_text(original, caller, phases))
        for changed in (
            caller.replace("            thread,", "            other,"),
            caller.replace("kernel_stack_bottom,\n", "kernel_stack_top,\n", 1),
            caller.replace("|| !kernel_object_context_stack_avoids_harts", "&& !kernel_object_context_stack_avoids_harts"),
        ):
            self.assertNotEqual(caller, changed)
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                stack_conflict_body_text(original, changed, phases)
        for changed in (
            phases[0].replace("MICROS_THREAD_CAPACITY", "MICROS_HART_CAPACITY"),
            phases[0].replace("return false;", "return true;"),
            phases[0].replace("size_t index;", "unsigned int index;"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                stack_conflict_body_text(original, caller, (changed, phases[1]))
        self.assertNotEqual(
            expected, stack_conflict_body_text(original, caller.replace("original_context_copy", "new_copy"), phases),
        )


if __name__ == "__main__":
    unittest.main()

"""Bounded source-contract checks for Accepted ADR-0069."""

import ast
import hashlib
import json
from pathlib import Path
import re
import unittest

from tests.host.c_source_records import function_records, matching_lexical
from tests.host.test_frame_ownership_source_layout import c_function_definitions, c_lexical_text, tokens
from tests.host.test_pm_service_source_layout import cmake_commands
from tools import validation_context, validation_plan


ROOT = Path(__file__).resolve().parents[2]
REVIEWED_BASE = "6269d9247de6beafed2448aa0ffd6904de5e8833"
BASE_CMAKE_DIGEST = "44d8a8615e5465ad199ed916b30e72af05643ef00ae5ac9a7df00a0dc4cad2c1"
STRUCTURAL_TEST_PATH = "tests/host/test_grant_data_plane_source_layout.py"
COPY_SOURCES = ("kernel/grant_copy.c", "kernel/grant_copy_range.c")
SYSCALL_TEST_SOURCES = (
    "kernel/grant_syscall_test.c",
    "kernel/grant_syscall_test_fixture.c",
    "kernel/grant_syscall_test_commands.c",
    "kernel/grant_syscall_test_copy_commands.c",
    "kernel/grant_syscall_test_transitions.c",
    "kernel/grant_syscall_test_effects.c",
    "kernel/grant_syscall_test_hooks.c",
    "kernel/grant_syscall_test_lifecycle.c",
)
HOST_SOURCES = (
    "tests/host/grant_test.c",
    "tests/host/grant_test_fixture.c",
    "tests/host/grant_lifecycle_test.c",
    "tests/host/grant_validation_test.c",
    "tests/host/grant_model_test.c",
    "tests/host/grant_model_reference.c",
    "tests/host/grant_model_operations.c",
    "tests/host/grant_model_copy.c",
)
FAMILY_PATHS = (
    COPY_SOURCES, ("kernel/grant_syscall.c",), ("kernel/grant_test.c",),
    SYSCALL_TEST_SOURCES, HOST_SOURCES,
    ("tests/host/grant_copy_test.c",), ("tests/host/grant_syscall_core_test.c",),
)
SOURCE_SETS = {
    "MICROS_GRANT_COPY_TARGET_SOURCES": COPY_SOURCES,
    "MICROS_GRANT_HOST_TEST_SOURCES": HOST_SOURCES,
    "MICROS_GRANT_SYSCALL_QEMU_TEST_SOURCES": SYSCALL_TEST_SOURCES,
}
PRIVATE_HEADERS = (
    "kernel/grant_copy_internal.h",
    "tests/host/grant_test_internal.h",
    "kernel/grant_syscall_test_internal.h",
)
COPY_EXECUTION_TAG = "grant_copy_execution"
COPY_EXECUTION_PHASE = "grant_copy_prepare_participant_plans"
COPY_LOCAL_RANGE_PHASE = "grant_copy_local_range_is_valid"
RANGE_DEVICE_PHASE = "grant_copy_plan_device_span"
RANGE_DEVICE_PARAMETERS = (
    "struct micros_process_handle process, const struct micros_tty_device_authority *device_authority, "
    "uintptr_t *current, size_t *remaining, bool *device_fault, "
    "enum micros_user_address_space_error *address_error, size_t *chunk_length"
)
VALIDATION_WORK_TAG = "grant_copy_validation"
VALIDATION_PHASE = "grant_copy_prepare_remote_plan"
VALIDATION_FIELDS = (
    ("const struct micros_frame_ownership *ledger;", False),
    ("struct micros_grant_copy_authority authority;", False),
    ("struct micros_grant_copy_range_plan remote_plan;", True),
    ("struct micros_tty_device_authority device_authority;", False),
    ("enum micros_grant_error error;", False),
    ("enum micros_tty_device_authority_status device_status;", False),
)
VALIDATION_PARAMETERS = (
    "struct grant_copy_validation *work, const struct micros_endpoint_registry *endpoint_registry, "
    "const struct micros_kernel_objects *objects, size_t length, uint32_t required_permission"
)
SYSCALL_WORK_TAG = "grant_syscall_execution"
SYSCALL_DISPATCH_PHASE = "grant_syscall_execute_request"
SYSCALL_CREATE_PHASE = "grant_syscall_create_request"
SYSCALL_PHASE_PARAMETERS = (
    "struct micros_hart *hart, struct micros_trap_frame *frame, const struct micros_syscall_context *context, "
    "struct grant_syscall_execution *work"
)
SYSCALL_FIELDS = (
    ("struct micros_grant_registry *grant_registry;", False),
    ("struct micros_endpoint_registry *endpoint_registry;", False),
    ("struct micros_grant_create_plan create_plan;", False),
    ("struct micros_grant_syscall_request request;", False),
    ("struct micros_tty_device_authority device_authority;", False),
    ("micros_grant_t created;", "value"),
    ("enum micros_syscall_abi_result decode_result;", False),
    ("enum micros_grant_error error;", False),
    ("enum micros_tty_device_authority_status device_status;", False),
    ("uint64_t abi_result;", False),
)
COPY_EXECUTION_FIELDS = (
    ("const struct micros_frame_ownership *ledger;", False),
    ("struct micros_grant_copy_authority authority;", False),
    ("struct micros_grant_copy_range_plan remote_plan;", True),
    ("struct micros_grant_copy_range_plan local_plan;", True),
    ("struct micros_tty_device_authority device_authority;", False),
    ("const struct micros_grant_copy_range_plan *source_plan;", False),
    ("const struct micros_grant_copy_range_plan *destination_plan;", False),
    ("enum micros_grant_error remote_error;", False),
    ("enum micros_grant_error local_error;", False),
    ("enum micros_grant_error error;", False),
    ("enum micros_tty_device_authority_status device_status;", False),
)
ORIGINAL_FUNCTIONS = {
    0: {
        0: "copy_grant micros_grant_validate_range micros_grant_copy_from micros_grant_copy_to".split(),
        1: "map_address_error plan_range".split(),
    },
    1: {0: "panic_grant_syscall micros_grant_handle_captured_user_ecall".split()},
    2: {0: (
        "copy_storage storage_matches snapshot_failure_state failure_state_matches fill_bytes "
        "physical_for write_pattern pattern_matches range_is_value micros_grant_runtime_run_self_test"
    ).split()},
    3: {
        0: "micros_grant_syscall_runtime_run_self_test micros_grant_syscall_test_finish".split(),
        1: (
            "copy_bytes bytes_equal fill_bytes read_satp user_address_of expected_satp abi_result "
            "physical_for write_pattern pattern_matches range_is_value fill_context_pattern current_actor "
            "test_mismatch normalize_objects capture_page_tables snapshot_user_bytes user_bytes_match "
            "snapshot_failure_state failure_state_matches scheduler_state_valid arm_raw arm_control"
        ).split(),
        2: ["arm_command"],
        3: [],
        4: "schedule_next schedule_reuse schedule_finish transition_after_return".split(),
        5: ["validate_command_effect"],
        6: (
            "micros_grant_syscall_test_before_ecall micros_grant_syscall_test_after_dispatch "
            "micros_grant_syscall_test_after_return micros_grant_syscall_test_handle_control_trap"
        ).split(),
        7: (
            "release_address_space release_thread close_endpoint reuse_grantor prepare_process "
            "endpoint_registry_is_empty scheduler_is_at_idle_baseline"
        ).split(),
    },
    4: {
        0: ["micros_grant_test_run"],
        1: "setup_fixture recreate_process expect_create_failure_unchanged".split(),
        2: (
            "context_pattern test_token_boundaries test_create_inspect_revoke_and_reuse "
            "test_create_failures_and_capacity test_terminal_generation_quarantines "
            "test_endpoint_cancellation_and_reuse test_failed_ipc_close_preserves_cancel_plan"
        ).split(),
        3: "test_error_precedence_and_output_preservation test_validator_rejects_corruption".split(),
        4: "test_grant_model_boundaries test_grant_model micros_grant_model_test_run".split(),
        5: (
            "process_handles_equal records_equal registries_equal model_next model_operation_name "
            "record_model_trace model_fail cancel_plan_matches_reference"
        ).split(),
        6: [],
        7: [],
    },
    5: {0: (
        "process_handles_equal authority_equal setup_fixture create_grant expect_authority_failure_unchanged "
        "test_copy_authority_and_bounds test_copy_plan_and_commit micros_grant_copy_test_run"
    ).split()},
    6: {0: "request_matches expect_decode_failure micros_grant_syscall_core_test_run".split()},
}
LONG_FUNCTIONS = (
    (0, "copy_grant"),
    (1, "micros_grant_handle_captured_user_ecall"),
    (2, "micros_grant_runtime_run_self_test"),
    (3, "arm_command"),
    (3, "transition_after_return"),
    (3, "validate_command_effect"),
    (3, "micros_grant_syscall_runtime_run_self_test"),
    (4, "test_create_inspect_revoke_and_reuse"),
    (4, "test_endpoint_cancellation_and_reuse"),
    (4, "test_error_precedence_and_output_preservation"),
    (4, "test_grant_model_boundaries"),
    (4, "test_grant_model"),
    (5, "test_copy_authority_and_bounds"),
    (6, "micros_grant_syscall_core_test_run"),
)
SHORTER_TASKS = (
    (0, "plan_range"), (0, "micros_grant_validate_range"),
    (5, "test_copy_plan_and_commit"),
    (3, "micros_grant_syscall_test_handle_control_trap"),
    (3, "micros_grant_syscall_test_finish"),
)
REQUIRED_TASKS = (*LONG_FUNCTIONS, *SHORTER_TASKS)
INITIAL_SYMBOL_OWNER_MIGRATION = tuple(
    (family, name) for family, owners in ORIGINAL_FUNCTIONS.items()
    for owner, names in owners.items() if owner != 0 for name in names
)
SYMBOL_OWNER_MIGRATION = tuple(
    item for item in INITIAL_SYMBOL_OWNER_MIGRATION
    if item not in ((0, "map_address_error"), (0, "plan_range"))
)
FUNCTION_TASK_MIGRATION = (
    (2, "micros_grant_runtime_run_self_test"),
    (3, "arm_command"),
    (3, "transition_after_return"),
    (3, "validate_command_effect"),
    (3, "micros_grant_syscall_runtime_run_self_test"),
    (4, "test_create_inspect_revoke_and_reuse"),
    (4, "test_endpoint_cancellation_and_reuse"),
    (4, "test_error_precedence_and_output_preservation"),
    (4, "test_grant_model_boundaries"),
    (4, "test_grant_model"),
    (5, "test_copy_authority_and_bounds"),
    (6, "micros_grant_syscall_core_test_run"),
    (5, "test_copy_plan_and_commit"),
    (3, "micros_grant_syscall_test_handle_control_trap"),
    (3, "micros_grant_syscall_test_finish"),
)

# Immutable oracle literals are inserted from the offline reviewed Git manifest.
BASELINE_BODIES = {0: {'copy_grant': '1c7df8ef2d611dc882f8e0617dcc03afafe45bc344edab5b32e386bf973172af',
     'map_address_error': '05b7818b280c6fb9eef074137eb5efd8bae8db18e50e2a5439862d486e44a356',
     'micros_grant_copy_from': 'bb7569b02157a044462fdf9f5ba6329048dc2fa59efa8f368aeae4d5ac5e6312',
     'micros_grant_copy_to': '77d3a32941c480be4d27ac75833a4ed05bd1a56ba1a98f763683463ff1837de2',
     'micros_grant_validate_range': 'd38328ae50c4208037c8e7c2ca6e4e629ceb26f71a1191a270e8be125532bb77',
     'plan_range': 'c8d5c14c2e1e218dbbbe4631d6851419830e52ffeca8c9f3f271f898eeb992a4'},
 1: {'micros_grant_handle_captured_user_ecall': 'ef22d946db8b189fbd91825154a83cfb11ddc2e258d315155931d48a53a1abd2',
     'panic_grant_syscall': '230b3b9d69d753928e27b11039796536bfdcc6d54a001797a7211a87827eeb4c'},
 2: {'copy_storage': '4b480638cdb607c5cc090e482b8427a0a73209fe3d249c13e8cf513b8528a036',
     'failure_state_matches': '5ec079ae8b78f7b7af2025512e8dd41279f960078f66b6d2617c150c844a0716',
     'fill_bytes': '11b5f5403205b7afe3c10346cf86a035777959dc7314b6b98bac8886d35bd489',
     'micros_grant_runtime_run_self_test': '96f17663b48175f054e1f67174ed8f983b32ef686050ef0867bd3991868ce8de',
     'pattern_matches': '513733b9d0669d3bf0b4a3c7b7e8224bfa1f8fe1cbdac34fb0a0785f115d7ff3',
     'physical_for': 'bd794c9790dcbfb2c7f9c75163f492f4268a3d7625fa902b016f07611fc299c4',
     'range_is_value': 'f1494ceb6590ca81ae05775c5e4bd52d99949e2adbca8bac8923911be69cba00',
     'snapshot_failure_state': 'e6534f398f87f6dec7367cb2009f8c1042d6eaaf127f887f4f76b8eb77726470',
     'storage_matches': '2cf2a64c99b80613e115a17e7c95e674a8fe695a665679f138c0fc4d520a984a',
     'write_pattern': 'c64840a8bd05240000d50443f2ad099d26db92760627865978d736e9474d9a78'},
 3: {'abi_result': 'f1692d52685cb41fe5d24e268980509a899f33fae0586cd5c70c229264c8e56c',
     'arm_command': 'd7330e6cb635194fbb77a6ae886b9a04fcd5c368246b50e32a23ded6f47335b9',
     'arm_control': 'fa351cd110a332b190a811b8bd069bca97cac639294fae66e7b2930fbf3e155f',
     'arm_raw': 'a06cd4f352a340747e9047fea6f01de0e7d18126de6a6db7af61f2872c497625',
     'bytes_equal': '2cf2a64c99b80613e115a17e7c95e674a8fe695a665679f138c0fc4d520a984a',
     'capture_page_tables': '7ca19b0d8f3d4a7a077f624e4faaf7f66c666aa9d671f8825842171ba1918853',
     'close_endpoint': '233b26dabc4923e2f13842f913c995c186dd04cf9620c80df8c4b7e41c69dd60',
     'copy_bytes': '4b480638cdb607c5cc090e482b8427a0a73209fe3d249c13e8cf513b8528a036',
     'current_actor': '252b0f08808ae07b3d0729463366ab1ccf65e26a453488d909077a66179e53d0',
     'endpoint_registry_is_empty': 'fb7e5f696e449d1b95fc73fc357e904ff0028cdc758700995741c5faa4320d48',
     'expected_satp': '649a8a28c787a5e26c825f5ab9a658506f81549ba1c6c7f5f1289e6a00f5067f',
     'failure_state_matches': '2a5e547c22f381b3834a443464f28a08ffa0dcb1dfa4a3d84dd2dabf8e0ea1a2',
     'fill_bytes': '11b5f5403205b7afe3c10346cf86a035777959dc7314b6b98bac8886d35bd489',
     'fill_context_pattern': '91141dcbd5aed935b672c5118aaf76ea31afbeddc700c94974406600046a6669',
     'micros_grant_syscall_runtime_run_self_test': '04577ecb59c300ea6058749a8e36eae2a9cd1e0160729ce1295a1bb75c4eb133',
     'micros_grant_syscall_test_after_dispatch': '2ba37746ca157d41333440efc5d6cb8f245a72880c48a95ab3361c2086d20988',
     'micros_grant_syscall_test_after_return': 'e649b102803449c945b436517d660698c8955172700a076a1c93a7856e0f4507',
     'micros_grant_syscall_test_before_ecall': 'b2d2217e4d7f6c0cbd780a2ac322422480a6dda144bedaebbb371b19b84d765f',
     'micros_grant_syscall_test_finish': '520601ce1fbfac7e2087c94be021eb7cb571873c391fe02f826b417152638b3e',
     'micros_grant_syscall_test_handle_control_trap': '3b6ae115bbe0a2e3a67302171eadb969744f91f969d831551d9c74fc7bf23dfa',
     'normalize_objects': '30a155e23cc5b75858d3cc87d591d5626ba1ce433288317bd1435cff7a8a7760',
     'pattern_matches': '593a14e157a1261d084fa312322243e7aeae28756b7c926b8131e88a832274aa',
     'physical_for': '92a2f6347bd20712565c8579230b3b9e532010cdbd5c4285a2e5769c2a3f4d42',
     'prepare_process': '96fadd3809d025d02309d47aad9f3360b438942bf5ae00f278766d0ce04c1436',
     'range_is_value': '7e9bcc44bbfd214f5ee258bcaa91b9f6e0e1c9dd7f1c02777865ed1c99396ddc',
     'read_satp': '24735af2a51205a6ac12d4b983c10f4bde6e6cef0b8e4e11d6aa49d9d881ff19',
     'release_address_space': '3597ec571e10cb024d841459d32c2b085917fa140aabb522d136535eedcb2a80',
     'release_thread': 'b6f09ef1b71aa9638e50406e095a82763f59d1d6b44cefe3f7c3117cf8cdfd3a',
     'reuse_grantor': '7043242b39c5f8f8dde1c5b11cf9aa43ddab4fd74fbde18eb405293f80f28aa4',
     'schedule_finish': '208d7f31520580a7e764ce29337349576411c38a970d16d98aede274b042172b',
     'schedule_next': '8a8be5bcd206630956fd1c4403d0c4fe875f24eb32c67caede18e6fadeb32b5c',
     'schedule_reuse': '190e0b48617ef7b9cba545c7b3853d80f6b9695d17250990ef29567d4b0e9684',
     'scheduler_is_at_idle_baseline': '232cf0dbef916ce30807a524d31df4268b1522017f1340daa7ccc73608850f10',
     'scheduler_state_valid': '044453286a297467e5141b330fa1d3af7d645419ae3885221a56de896740a94f',
     'snapshot_failure_state': '5838de5e639ca2ecd93f44f4efa1ebd60595f7e9057021bc502f276a7cf7f8ce',
     'snapshot_user_bytes': '4fd61bdc8f3a82d371267ede7c58bfc1259a7ef7fa3cbd0ff6bcf7584171c5a8',
     'test_mismatch': '971ac087d0c589d9873cbbf91afe0ff5a3d6b6b9cf1fa73fb958baef35e69f65',
     'transition_after_return': '395b31f068b274bcf9ac046a89662c90e35721f811364412418e34cb91fea5fb',
     'user_address_of': '4d70f3075c22136fcbddf26d0d72d7f7c762677794668c8578fa48fad7f8ff95',
     'user_bytes_match': '3ed05758e22bdee4ef8153d22f5aff77f9bd7debdec04ffce690e6aaf400af38',
     'validate_command_effect': '728476346cdd62c58f5e6a386434e550b5d917604c552771cf9f152eeac4f8fe',
     'write_pattern': '31c38f222e115baf2a8d5b7dd41e12be44ab330416eca2cab89b3ea2f7abc188'},
 4: {'cancel_plan_matches_reference': '525bf853385483ad1ca7ee251a76c44b9968da92645e34c85f66cffc519d802d',
     'context_pattern': '27beff55faa7c13139dcbfb8ca672501f612ff42033bf579e2651fabbb46d06c',
     'expect_create_failure_unchanged': '55d0a0797793ca471bb23b96afc0b0ad65bb952a371ce8d60b8add5201fcfc11',
     'micros_grant_model_test_run': '2ce591393ca4c2e019123f21962867b8e75e7aeb94fb3a2863ddad151a5be11c',
     'micros_grant_test_run': '07c4731e2f7c30beac8df79c3e0a7c24db2fb02a49d7846f1d7368ba3a5d9e2b',
     'model_fail': '74216a4783838e9482161632752398313a68bd785af9dede3921e3e0a1eec41f',
     'model_next': '88d510643c80dabe81f46f60351cf9cbf6fbb71a36caa967392e9c72ad448cca',
     'model_operation_name': '0269ed27d678ff510deba1f57b386ac4393b719cd23bc9947a7d8d729b6b8ce8',
     'process_handles_equal': '1eb9ca3d14ed288168beacc0916c15eb03aa6ecf2e283e022b4d3ccb7e92f678',
     'record_model_trace': '45c57c90177532671f75043947bffb2c90ec64b1bd53d94dabaa5a3b3c2d0d47',
     'records_equal': 'fa25c1e8fd5861430084c5924ee77fa5c535c39d53bf874a7e1c0ddb6b52943d',
     'recreate_process': 'a421aa2f691e0899f9d6339535d8a0f0ae6bc874c3c553b10e0a7048a8d65a29',
     'registries_equal': '0c558d0539e047bd16033e24598831e00b4c739f328e2478cc7a8514f6e3acd3',
     'setup_fixture': '0268e61f54ecc97a9957772241ad3b2b78db08c07919621fd096cc5364966930',
     'test_create_failures_and_capacity': '5cd990e98c56d43d5c60601b9352d274e0dc6c0922b42f5afe40641de592c91d',
     'test_create_inspect_revoke_and_reuse': 'c20a2cfe17b520b58cce1de46e6e6b907d695ad04ccfc40238ddc960d9a2d6d5',
     'test_endpoint_cancellation_and_reuse': '452d6f373f9e96322d35a4630f42ce5861aa60ec6ec5874c1ed86e8dc13ed915',
     'test_error_precedence_and_output_preservation': '847c4f009d1cb62429971684e7a8bc4da8d70bb0c95ba6117e3d6cdf548bc9a4',
     'test_failed_ipc_close_preserves_cancel_plan': '8ab2a01717bacf101b8e17163b87bb4480a10549920d947cab6ad821c879159b',
     'test_grant_model': 'beafadec38361483ab7d3a1ef4bdf4030ba49c2482006ce168bda566bb20b996',
     'test_grant_model_boundaries': '21ac68bf5d658c4b3efb098db617f72354d205d8924bd8669ca2dbb0ac3d1516',
     'test_terminal_generation_quarantines': '9d00b1f7737ddd1778a7351d87cc36882516e14642ad05bd906cf634e5d4528a',
     'test_token_boundaries': 'b2b5fd3ed3627730a922d55889865ad253a91961ff298c68f39ec397cfa3174a',
     'test_validator_rejects_corruption': 'da12bb718d9d9388b83bebd6ae06e4b678bd3b575b3ce6533d0aa8dfb7f1dc0e'},
 5: {'authority_equal': '42d59de8c82740c0cf279615d3f067a5e0793240944a85eecd81d0f42597c812',
     'create_grant': '9832f4fa141bb6954a50e7f0a5c5cb2c42825fab44b0957d0348919621cd3afb',
     'expect_authority_failure_unchanged': '1b64989ca1f45521c94bebee351e5c5e7e3498d9c79db8f7cda03f510b2f5926',
     'micros_grant_copy_test_run': 'ba940a6ead9b1d5ca49fdc4e6b2287454dc7774f8a5219f4647a111b4bb1db5b',
     'process_handles_equal': '1eb9ca3d14ed288168beacc0916c15eb03aa6ecf2e283e022b4d3ccb7e92f678',
     'setup_fixture': '8eddb16c4121e6fb8093083dd3505eea0ea012df577b5544b898916e3d1d79c4',
     'test_copy_authority_and_bounds': 'a44e0767b9f824aaeba3b6649b85f3fe1823ed096af279a4966ec883b08b86e2',
     'test_copy_plan_and_commit': '00ca3e0e1bbd211f41b3eb49069238d9eb289ee7a3f9cf657082b642463eb8ef'},
 6: {'expect_decode_failure': 'd8fc7322f7be70ea53cf9d133069c63581643834263fe4cd4b16bcaad93da503',
     'micros_grant_syscall_core_test_run': 'e48bcc5892f4c244b369fd6737d3c95668c625f6b884a944f3996c037f1cde54',
     'request_matches': 'be3f9e8ad71bfcae79c291207cd380ba9e6c72709c8b6e2972ec6b9c85053320'}}
BASELINE_SIGNATURES = ('e6ef14db1e52a16ee0a9849d3417175acc6dade9d2a737fe691d918d62566769',
 'de2964c3d47c9ce24490c268790f7c7f3fc0f2a586254d18f27b98ec3fa9413e',
 '05855d7b82814e75bdaf558d2ba74f455938a7567fb9fbd242510faf99027f3a',
 'a0c5c1782c2e655a15597000133e8c94b8e8ecb9915b41b36c0af8ef09011125',
 'b09f3b171a08c0e895c023977b9ca81fc4c788d1ca6dac153bef801d023a5835',
 'cfbf69249a716352b942640c7184810a42681bbc990debd0c8444ed2487f1b5a',
 'b197f4936ffb7cfe345ab6067cd3a8acafe1fbb8bd3c52d14478eeddfc59ec46')
ORIGINAL_ORDER = (('map_address_error',
  'plan_range',
  'copy_grant',
  'micros_grant_validate_range',
  'micros_grant_copy_from',
  'micros_grant_copy_to'),
 ('panic_grant_syscall', 'micros_grant_handle_captured_user_ecall'),
 ('copy_storage',
  'storage_matches',
  'snapshot_failure_state',
  'failure_state_matches',
  'fill_bytes',
  'physical_for',
  'write_pattern',
  'pattern_matches',
  'range_is_value',
  'micros_grant_runtime_run_self_test'),
 ('copy_bytes',
  'bytes_equal',
  'fill_bytes',
  'read_satp',
  'user_address_of',
  'expected_satp',
  'abi_result',
  'physical_for',
  'write_pattern',
  'pattern_matches',
  'range_is_value',
  'fill_context_pattern',
  'current_actor',
  'test_mismatch',
  'normalize_objects',
  'capture_page_tables',
  'snapshot_user_bytes',
  'user_bytes_match',
  'snapshot_failure_state',
  'failure_state_matches',
  'scheduler_state_valid',
  'arm_raw',
  'arm_command',
  'arm_control',
  'schedule_next',
  'schedule_reuse',
  'schedule_finish',
  'transition_after_return',
  'validate_command_effect',
  'micros_grant_syscall_test_before_ecall',
  'micros_grant_syscall_test_after_dispatch',
  'micros_grant_syscall_test_after_return',
  'release_address_space',
  'release_thread',
  'close_endpoint',
  'reuse_grantor',
  'micros_grant_syscall_test_handle_control_trap',
  'prepare_process',
  'endpoint_registry_is_empty',
  'scheduler_is_at_idle_baseline',
  'micros_grant_syscall_test_finish',
  'micros_grant_syscall_runtime_run_self_test'),
 ('process_handles_equal',
  'records_equal',
  'registries_equal',
  'setup_fixture',
  'recreate_process',
  'context_pattern',
  'test_token_boundaries',
  'test_create_inspect_revoke_and_reuse',
  'expect_create_failure_unchanged',
  'test_create_failures_and_capacity',
  'test_terminal_generation_quarantines',
  'test_endpoint_cancellation_and_reuse',
  'test_failed_ipc_close_preserves_cancel_plan',
  'test_error_precedence_and_output_preservation',
  'test_validator_rejects_corruption',
  'model_next',
  'model_operation_name',
  'record_model_trace',
  'model_fail',
  'cancel_plan_matches_reference',
  'test_grant_model_boundaries',
  'test_grant_model',
  'micros_grant_test_run',
  'micros_grant_model_test_run'),
 ('process_handles_equal',
  'authority_equal',
  'setup_fixture',
  'create_grant',
  'expect_authority_failure_unchanged',
  'test_copy_authority_and_bounds',
  'test_copy_plan_and_commit',
  'micros_grant_copy_test_run'),
 ('request_matches', 'expect_decode_failure', 'micros_grant_syscall_core_test_run'))
STABLE_HASHES = {'CMakePresets.json': 'eae6c86ce5ac8f0e1b39605ebbd6816135558f0b4f8ea293b3680d7ecb79df86',
 'arch/riscv64/grant_syscall_test.S': '23e1c37e054c0028ae8f78a14d888a594c161d0bea591805d7ea5723670c0d7d',
 'include/micros/grant.h': '9a3723ad14c99db1d54b5b2cb4d051a42a191379f33547a44b5ff769d8346d29',
 'include/micros/grant_copy.h': 'fc3a2a7c30cda2ca701e5f369c39870eccf3c060dcf6c8ded9b955a7a0c5fce4',
 'include/micros/grant_runtime.h': '1c1d153c4a8935f22963a506c93a8b62d72303b89d671634f4ac2672d8f7abb2',
 'include/micros/grant_syscall_core.h': '282b7a4499617a7b0eeaa7a52347466dde28da13215feab8ad011c839d0662d5',
 'include/micros/ipc_abi.h': 'ddf21e7cf2752908e7297a051e0c74bc8db0a37c8f3487413147961fca4eb5c3',
 'include/micros/syscall_abi.h': '8bd61b7bacc4b8c29fb62270aa8678930bb15a54e27e9aaaee03fafc5a9e8cad',
 'kernel/boot_component_tests.c': '6a22c42e953c6af39be80baee6334a583658e150f0dde2944d637692b4a53cc3',
 'kernel/grant.c': '00da274e5ec7ef45e03a68392bf33d32ba7b9eb2b5218e8ca9908a582291e707',
 'kernel/grant_abi.c': 'a53c92b09f814d43393d1d3515b09db98bbb17b0de211f17a067917b5c7665ad',
 'kernel/grant_copy_core.c': '84a1c1fdd005d27d044c19d81a1f3b69d4804380f0b9d1bbca1ddacbf3b9a501',
 'kernel/grant_internal.h': '96e3f0939ec325e618be5d089f0d53585b0a65a16ad995989d0761a0c315b6bf',
 'kernel/grant_runtime.c': '3e9ca759068b2f0f7e4c2c4a75a0a9b65d86a4717bb95e543dbfa0ef9c5ae267',
 'kernel/grant_runtime_internal.h': '03ada0e660616efea7c6d97cffbd803a4a07c253fbe7a515dbbd25459a2cc816',
 'kernel/grant_syscall.h': '860e12969e918a7517c96949c116393882b659a6d20157b17fda71f735c89573',
 'kernel/grant_syscall_core.c': '9959f9e3913133b95aac32adf954e83516bb40c71cec6e0e164101978e28a5ca',
 'kernel/grant_syscall_test.h': '5d9613bc532e0aff143123e73e2c297785dd7a0ad5fd0827f6ea4a4efbec9319',
 'kernel/grant_test.h': 'e9b7541252d594b5d77d7c85dfe3bf1361d1336231983e89fb2613edf861b171',
 'kernel/ipc_syscall.c': 'a281324a7c82fab9aebfbc238dad427af803ac09683cc1d0e3378fe385f85fa8',
 'kernel/syscall.c': '1f256f61b29759e004f1d26d0c5c634f6975a6c126e885320f1ca3a879a52803',
 'kernel/trap_test_hooks.c': '1a037ffb5e162083cfb4dcbb7a23b9cb9fdbec7a28cd843297dc71267745e6df',
 'tests/host/endpoint_test.c': 'e4bd7fe82fe2a878cecb6a940cba48a85d32d7900cc909f44dbe03c1c0602ccb',
 'tests/host/ipc_model_main.c': '451170f1a93a19ac85d347121257d89817de1ff2e928990d514fc3b719a813f3',
 'tools/run_qemu_smoke.py': '9e54f744074d077d05b52880495b73f7c74ae1db0a6ad751f8b7024688bf79e3'}
SHARED_INSPECTOR_DIGEST = 'efab84d99e3d5c49fc416a161816431b9e54615ac23af9aef3e9f0e995bc2e0d'
PLANNER_AST_DIGEST = '79cbadcd7b9bf78529f6c0c4b1930e5db27179f6f5af4bfaf09f5c458c921732'
CONTEXT_AST_DIGEST = '0c2af36928f0b9646d25c12f6c24b9a3e5cff97222f1041cbd902889738f0d3c'


def source(path):
    return (ROOT / path).read_text()


def digest_bytes(value):
    return hashlib.sha256(value).hexdigest()


def digest_tokens(value):
    return digest_bytes(json.dumps(tuple(value), separators=(",", ":")).encode())


def normal_names(family):
    if family == 0:
        return {
            "grant_copy_execute": "copy_grant",
            "grant_copy_map_address_error": "map_address_error",
            "grant_copy_plan_range": "plan_range",
        }
    if family in (3, 4):
        prefix = "grant_syscall_test_" if family == 3 else "grant_test_"
        return {
            prefix + (name[5:] if family == 4 and name.startswith("test_") else name): name
            for name in BASELINE_BODIES[family] if not name.startswith("micros_")
        }
    return {}


def owned_records(family, texts=None):
    texts = texts if texts is not None else {
        path: source(path) for path in FAMILY_PATHS[family] if (ROOT / path).is_file()
    }
    return {
        (path, name): record for path, text in texts.items()
        for name, record in function_records(text).items()
    }


def original_locations(family, texts=None):
    locations = {}
    names = normal_names(family)
    for (path, spelling), record in owned_records(family, texts).items():
        if family == 0 and spelling in (COPY_EXECUTION_PHASE, COPY_LOCAL_RANGE_PHASE, VALIDATION_PHASE):
            if path != COPY_SOURCES[0]:
                raise AssertionError("the private participant-plan phase has its exact copy owner")
            continue
        if family == 0 and spelling == RANGE_DEVICE_PHASE:
            if path != COPY_SOURCES[1]:
                raise AssertionError("the device-span phase has only its original range owner")
            continue
        if family == 1 and spelling in (SYSCALL_DISPATCH_PHASE, SYSCALL_CREATE_PHASE):
            if path != FAMILY_PATHS[1][0]:
                raise AssertionError("captured syscall phases remain in their exact file-local adapter")
            continue
        original = names.get(spelling, spelling)
        if original not in BASELINE_BODIES[family] or original in locations:
            raise AssertionError("unknown/duplicate original or unreviewed phase: " + spelling)
        locations[original] = (path, record)
    if set(locations) != set(BASELINE_BODIES[family]):
        raise AssertionError("every complete original grant definition remains present")
    return locations


def original_signature_tokens(family, original, record):
    names = normal_names(family)
    signature = tuple(names.get(token, token) for token in tokens(record.signature))
    if family == 0 and original == "plan_range" and record.name == "grant_copy_plan_range":
        if signature[:1] == ("static",):
            raise AssertionError("the one required range interface has its actual external declaration")
        signature = ("static", *signature)
    return signature


def original_body_digest(family, original, record):
    names = normal_names(family)
    body = tokens("{" + record.body + "}")
    if family == 0 and original == "copy_grant" and (0, original) not in FUNCTION_TASK_MIGRATION:
        body = copy_execution_actual_body_tokens(record)
    if family == 0 and original == "plan_range" and (0, original) not in FUNCTION_TASK_MIGRATION:
        body = range_device_actual_body_tokens(record)
    if family == 0 and original == "micros_grant_validate_range" and (0, original) not in FUNCTION_TASK_MIGRATION:
        body = validation_actual_body_tokens(record)
    if family == 1 and original == "micros_grant_handle_captured_user_ecall" and (1, original) not in FUNCTION_TASK_MIGRATION:
        body = syscall_actual_body_tokens(record)
    return digest_tokens(tuple(names.get(token, token) for token in body))


def work_declarations(owner, tag, fields):
    lexical = c_lexical_text(owner)
    starts = tuple(re.finditer(r"\bstruct " + tag + r"\s*\{", lexical))
    if len(starts) != 1:
        raise AssertionError("one file-local execution work type owns only original automatic objects")
    opening = lexical.index("{", starts[0].start())
    closing = matching_lexical(lexical, opening, "{", "}")
    declarations = tuple(tokens(part + ";") for part in lexical[opening + 1:closing].split(";") if part.strip())
    if declarations != tuple(tokens(declaration) for declaration, _ in fields):
        raise AssertionError("automatic work retains original fields, complete types and declaration order")
    return declarations


def copy_execution_declarations(owner):
    return work_declarations(owner, COPY_EXECUTION_TAG, COPY_EXECUTION_FIELDS)


def copy_execution_body_tokens(caller, phase, declarations, local_phase):
    if tuple(declarations) != tuple(tokens(declaration) for declaration, _ in COPY_EXECUTION_FIELDS):
        raise AssertionError("original declarations come from actual verified execution fields")
    body = list(tokens(caller))
    local_guard = list(tokens(
        "if (!" + COPY_LOCAL_RANGE_PHASE + "(local_address, length)) { return MICROS_GRANT_ERROR_RANGE; }"
    ))
    local_positions = [
        index for index in range(len(body)) if body[index:index + len(local_guard)] == local_guard
    ]
    local = list(tokens(local_phase))
    if len(local_positions) != 1 or local[:1] != ["{"] or local[-4:] != ["return", "true", ";", "}"]:
        raise AssertionError("local range admission retains exact original scalar arguments before IRQ save")
    local = local[1:-4]
    local_restored = []
    index = 0
    while index < len(local):
        if local[index] == "return":
            if local[index:index + 3] != ["return", "false", ";"]:
                raise AssertionError("local range success cannot bypass its original full shape check")
            local_restored.extend(tokens("return MICROS_GRANT_ERROR_RANGE;"))
            index += 3
        else:
            local_restored.append(local[index])
            index += 1
    start = local_positions[0]
    body[start:start + len(local_guard)] = local_restored
    guard = list(tokens(
        "if (!" + COPY_EXECUTION_PHASE + "(&work, local_address, length, required_permission)) { goto done; }"
    ))
    positions = [index for index in range(len(body)) if body[index:index + len(guard)] == guard]
    if len(positions) != 1:
        raise AssertionError("dual-plan execution binds exact caller state/scalars and common failure label")
    phase = list(tokens(phase))
    if phase[:1] != ["{"] or phase[-4:] != ["return", "true", ";", "}"]:
        raise AssertionError("participant-plan success follows both original plans and complete error classification")
    inner = phase[1:-4]
    if {"goto", "break", "continue"} & set(inner):
        raise AssertionError("participant planning cannot borrow caller exit or loop scope")
    restored = []
    index = 0
    while index < len(inner):
        if inner[index] == "return":
            if inner[index:index + 3] != ["return", "false", ";"]:
                raise AssertionError("partial participant planning cannot bypass remaining classification")
            restored.extend(tokens("goto done;"))
            index += 3
        else:
            restored.append(inner[index])
            index += 1
    start = positions[0]
    body[start:start + len(guard)] = restored
    return restore_automatic_work_tokens(body, COPY_EXECUTION_TAG, COPY_EXECUTION_FIELDS, declarations)


def restore_automatic_work_tokens(body, tag, fields, declarations):
    body = list(body)
    if tuple(declarations) != tuple(tokens(declaration) for declaration, _ in fields):
        raise AssertionError("automatic declarations come from the exact verified actual work fields")
    prefix = list(tokens("{ struct " + tag + " work;"))
    if body[:len(prefix)] != prefix:
        raise AssertionError("execution work is one original uninitialized automatic root object")
    del body[1:len(prefix)]
    names = tuple(declaration[-2] for declaration in declarations)
    restored = []
    index = 0
    while index < len(body):
        if body[index] == "work":
            if (
                body[index + 1:index + 2] not in (["."], ["->"])
                or body[index + 2:index + 3] not in ([name] for name in names)
                or (index and body[index - 1] in (".", "->"))
            ):
                raise AssertionError("execution work is not copied, reset, nested-qualified or retained")
            restored.append(body[index + 2])
            index += 3
        else:
            restored.append(body[index])
            index += 1
    initializers = []
    for actual, (_, initialized) in zip(declarations, fields):
        if not initialized:
            initializers.extend(actual)
            continue
        name = actual[-2]
        if restored[1:3] != [name, "="]:
            raise AssertionError("only original explicit plan initializers execute, in original order")
        end = restored.index(";", 3)
        value = restored[3:end]
        if initialized is True:
            cast = ["(", *actual[:-2], ")"]
            if value[:len(cast) + 1] != cast + ["{"]:
                raise AssertionError("plan initializer retains its exact original aggregate type")
            value = value[len(cast):]
        elif initialized != "value":
            raise AssertionError("automatic initializer transport is not declared by the original scope")
        initializers.extend((*actual[:-1], "=", *value, ";"))
        del restored[1:end + 1]
    restored[1:1] = initializers
    return tuple(restored)


def copy_execution_actual_body_tokens(record, owner=None):
    owner = source(COPY_SOURCES[0]) if owner is None else owner
    declarations = copy_execution_declarations(owner)
    phase = function_records(owner)[COPY_EXECUTION_PHASE]
    local = function_records(owner)[COPY_LOCAL_RANGE_PHASE]
    expected = (
        "static bool " + COPY_EXECUTION_PHASE
        + "(struct grant_copy_execution *work, uintptr_t local_address, size_t length, uint32_t required_permission)"
    )
    if phase.guards or tokens(phase.signature) != tokens(expected):
        raise AssertionError("actual participant-plan phase retains its complete file-local typed contract")
    if local.guards or tokens(local.signature) != tokens(
        "static bool " + COPY_LOCAL_RANGE_PHASE + "(uintptr_t local_address, size_t length)"
    ):
        raise AssertionError("local range admission retains its complete pure scalar types")
    return copy_execution_body_tokens(
        "{" + record.body + "}", "{" + phase.body + "}", declarations, "{" + local.body + "}",
    )


def validation_body_tokens(caller, phase, declarations):
    body = list(tokens(caller))
    call = list(tokens(
        VALIDATION_PHASE + "(&work, endpoint_registry, objects, length, required_permission);"
    ))
    positions = [index for index in range(len(body)) if body[index:index + len(call)] == call]
    phase = list(tokens(phase))
    if (
        len(positions) != 1 or phase[:1] != ["{"] or phase[-1:] != ["}"]
        or body[positions[0] + len(call):positions[0] + len(call) + 2] != ["done", ":"]
    ):
        raise AssertionError("remote-only planning returns directly to the original common IRQ/result exit")
    inner = phase[1:-1]
    if {"goto", "break", "continue"} & set(inner):
        raise AssertionError("remote-only planning cannot borrow a caller label or loop")
    restored = []
    index = 0
    while index < len(inner):
        if inner[index] == "return":
            if inner[index:index + 2] != ["return", ";"]:
                raise AssertionError("the void remote phase preserves the original caller-owned error result")
            restored.extend(tokens("goto done;"))
            index += 2
        else:
            restored.append(inner[index])
            index += 1
    start = positions[0]
    body[start:start + len(call)] = restored
    return restore_automatic_work_tokens(body, VALIDATION_WORK_TAG, VALIDATION_FIELDS, declarations)


def validation_actual_body_tokens(record, owner=None):
    owner = source(COPY_SOURCES[0]) if owner is None else owner
    declarations = work_declarations(owner, VALIDATION_WORK_TAG, VALIDATION_FIELDS)
    phase = function_records(owner)[VALIDATION_PHASE]
    if phase.guards or tokens(phase.signature) != tokens(
        "static void " + VALIDATION_PHASE + "(" + VALIDATION_PARAMETERS + ")"
    ):
        raise AssertionError("remote-only planning retains exact complete file-local scalar and state types")
    return validation_body_tokens("{" + record.body + "}", "{" + phase.body + "}", declarations)


def syscall_body_tokens(caller, dispatch, create, declarations):
    body = list(tokens(caller))
    dispatch = list(tokens(dispatch))
    create = list(tokens(create))
    if (
        dispatch[:1] != ["{"] or dispatch[-1:] != ["}"]
        or create[:1] != ["{"] or create[-1:] != ["}"]
    ):
        raise AssertionError("actual captured phases retain complete function scope")
    create_inner = create[1:-1]
    if {"break", "continue", "goto", "switch"} & set(create_inner):
        raise AssertionError("create preparation cannot borrow another switch/loop/caller exit")
    restored = []
    index = 0
    while index < len(create_inner):
        if create_inner[index] == "return":
            if create_inner[index:index + 2] != ["return", ";"]:
                raise AssertionError("create helper preserves only original case exits and caller-owned status")
            restored.extend(tokens("break;"))
            index += 2
        else:
            restored.append(create_inner[index])
            index += 1
    restored.extend(tokens("break;"))
    dispatch_inner = dispatch[1:-1]
    if {"return", "continue", "goto"} & set(dispatch_inner):
        raise AssertionError("dispatch retains its original complete operation switch")
    branch = list(tokens(
        "case MICROS_SYSCALL_ABI_GRANT_CREATE: "
        + SYSCALL_CREATE_PHASE + "(hart, frame, context, work); break;"
    ))
    positions = [
        index for index in range(len(dispatch_inner)) if dispatch_inner[index:index + len(branch)] == branch
    ]
    if len(positions) != 1:
        raise AssertionError("create dispatch binds the exact captured hart/frame/process/work and sole case exit")
    start = positions[0]
    dispatch_inner[start:start + len(branch)] = [
        *tokens("case MICROS_SYSCALL_ABI_GRANT_CREATE:"), *restored,
    ]
    call = list(tokens(SYSCALL_DISPATCH_PHASE + "(hart, frame, context, &work);"))
    positions = [index for index in range(len(body)) if body[index:index + len(call)] == call]
    if len(positions) != 1:
        raise AssertionError("captured dispatch binds the exact original runtime context and caller-owned state")
    start = positions[0]
    body[start:start + len(call)] = dispatch_inner
    return restore_automatic_work_tokens(body, SYSCALL_WORK_TAG, SYSCALL_FIELDS, declarations)


def syscall_actual_body_tokens(record, owner=None):
    owner = source(FAMILY_PATHS[1][0]) if owner is None else owner
    declarations = work_declarations(owner, SYSCALL_WORK_TAG, SYSCALL_FIELDS)
    records = function_records(owner)
    for name in (SYSCALL_DISPATCH_PHASE, SYSCALL_CREATE_PHASE):
        phase = records[name]
        if phase.guards or tokens(phase.signature) != tokens(
            "static void " + name + "(" + SYSCALL_PHASE_PARAMETERS + ")"
        ):
            raise AssertionError("captured private phases retain every complete original pointer/type binding")
    return syscall_body_tokens(
        "{" + record.body + "}", "{" + records[SYSCALL_DISPATCH_PHASE].body + "}",
        "{" + records[SYSCALL_CREATE_PHASE].body + "}", declarations,
    )


def range_device_body_tokens(caller, phase):
    body = list(tokens(caller))
    guard = list(tokens(
        "switch (" + RANGE_DEVICE_PHASE
        + "(process, device_authority, &current, &remaining, &device_fault, &address_error, &chunk_length)) {"
        "case MICROS_GRANT_OK: continue;"
        "case MICROS_GRANT_ERROR_PHASE: return MICROS_GRANT_ERROR_PHASE;"
        "case MICROS_GRANT_ERROR_INVARIANT: return MICROS_GRANT_ERROR_INVARIANT;"
        "default: return MICROS_GRANT_ERROR_INVARIANT; }"
    ))
    positions = [index for index in range(len(body)) if body[index:index + len(guard)] == guard]
    phase = list(tokens(phase))
    if len(positions) != 1 or phase[:1] != ["{"] or phase[-4:] != ["return", "MICROS_GRANT_OK", ";", "}"]:
        raise AssertionError("device span binds the exact original caller objects and checked success/failure scope")
    inner = phase[1:-4]
    if {"goto", "break", "continue"} & set(inner):
        raise AssertionError("device span cannot borrow an unverified loop or exit label")
    for index, token in enumerate(inner):
        if token == "return" and (
            inner[index + 1:index + 2] not in (["MICROS_GRANT_ERROR_PHASE"], ["MICROS_GRANT_ERROR_INVARIANT"])
            or inner[index + 2:index + 3] != [";"]
        ):
            raise AssertionError("device success occurs only after original validation and deferred-fault advancement")
    mutable = {"current", "remaining", "device_fault", "address_error", "chunk_length"}
    restored = []
    index = 0
    while index < len(inner):
        if inner[index] == "*" and inner[index + 1:index + 2] and inner[index + 1] in mutable:
            restored.append(inner[index + 1])
            index += 2
        else:
            if inner[index] in mutable:
                raise AssertionError("the original range objects are borrowed typed lvalues, not copied pointer state")
            restored.append(inner[index])
            index += 1
    restored.extend(tokens("continue;"))
    start = positions[0]
    body[start:start + len(guard)] = restored
    return tuple(body)


def range_device_actual_body_tokens(record, owner=None):
    owner = source(COPY_SOURCES[1]) if owner is None else owner
    phase = function_records(owner)[RANGE_DEVICE_PHASE]
    if phase.guards or tokens(phase.signature) != tokens(
        "static enum micros_grant_error " + RANGE_DEVICE_PHASE + "(" + RANGE_DEVICE_PARAMETERS + ")"
    ):
        raise AssertionError("actual device span retains every complete original pointer/handle type")
    return range_device_body_tokens("{" + record.body + "}", "{" + phase.body + "}")


def source_set_block(name, paths):
    return "set(\n    " + name + "\n" + "".join("    " + path + "\n" for path in paths) + ")"


def alias_rows():
    return tuple(
        (paths[0], (*paths[1:], header)) for paths, header in zip(SOURCE_SETS.values(), PRIVATE_HEADERS)
    )


def grant_data_plane_cmake_projection(text):
    blocks = [(name, paths, cmake_commands(text, "set", name)) for name, paths in SOURCE_SETS.items()]
    if not any(found for _, _, found in blocks):
        if any("${" + name + "}" in text for name in SOURCE_SETS):
            raise AssertionError("grant operands require their complete actual source sets")
        return text
    for index, (name, paths, found) in enumerate(blocks):
        expected = source_set_block(name, paths)
        if len(found) != 1 or found[0] != expected or text.count(expected + "\n\n") != 1:
            raise AssertionError("exact complete ordered grant source sets are required")
        text = text.replace(expected + "\n\n", "", 1)
        variable = "${" + name + "}"
        if text.count(variable) != (1, 2, 1)[index]:
            raise AssertionError("grant source sets have exactly their original four consumer operands")
        text = text.replace(variable, paths[0])
    return text


def restored_cmake(text):
    restored = grant_data_plane_cmake_projection(text)
    if digest_bytes(restored.encode()) != BASE_CMAKE_DIGEST:
        raise AssertionError("whole grant recipe inverse differs outside three sets/four operands")
    return restored


def planner_ast_without_grant_paths(text):
    tree = ast.parse(text)
    found = set()
    expected = dict(alias_rows())
    for node in tree.body:
        if not isinstance(node, ast.Assign) or len(node.targets) != 1 or not isinstance(node.targets[0], ast.Name):
            continue
        name = node.targets[0].id
        if name == "PATH_CLASSIFICATION_EQUIVALENTS":
            actual = ast.literal_eval(node.value)
            selected = {old: aliases for old, aliases in actual if old in expected}
            if selected != expected or sum(old in expected for old, _ in actual) != 3:
                raise AssertionError("grant aliases preserve complete independent baseline families once")
            node.value.elts = [item for item in node.value.elts if ast.literal_eval(item)[0] not in expected]
            found.add(name)
        elif name == "HOST_UNIT_ONLY_INPUTS":
            actual = ast.literal_eval(node.value)
            if actual.count(STRUCTURAL_TEST_PATH) != 1:
                raise AssertionError("the new grant gate is explicitly host-unit-only once")
            node.value.elts = [item for item in node.value.elts if ast.literal_eval(item) != STRUCTURAL_TEST_PATH]
            found.add(name)
    if found != {"PATH_CLASSIFICATION_EQUIVALENTS", "HOST_UNIT_ONLY_INPUTS"}:
        raise AssertionError("complete grant metadata assignments remain present")
    return tree


def context_ast_without_recipe_pin(text):
    tree = ast.parse(text)
    selected = []
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(
            isinstance(target, ast.Name) and target.id == "RECIPE_DIGESTS" for target in node.targets
        ):
            selected.append(node)
    if len(selected) != 1 or not isinstance(selected[0].value, ast.Dict):
        raise AssertionError("one actual complete recipe digest declaration is required")
    mapping = selected[0].value
    positions = [index for index, key in enumerate(mapping.keys) if ast.literal_eval(key) == "CMakeLists.txt"]
    if len(positions) != 1:
        raise AssertionError("one maintained CMake recipe entry is required")
    mapping.values[positions[0]] = ast.Constant(value=BASE_CMAKE_DIGEST)
    return tree


class GrantDataPlaneSourceLayoutTest(unittest.TestCase):
    def test_range_planning_has_its_exact_read_only_leaf_owner(self):
        facade = function_records(source(COPY_SOURCES[0]))
        self.assertNotIn("plan_range", set(facade))
        self.assertNotIn("grant_copy_plan_range", set(facade))
        leaf = ROOT / COPY_SOURCES[1]
        self.assertTrue(leaf.is_file())
        self.assertIn("grant_copy_plan_range", set(function_records(leaf.read_text())))

    def test_immutable_inventories_and_explicit_remaining_work_are_exact(self):
        self.assertEqual((6, 2, 10, 42, 24, 8, 3), tuple(len(value) for value in BASELINE_BODIES.values()))
        self.assertEqual(95, sum(map(len, BASELINE_BODIES.values())))
        self.assertEqual(65, len(INITIAL_SYMBOL_OWNER_MIGRATION))
        self.assertEqual(14, len(LONG_FUNCTIONS))
        self.assertEqual(5, len(SHORTER_TASKS))
        self.assertEqual(19, len(REQUIRED_TASKS))
        for active, initial in (
            (SYMBOL_OWNER_MIGRATION, INITIAL_SYMBOL_OWNER_MIGRATION),
            (FUNCTION_TASK_MIGRATION, REQUIRED_TASKS),
        ):
            self.assertIsInstance(active, tuple)
            self.assertEqual(len(active), len(set(active)))
            self.assertLessEqual(set(active), set(initial))
        for family, owners in ORIGINAL_FUNCTIONS.items():
            names = tuple(name for values in owners.values() for name in values)
            self.assertEqual(len(names), len(set(names)))
            self.assertEqual(set(BASELINE_BODIES[family]), set(names))

    def test_all_original_definitions_have_their_exact_current_and_final_owner(self):
        for family, owners in ORIGINAL_FUNCTIONS.items():
            locations = original_locations(family)
            for owner, names in owners.items():
                for name in names:
                    with self.subTest(family=family, original=name):
                        current = 0 if (family, name) in SYMBOL_OWNER_MIGRATION else owner
                        self.assertEqual(FAMILY_PATHS[family][current], locations[name][0])

    def test_all_95_complete_bodies_and_parameter_types_remain_exact(self):
        for family in ORIGINAL_FUNCTIONS:
            locations = original_locations(family)
            signatures = []
            for original in ORIGINAL_ORDER[family]:
                _, record = locations[original]
                with self.subTest(family=family, original=original):
                    self.assertEqual(BASELINE_BODIES[family][original], original_body_digest(family, original, record), REVIEWED_BASE)
                    signatures.append((original, original_signature_tokens(family, original, record)))
            self.assertEqual(BASELINE_SIGNATURES[family], digest_tokens(signatures))

    def test_only_original_pending_long_tasks_exceed_the_signal(self):
        actual = {
            (family, normal_names(family).get(name, name))
            for family in ORIGINAL_FUNCTIONS for (_, name), record in owned_records(family).items()
            if record.lines > 120
        }
        self.assertEqual(set(LONG_FUNCTIONS) & set(FUNCTION_TASK_MIGRATION), actual)

    def test_all_22_source_paths_three_headers_sets_and_real_consumers_are_exact(self):
        self.assertEqual(22, sum(map(len, FAMILY_PATHS)))
        self.assertEqual(22, len({path for paths in FAMILY_PATHS for path in paths}))
        cmake = source("CMakeLists.txt")
        for paths in FAMILY_PATHS:
            for path in paths:
                self.assertTrue((ROOT / path).is_file(), path)
        for index, (name, paths) in enumerate(SOURCE_SETS.items()):
            self.assertEqual((source_set_block(name, paths),), cmake_commands(cmake, "set", name))
            self.assertEqual((1, 2, 1)[index], cmake.count("${" + name + "}"))
        for header in PRIVATE_HEADERS:
            self.assertTrue((ROOT / header).is_file(), header)
            self.assertEqual({}, c_function_definitions(source(header)))
        self.assertEqual(1, cmake_commands(cmake, "add_executable", "micros")[0].count("${MICROS_GRANT_COPY_TARGET_SOURCES}"))
        for consumer in ("endpoint_test", "ipc_model_test"):
            self.assertEqual(1, cmake_commands(cmake, "add_executable", consumer)[0].count("${MICROS_GRANT_HOST_TEST_SOURCES}"))
        guarded = re.findall(r"if\(MICROS_BUILD_GRANT_SYSCALL_TEST\)([\s\S]*?)\nendif\(\)", cmake)
        self.assertEqual(1, sum("${MICROS_GRANT_SYSCALL_QEMU_TEST_SOURCES}" in block for block in guarded))
        self.assertEqual(BASE_CMAKE_DIGEST, digest_bytes(restored_cmake(cmake).encode()))
        self.assertEqual(digest_bytes(cmake.encode()), validation_context.RECIPE_DIGESTS["CMakeLists.txt"])

    def test_registry_runtime_public_abi_payload_and_drivers_remain_byte_exact(self):
        self.assertEqual(25, len(STABLE_HASHES))
        for path, expected in STABLE_HASHES.items():
            with self.subTest(path=path):
                self.assertEqual(expected, digest_bytes((ROOT / path).read_bytes()))
        self.assertEqual(SHARED_INSPECTOR_DIGEST, digest_bytes((ROOT / "tests/host/c_source_records.py").read_bytes()))

    def test_only_declared_recipe_pin_and_classification_metadata_change(self):
        for name, tree, expected in (
            ("planner", planner_ast_without_grant_paths(source("tools/validation_plan.py")), PLANNER_AST_DIGEST),
            ("context", context_ast_without_recipe_pin(source("tools/validation_context.py")), CONTEXT_AST_DIGEST),
        ):
            self.assertEqual(expected, digest_bytes(ast.dump(tree, include_attributes=False).encode()), name)

    def test_all_four_independent_classification_dimensions_remain_exact(self):
        copy_gates = tuple("test-qemu-" + name for name in (
            "grant", "grant-syscall", "user-runtime", "ipc-ecall-core", "ipc-syscall", "ipc-syscall-panic",
            "frame-ownership", "user-address-space", "address-space-handoff", "user-execution", "scheduler",
        ))
        targets = ("test-qemu-grant-syscall", "test-qemu-address-space-handoff")
        dimensions = ((False, copy_gates, ("test-ipc-model",), ()), (False, (), ("test-ipc-model",), ()), (False, targets, (), ()))
        for (_, aliases), expected in zip(alias_rows(), dimensions):
            for path in aliases:
                with self.subTest(path=path):
                    self.assertEqual(expected, (
                        validation_plan.requires_full_tier([path]), tuple(validation_plan.affected_workflows([path])),
                        tuple(validation_plan.affected_slow_models([path])), tuple(validation_plan.affected_image_workflows([path])),
                    ))
        self.assertEqual((False, [], [], []), (
            validation_plan.requires_full_tier([STRUCTURAL_TEST_PATH]),
            validation_plan.affected_workflows([STRUCTURAL_TEST_PATH]),
            validation_plan.affected_slow_models([STRUCTURAL_TEST_PATH]),
            validation_plan.affected_image_workflows([STRUCTURAL_TEST_PATH]),
        ))

    def test_full_recipe_inverse_rejects_unknown_members_operands_and_other_code(self):
        current = source("CMakeLists.txt")
        self.assertEqual(BASE_CMAKE_DIGEST, digest_bytes(restored_cmake(current).encode()))
        for changed in (
            current.replace("kernel/grant_copy_range.c", "kernel/unreviewed_range.c", 1),
            current.replace("${MICROS_GRANT_HOST_TEST_SOURCES}", "tests/host/grant_test.c", 1),
            current + "\nconsumer(${MICROS_GRANT_COPY_TARGET_SOURCES})\n",
            current.replace("--timeout\n            8", "--timeout\n            9", 1),
        ):
            self.assertNotEqual(current, changed)
            with self.subTest(changed=changed[-100:]), self.assertRaises(AssertionError):
                restored_cmake(changed)

    def test_inverse_retains_real_range_predicates_and_complete_signatures(self):
        text = source(COPY_SOURCES[1])
        records = function_records(text)
        original = records["grant_copy_plan_range"]
        changed = original.body.replace("candidate.chunk_count >= 2", "candidate.chunk_count > 2", 1)
        self.assertNotEqual(original.body, changed)
        candidate = function_records(text.replace(original.body, changed))["grant_copy_plan_range"]
        self.assertNotEqual(BASELINE_BODIES[0]["plan_range"], original_body_digest(0, "plan_range", candidate))
        changed = text.replace("size_t length,", "uint32_t length,", 1)
        candidate = function_records(changed)["grant_copy_plan_range"]
        self.assertNotEqual(
            original_signature_tokens(0, "plan_range", original), original_signature_tokens(0, "plan_range", candidate),
        )

    def test_independent_recipe_fixture_preserves_all_four_original_consumers(self):
        expected = (
            "add_executable(micros before kernel/grant_copy.c after)\n"
            "add_executable(endpoint_test tests/host/grant_test.c)\n"
            "add_executable(ipc_model_test tests/host/grant_test.c)\n"
            "if(MICROS_BUILD_GRANT_SYSCALL_TEST)\n"
            "target_sources(micros PRIVATE arch/riscv64/grant_syscall_test.S kernel/grant_syscall_test.c)\nendif()\n"
        )
        current = (
            "".join(source_set_block(name, paths) + "\n\n" for name, paths in SOURCE_SETS.items())
            + "add_executable(micros before ${MICROS_GRANT_COPY_TARGET_SOURCES} after)\n"
            "add_executable(endpoint_test ${MICROS_GRANT_HOST_TEST_SOURCES})\n"
            "add_executable(ipc_model_test ${MICROS_GRANT_HOST_TEST_SOURCES})\n"
            "if(MICROS_BUILD_GRANT_SYSCALL_TEST)\n"
            "target_sources(micros PRIVATE arch/riscv64/grant_syscall_test.S ${MICROS_GRANT_SYSCALL_QEMU_TEST_SOURCES})\nendif()\n"
        )
        self.assertEqual(expected, grant_data_plane_cmake_projection(current))
        for changed in (
            current.replace("kernel/grant_copy_range.c", "kernel/other_range.c", 1),
            current.replace("${MICROS_GRANT_HOST_TEST_SOURCES}", "tests/host/grant_test.c", 1),
            current + "\nconsumer(${MICROS_GRANT_SYSCALL_QEMU_TEST_SOURCES})\n",
        ):
            with self.subTest(changed=changed[-100:]), self.assertRaises(AssertionError):
                grant_data_plane_cmake_projection(changed)
        self.assertNotEqual(
            expected, grant_data_plane_cmake_projection(current.replace(" before ", " mutated_before ")),
        )

    def test_independent_metadata_fixture_retains_unrelated_rows_and_algorithms(self):
        expected = (
            "PATH_CLASSIFICATION_EQUIVALENTS = (('original', ('kept',)),)\n"
            "HOST_UNIT_ONLY_INPUTS = ('original_host',)\n"
            "def algorithm(value):\n    return value + 1\n"
        )
        current = (
            "PATH_CLASSIFICATION_EQUIVALENTS = " + repr((("original", ("kept",)), *alias_rows())) + "\n"
            "HOST_UNIT_ONLY_INPUTS = " + repr(("original_host", STRUCTURAL_TEST_PATH)) + "\n"
            "def algorithm(value):\n    return value + 1\n"
        )
        fingerprint = lambda tree: ast.dump(tree, include_attributes=False)
        self.assertEqual(fingerprint(ast.parse(expected)), fingerprint(planner_ast_without_grant_paths(current)))
        for changed in (
            current.replace("kernel/grant_copy_range.c", "kernel/unchecked_range.c"),
            current.replace(STRUCTURAL_TEST_PATH, "tests/host/other.py"),
            current.replace("'kernel/grant_copy_range.c', ", ""),
        ):
            self.assertNotEqual(current, changed)
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                planner_ast_without_grant_paths(changed)
        self.assertNotEqual(
            fingerprint(ast.parse(expected)),
            fingerprint(planner_ast_without_grant_paths(current.replace("value + 1", "value - 1"))),
        )

    def test_copy_execution_exposes_typed_dual_plan_phase_and_root_irq_lifetime(self):
        owner = source(COPY_SOURCES[0])
        records = function_records(owner)
        self.assertIn(COPY_EXECUTION_PHASE, set(records))
        self.assertEqual(
            tokens("static bool " + COPY_EXECUTION_PHASE + "(struct grant_copy_execution *work, "
                   "uintptr_t local_address, size_t length, uint32_t required_permission)"),
            tokens(records[COPY_EXECUTION_PHASE].signature),
        )
        self.assertIsNotNone(re.search(r"\bstruct grant_copy_execution\s*\{", owner))
        root = records["grant_copy_execute"]
        self.assertLessEqual(root.lines, 120)
        self.assertEqual(1, root.body.count("riscv_irq_save()"))
        self.assertEqual(1, root.body.count("riscv_irq_restore(saved_status)"))
        self.assertNotIn("riscv_irq_", records[COPY_EXECUTION_PHASE].body)
        self.assertEqual(
            tokens("static bool " + COPY_LOCAL_RANGE_PHASE + "(uintptr_t local_address, size_t length)"),
            tokens(records[COPY_LOCAL_RANGE_PHASE].signature),
        )
        self.assertLess(root.body.index(COPY_LOCAL_RANGE_PHASE), root.body.index("riscv_irq_save()"))

    def test_copy_execution_inverse_preserves_dual_plan_order_scope_initializers_and_common_exit(self):
        caller = (
            "{ struct grant_copy_execution work; "
            "work.remote_plan = (struct micros_grant_copy_range_plan){0}; "
            "work.local_plan = (struct micros_grant_copy_range_plan){0}; uintptr_t saved_status; "
            "if (bad_shape()) { return ARGUMENT; } "
            "if (!grant_copy_local_range_is_valid(local_address, length)) { return MICROS_GRANT_ERROR_RANGE; } "
            "saved_status = riscv_irq_save(); "
            "original_authority_phase_zero_device(&work.authority, &work.ledger); "
            "if (!grant_copy_prepare_participant_plans(&work, local_address, length, required_permission)) { goto done; } "
            "select_direction(&work.remote_plan, &work.local_plan, &work.source_plan, &work.destination_plan); "
            "work.error = validate(work.source_plan, work.destination_plan, length); "
            "if (work.error != OK) { goto done; } commit(work.source_plan, work.destination_plan, length); "
            "done: riscv_irq_restore(saved_status); return work.error; }"
        )
        phase = (
            "{ work->remote_error = remote(work->authority.grantor, length, required_permission); "
            "work->local_error = local(work->authority.grantee, local_address, length, required_permission); "
            "if (work->remote_error == INVARIANT || work->local_error == INVARIANT) { work->error = INVARIANT; return false; } "
            "if (work->remote_error == PHASE || work->local_error == PHASE) { work->error = PHASE; return false; } "
            "if (work->remote_error != OK || work->local_error != OK) { work->error = FAULT; return false; } return true; }"
        )
        declarations = tuple(tokens(declaration) for declaration, _ in COPY_EXECUTION_FIELDS)
        local_phase = "{ if (length != 0 && local_address == 0) { return false; } return true; }"

        def restore(caller, phase, declarations):
            return copy_execution_body_tokens(caller, phase, declarations, local_phase)

        expected = (
            "{ const struct micros_frame_ownership *ledger; struct micros_grant_copy_authority authority; "
            "struct micros_grant_copy_range_plan remote_plan = {0}; struct micros_grant_copy_range_plan local_plan = {0}; "
            "struct micros_tty_device_authority device_authority; "
            "const struct micros_grant_copy_range_plan *source_plan; "
            "const struct micros_grant_copy_range_plan *destination_plan; "
            "enum micros_grant_error remote_error; enum micros_grant_error local_error; enum micros_grant_error error; "
            "enum micros_tty_device_authority_status device_status; uintptr_t saved_status; "
            "if (bad_shape()) { return ARGUMENT; } "
            "if (length != 0 && local_address == 0) { return MICROS_GRANT_ERROR_RANGE; } "
            "saved_status = riscv_irq_save(); "
            "original_authority_phase_zero_device(&authority, &ledger); "
            "remote_error = remote(authority.grantor, length, required_permission); "
            "local_error = local(authority.grantee, local_address, length, required_permission); "
            "if (remote_error == INVARIANT || local_error == INVARIANT) { error = INVARIANT; goto done; } "
            "if (remote_error == PHASE || local_error == PHASE) { error = PHASE; goto done; } "
            "if (remote_error != OK || local_error != OK) { error = FAULT; goto done; } "
            "select_direction(&remote_plan, &local_plan, &source_plan, &destination_plan); "
            "error = validate(source_plan, destination_plan, length); if (error != OK) { goto done; } "
            "commit(source_plan, destination_plan, length); done: riscv_irq_restore(saved_status); return error; }"
        )
        self.assertEqual(tokens(expected), restore(caller, phase, declarations))
        for changed in (
            caller.replace("struct grant_copy_execution work;", "struct grant_copy_execution work = {0};"),
            caller.replace("(&work, local_address,", "(&other, local_address,"),
            caller.replace("local_address, length, required_permission", "other_address, length, required_permission"),
            caller.replace("work.error = validate", "copy = work; work.error = validate"),
            caller.replace("(struct micros_grant_copy_range_plan){0}", "(struct other_plan){0}", 1),
        ):
            self.assertNotEqual(caller, changed)
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                restore(changed, phase, declarations)
        for changed in (
            phase.replace("return false;", "return true;", 1),
            phase.replace("work->remote_error", "work->extra", 1),
            phase.replace("return false;", "goto done;", 1),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                restore(caller, changed, declarations)
        self.assertNotEqual(
            tokens(expected), restore(caller, phase.replace("work->error = PHASE", "work->error = FAULT"), declarations),
        )
        self.assertNotEqual(
            tokens(expected), restore(caller.replace("riscv_irq_restore(saved_status);", ""), phase, declarations),
        )
        with self.assertRaises(AssertionError):
            copy_execution_body_tokens(caller, phase, declarations, local_phase.replace("return false;", "return true;"))
        with self.assertRaises(AssertionError):
            copy_execution_body_tokens(caller.replace("local_range_is_valid(local_address", "local_range_is_valid(other_address"),
                                       phase, declarations, local_phase)
        owner = "struct grant_copy_execution { " + " ".join(declaration for declaration, _ in COPY_EXECUTION_FIELDS) + " };"
        self.assertEqual(declarations, copy_execution_declarations(owner))
        with self.assertRaises(AssertionError):
            copy_execution_declarations(owner.replace("enum micros_grant_error remote_error;", "uint32_t remote_error;"))

    def test_range_planner_exposes_typed_device_span_without_new_state_or_irq(self):
        records = function_records(source(COPY_SOURCES[1]))
        self.assertIn(RANGE_DEVICE_PHASE, set(records))
        phase = records[RANGE_DEVICE_PHASE]
        self.assertEqual(
            tokens("static enum micros_grant_error " + RANGE_DEVICE_PHASE + "(" + RANGE_DEVICE_PARAMETERS + ")"),
            tokens(phase.signature),
        )
        root = records["grant_copy_plan_range"]
        self.assertLess(root.lines, 94)
        self.assertNotIn("riscv_irq_", phase.body)
        for declaration in (
            "struct micros_grant_copy_range_plan candidate = {0};", "uintptr_t current = address;",
            "size_t remaining = length;", "bool device_fault = false;",
            "uint64_t physical_address;", "uint32_t permissions;", "size_t contiguous;",
            "size_t chunk_length;", "enum micros_user_address_space_error address_error;",
        ):
            self.assertIn(declaration, root.body)

    def test_device_span_inverse_preserves_original_lvalues_deferred_fault_and_continue(self):
        caller = (
            "{ candidate_init(); while (remaining != 0) { original_five_locals(); "
            "if (device_authority != NULL && intersects(device_authority, process, current, 1)) { "
            "switch (grant_copy_plan_device_span(process, device_authority, &current, &remaining, "
            "&device_fault, &address_error, &chunk_length)) { "
            "case MICROS_GRANT_OK: continue; "
            "case MICROS_GRANT_ERROR_PHASE: return MICROS_GRANT_ERROR_PHASE; "
            "case MICROS_GRANT_ERROR_INVARIANT: return MICROS_GRANT_ERROR_INVARIANT; "
            "default: return MICROS_GRANT_ERROR_INVARIANT; } } "
            "original_regular_page_plan(); } "
            "if (device_fault) { return MICROS_GRANT_ERROR_FAULT; } *plan = candidate; return MICROS_GRANT_OK; }"
        )
        phase = (
            "{ *address_error = validate_device(process, device_authority->root_physical_address); "
            "if (*address_error == ADDRESS_PHASE) { return MICROS_GRANT_ERROR_PHASE; } "
            "if (*address_error != ADDRESS_OK) { return MICROS_GRANT_ERROR_INVARIANT; } "
            "*chunk_length = (DEVICE_BASE + DEVICE_LENGTH) - *current; "
            "if (*chunk_length > *remaining) { *chunk_length = *remaining; } "
            "*current += *chunk_length; *remaining -= *chunk_length; *device_fault = true; return MICROS_GRANT_OK; }"
        )
        expected = (
            "{ candidate_init(); while (remaining != 0) { original_five_locals(); "
            "if (device_authority != NULL && intersects(device_authority, process, current, 1)) { "
            "address_error = validate_device(process, device_authority->root_physical_address); "
            "if (address_error == ADDRESS_PHASE) { return MICROS_GRANT_ERROR_PHASE; } "
            "if (address_error != ADDRESS_OK) { return MICROS_GRANT_ERROR_INVARIANT; } "
            "chunk_length = (DEVICE_BASE + DEVICE_LENGTH) - current; "
            "if (chunk_length > remaining) { chunk_length = remaining; } "
            "current += chunk_length; remaining -= chunk_length; device_fault = true; continue; } "
            "original_regular_page_plan(); } "
            "if (device_fault) { return MICROS_GRANT_ERROR_FAULT; } *plan = candidate; return MICROS_GRANT_OK; }"
        )
        self.assertEqual(tokens(expected), range_device_body_tokens(caller, phase))
        for changed in (
            caller.replace("&current, &remaining", "&other_current, &remaining"),
            caller.replace("case MICROS_GRANT_OK: continue;", "case MICROS_GRANT_OK: return MICROS_GRANT_OK;"),
            caller.replace("default: return MICROS_GRANT_ERROR_INVARIANT;", "default: return MICROS_GRANT_OK;"),
        ):
            self.assertNotEqual(caller, changed)
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                range_device_body_tokens(changed, phase)
        for changed in (
            phase.replace("return MICROS_GRANT_ERROR_PHASE;", "return MICROS_GRANT_OK;"),
            phase.replace("*current +=", "current +="),
            phase.replace("return MICROS_GRANT_OK; }", "continue; }"),
            phase.replace("return MICROS_GRANT_ERROR_INVARIANT;", "goto done;"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                range_device_body_tokens(caller, changed)
        self.assertNotEqual(
            tokens(expected), range_device_body_tokens(caller, phase.replace("*device_fault = true", "*device_fault = false")),
        )
        self.assertNotEqual(
            tokens(expected), range_device_body_tokens(caller.replace("if (device_fault)", "if (false)"), phase),
        )

    def test_remote_only_validation_exposes_typed_plan_phase_without_copy_or_local_range(self):
        records = function_records(source(COPY_SOURCES[0]))
        self.assertIn(VALIDATION_PHASE, set(records))
        phase = records[VALIDATION_PHASE]
        self.assertEqual(
            tokens("static void " + VALIDATION_PHASE + "(" + VALIDATION_PARAMETERS + ")"), tokens(phase.signature),
        )
        root = records["micros_grant_validate_range"]
        self.assertLess(root.lines, 98)
        self.assertEqual(1, root.body.count("riscv_irq_save()"))
        self.assertEqual(1, root.body.count("riscv_irq_restore(saved_status)"))
        for excluded in ("local_address", "micros_grant_copy_commit", "riscv_irq_"):
            self.assertNotIn(excluded, phase.body)

    def test_remote_validation_inverse_preserves_one_plan_no_copy_and_original_void_error_exit(self):
        caller = (
            "{ struct grant_copy_validation work; work.remote_plan = (struct micros_grant_copy_range_plan){0}; "
            "uintptr_t saved_status; original_early_arguments(); saved_status = riscv_irq_save(); "
            "original_authority_phase_zero(&work.authority, &work.ledger, &work.error); "
            "grant_copy_prepare_remote_plan(&work, endpoint_registry, objects, length, required_permission); "
            "done: riscv_irq_restore(saved_status); return work.error; }"
        )
        phase = (
            "{ work->device_status = capture_device_authority(endpoint_registry, objects, &work->device_authority); "
            "if (work->device_status == INVALID) { work->error = INVARIANT; return; } "
            "work->error = prepare_remote(work->authority.grantor, work->authority.remote_address, length, "
            "required_permission, &work->remote_plan); }"
        )
        expected = (
            "{ const struct micros_frame_ownership *ledger; struct micros_grant_copy_authority authority; "
            "struct micros_grant_copy_range_plan remote_plan = {0}; "
            "struct micros_tty_device_authority device_authority; enum micros_grant_error error; "
            "enum micros_tty_device_authority_status device_status; uintptr_t saved_status; "
            "original_early_arguments(); saved_status = riscv_irq_save(); "
            "original_authority_phase_zero(&authority, &ledger, &error); "
            "device_status = capture_device_authority(endpoint_registry, objects, &device_authority); "
            "if (device_status == INVALID) { error = INVARIANT; goto done; } "
            "error = prepare_remote(authority.grantor, authority.remote_address, length, required_permission, &remote_plan); "
            "done: riscv_irq_restore(saved_status); return error; }"
        )
        declarations = tuple(tokens(declaration) for declaration, _ in VALIDATION_FIELDS)
        self.assertEqual(tokens(expected), validation_body_tokens(caller, phase, declarations))
        for changed in (
            caller.replace("struct grant_copy_validation work;", "struct grant_copy_validation work = {0};"),
            caller.replace("(&work, endpoint_registry,", "(&copy, endpoint_registry,"),
            caller.replace("endpoint_registry, objects, length,", "other_registry, objects, length,"),
            caller.replace("done: riscv_irq_restore", "new_step(); done: riscv_irq_restore"),
            caller.replace("(struct micros_grant_copy_range_plan){0}", "(struct other_plan){0}"),
        ):
            self.assertNotEqual(caller, changed)
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                validation_body_tokens(changed, phase, declarations)
        for changed in (
            phase.replace("return;", "return true;"),
            phase.replace("return;", "goto done;"),
            phase.replace("work->authority.grantor", "work->local_plan"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                validation_body_tokens(caller, changed, declarations)
        self.assertNotEqual(
            tokens(expected), validation_body_tokens(caller, phase.replace("work->error = INVARIANT", "work->error = OK"), declarations),
        )
        self.assertNotEqual(
            tokens(expected), validation_body_tokens(caller, phase.replace("length, required_permission", "length, READ"), declarations),
        )
        self.assertNotEqual(
            tokens(expected), validation_body_tokens(caller, phase.replace("work->error = prepare_remote", "copy_bytes(); work->error = prepare_remote"), declarations),
        )

    def test_captured_syscall_exposes_typed_dispatch_and_prepare_device_publication(self):
        records = function_records(source(FAMILY_PATHS[1][0]))
        for name in (SYSCALL_DISPATCH_PHASE, SYSCALL_CREATE_PHASE):
            with self.subTest(phase=name):
                self.assertIn(name, set(records))
                self.assertEqual(
                    tokens("static void " + name + "(" + SYSCALL_PHASE_PARAMETERS + ")"),
                    tokens(records[name].signature),
                )
                self.assertLessEqual(records[name].lines, 120)
        self.assertLessEqual(records["micros_grant_handle_captured_user_ecall"].lines, 120)

    def test_syscall_inverse_preserves_runtime_decode_create_case_and_success_only_publication(self):
        caller = (
            "{ struct grant_syscall_execution work; work.created = MICROS_GRANT_NONE; "
            "original_runtime_checks(&work.grant_registry, &work.endpoint_registry); "
            "work.decode_result = decode(arguments, &work.request); "
            "if (work.decode_result != OK) { frame->a0 = work.decode_result; return NORMAL; } "
            "grant_syscall_execute_request(hart, frame, context, &work); "
            "original_publication(frame, work.request.operation, work.created, work.error, &work.abi_result); return NORMAL; }"
        )
        dispatch = (
            "{ switch (work->request.operation) { "
            "case MICROS_SYSCALL_ABI_GRANT_CREATE: grant_syscall_create_request(hart, frame, context, work); break; "
            "case REVOKE: work->error = revoke(work->grant_registry, context->process, work->request.grant); break; "
            "default: panic_grant_syscall(hart, frame, \"grant-request-invariant\"); } }"
        )
        create = (
            "{ work->error = prepare_create(work->grant_registry, context->process, &work->create_plan); "
            "if (work->error != OK) { return; } "
            "work->device_status = capture_device(work->endpoint_registry, context->objects, &work->device_authority); "
            "if (work->device_status == INVARIANT) { panic_grant_syscall(hart, frame, \"grant-device-authority-invariant\"); } "
            "if (device_intersects(&work->device_authority, context->process, work->request.local_address)) { "
            "work->error = FAULT; return; } "
            "commit_create(work->grant_registry, &work->create_plan, &work->created); }"
        )
        expected = (
            "{ struct micros_grant_registry *grant_registry; struct micros_endpoint_registry *endpoint_registry; "
            "struct micros_grant_create_plan create_plan; struct micros_grant_syscall_request request; "
            "struct micros_tty_device_authority device_authority; micros_grant_t created = MICROS_GRANT_NONE; "
            "enum micros_syscall_abi_result decode_result; enum micros_grant_error error; "
            "enum micros_tty_device_authority_status device_status; uint64_t abi_result; "
            "original_runtime_checks(&grant_registry, &endpoint_registry); "
            "decode_result = decode(arguments, &request); "
            "if (decode_result != OK) { frame->a0 = decode_result; return NORMAL; } "
            "switch (request.operation) { case MICROS_SYSCALL_ABI_GRANT_CREATE: "
            "error = prepare_create(grant_registry, context->process, &create_plan); "
            "if (error != OK) { break; } "
            "device_status = capture_device(endpoint_registry, context->objects, &device_authority); "
            "if (device_status == INVARIANT) { panic_grant_syscall(hart, frame, \"grant-device-authority-invariant\"); } "
            "if (device_intersects(&device_authority, context->process, request.local_address)) { error = FAULT; break; } "
            "commit_create(grant_registry, &create_plan, &created); break; "
            "case REVOKE: error = revoke(grant_registry, context->process, request.grant); break; "
            "default: panic_grant_syscall(hart, frame, \"grant-request-invariant\"); } "
            "original_publication(frame, request.operation, created, error, &abi_result); return NORMAL; }"
        )
        declarations = tuple(tokens(declaration) for declaration, _ in SYSCALL_FIELDS)
        self.assertEqual(tokens(expected), syscall_body_tokens(caller, dispatch, create, declarations))
        for changed in (
            caller.replace("struct grant_syscall_execution work;", "struct grant_syscall_execution work = {0};"),
            caller.replace("context, &work", "other_context, &work"),
            caller.replace("context, &work", "context, &copy"),
            caller.replace("original_publication", "copy = work; original_publication"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                syscall_body_tokens(changed, dispatch, create, declarations)
        for changed in (
            dispatch.replace("context, work", "other_context, work"),
            dispatch.replace("work); break;", "work);", 1),
            dispatch.replace("case REVOKE:", "return; case REVOKE:"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                syscall_body_tokens(caller, changed, create, declarations)
        for changed in (
            create.replace("return;", "return true;", 1),
            create.replace("return;", "break;", 1),
            create.replace("return;", "goto done;", 1),
            create.replace("work->created", "work->extra"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                syscall_body_tokens(caller, dispatch, changed, declarations)
        for changed in (
            caller.replace("MICROS_GRANT_NONE", "0"),
            caller.replace("original_runtime_checks", "weakened_runtime_checks"),
            caller.replace("original_publication", "early_publication"),
        ):
            self.assertNotEqual(tokens(expected), syscall_body_tokens(changed, dispatch, create, declarations))
        self.assertNotEqual(
            tokens(expected), syscall_body_tokens(caller, dispatch, create.replace("work->error = FAULT", "work->error = OK"), declarations),
        )


if __name__ == "__main__":
    unittest.main()

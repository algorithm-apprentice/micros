"""Bounded source-contract checks for Accepted ADR-0062.

The frozen inventories below come from e5b4da6, not from the evolving worktree.
This module never invokes Git, a compiler, a model, QEMU, or the network.
"""

from __future__ import annotations

from collections import Counter
import hashlib
from pathlib import Path
import re
import unittest
from unittest.mock import patch

from tools import validation_plan
from tests.host.c_source_records import (
    Function,
    compact,
    cpp_guards,
    function_records,
    matching,
    matching_lexical,
)
from tests.host.test_kernel_object_source_layout import restored_kernel_objects_source
from tests.host.test_frame_ownership_source_layout import (
    c_function_definitions,
    c_lexical_text,
    c_signature_start,
    cmake_command,
    cmake_if_blocks,
    tokens,
)
from tests.host.test_kernel_pm_control_source_layout import macro_definition_text
from tests.host.test_trap_syscall_source_layout import c_statement_text
from tests.host.test_vm_service_source_layout import c_declaration_text, file_scope_objects
from tests.host import kernel_layout_source


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
BASELINE_COMMIT = "e5b4da6e2fd33f752cfdadc7beb77f0ebf9b025b"
FUNCTION_REVIEW_LINE_LIMIT = 120
STRUCTURAL_TEST_PATH = "tests/host/test_scheduler_source_layout.py"

SOURCE_SETS = {
    "MICROS_SCHEDULER_CORE_SOURCES": (
        "kernel/scheduler_core.c",
        "kernel/scheduler_core_queue.c",
        "kernel/scheduler_core_validate.c",
        "kernel/scheduler_core_policy.c",
        "kernel/scheduler_core_ipc.c",
        "kernel/scheduler_core_ipc_transition.c",
        "kernel/scheduler_core_return_plan.c",
        "kernel/scheduler_core_return.c",
        "kernel/scheduler_core_accounting.c",
    ),
    "MICROS_SCHEDULER_RUNTIME_SOURCES": (
        "kernel/scheduler.c",
        "kernel/scheduler_context.c",
        "kernel/scheduler_completion.c",
        "kernel/scheduler_return.c",
        "kernel/scheduler_interrupt.c",
        "kernel/scheduler_idle.c",
        "kernel/scheduler_test_support.c",
    ),
    "MICROS_SCHEDULER_QEMU_TEST_SOURCES": (
        "kernel/scheduler_test.c",
        "kernel/scheduler_test_state.c",
        "kernel/scheduler_test_fixture.c",
        "kernel/scheduler_test_completion.c",
        "kernel/scheduler_test_interrupt.c",
        "kernel/scheduler_test_trap.c",
        "kernel/scheduler_test_finish.c",
    ),
    "MICROS_SCHEDULER_INVALID_QEMU_TEST_SOURCES": (
        "kernel/scheduler_invalid_test.c",
        "kernel/scheduler_invalid_test_state.c",
        "kernel/scheduler_invalid_test_fixture.c",
        "kernel/scheduler_invalid_test_observation.c",
    ),
    "MICROS_SCHEDULER_HOST_TEST_SOURCES": (
        "tests/host/scheduler_test.c",
        "tests/host/scheduler_test_fixture.c",
        "tests/host/scheduler_queue_test.c",
        "tests/host/scheduler_return_test.c",
        "tests/host/scheduler_accounting_test.c",
    ),
    "MICROS_SCHEDULER_MODEL_HOST_TEST_SOURCES": (
        "tests/host/scheduler_model_test.c",
        "tests/host/scheduler_model_fixture.c",
        "tests/host/scheduler_model_reference.c",
        "tests/host/scheduler_model_projection.c",
        "tests/host/scheduler_model_prelude.c",
        "tests/host/scheduler_model_operations.c",
    ),
}
PRIVATE_HEADERS = (
    "kernel/scheduler_core_private.h",
    "kernel/scheduler_runtime_internal.h",
    "kernel/scheduler_test_internal.h",
    "kernel/scheduler_invalid_test_internal.h",
    "tests/host/scheduler_test_internal.h",
    "tests/host/scheduler_model_internal.h",
)
FAMILY_PREFIXES = (
    "scheduler_core_", "scheduler_runtime_", "scheduler_test_",
    "scheduler_invalid_test_", "scheduler_host_", "scheduler_model_",
)
ORIGINAL_SOURCES = tuple(paths[0] for paths in SOURCE_SETS.values())
ALL_SOURCES = tuple(path for paths in SOURCE_SETS.values() for path in paths)

# Every original identity is listed, including the 25 retained root definitions.
# Names are family-scoped: the two assembly callbacks and private fixture names
# intentionally occur in more than one independently linked image/family.
ORIGINAL_FUNCTIONS = {
    "kernel/scheduler_core.c": {
        "kernel/scheduler_core.c": (
            "thread_handles_equal", "hart_handles_equal",
            "thread_handle_is_null", "hart_handle_is_null",
            "null_thread_handle", "resolve_thread_mutable",
            "resolve_hart_mutable", "thread_is_current",
            "hart_has_assigned_thread", "add_overflows",
            "bitmap_clear", "bitmap_set", "bitmap_test",
        ),
        "kernel/scheduler_core_queue.c": (
            "queue_dequeue", "queue_enqueue_tail_raw", "queue_enqueue_tail",
            "queue_pick", "preflight_tail_enqueue",
            "queue_enqueue_tail_prevalidated", "queue_enqueue_head_prevalidated",
        ),
        "kernel/scheduler_core_validate.c": ("micros_scheduler_core_validate",),
        "kernel/scheduler_core_policy.c": (
            "micros_thread_scheduler_admit_preflight",
            "micros_thread_scheduler_admit", "micros_thread_scheduler_hold",
            "micros_thread_scheduler_remove", "micros_thread_runtime_flags_set",
            "micros_thread_runtime_flags_unset", "micros_thread_install_policy",
            "micros_hart_pick_ready",
        ),
        "kernel/scheduler_core_ipc.c": (
            "current_ipc_guard_is_zero", "clear_current_ipc_guard",
            "ipc_blocked_flags_are_valid", "micros_scheduler_preflight_current_ipc",
            "micros_scheduler_begin_current_ipc",
            "micros_scheduler_rollback_current_ipc",
            "micros_scheduler_commit_current_ipc",
        ),
        "kernel/scheduler_core_ipc_transition.c": (
            "ipc_transition_clear_flags_are_valid",
            "ipc_transition_set_flags_are_valid",
            "micros_scheduler_commit_ipc_transitions",
            "micros_scheduler_prepare_ipc_wake",
            "micros_scheduler_commit_ipc_wake_prevalidated",
            "micros_scheduler_commit_ipc_wake",
            "micros_scheduler_commit_ipc_wake_pair",
            "micros_scheduler_commit_ipc_call_delivery",
            "micros_scheduler_commit_ipc_reply_receive_wait",
            "micros_scheduler_commit_ipc_reply_receive_delivery",
        ),
        "kernel/scheduler_core_return_plan.c": (
            "scratch_load", "scratch_remove", "scratch_head", "scratch_tail",
            "scratch_pick", "micros_hart_plan_user_return",
        ),
        "kernel/scheduler_core_return.c": (
            "plans_equal", "micros_hart_commit_user_return",
            "micros_scheduler_apply_return_plan",
        ),
        "kernel/scheduler_core_accounting.c": (
            "micros_scheduler_accounting_initialize",
            "micros_scheduler_account_user_trap",
            "micros_scheduler_account_idle_trap",
            "micros_scheduler_account_enter_thread",
            "micros_scheduler_account_enter_idle",
        ),
    },
    "kernel/scheduler.c": {
        "kernel/scheduler.c": (
            "micros_scheduler_initialize", "micros_scheduler_is_initialized",
            "micros_scheduler_admit", "micros_scheduler_prepare_start_timer",
            "micros_scheduler_start",
        ),
        "kernel/scheduler_context.c": (
            "map_object_error", "authoritative_objects", "copy_user_context",
            "clear_bytes", "validate_selected_thread", "load_selected_context",
            "preflight_kernel_interval", "activate_prevalidated_root",
            "enter_selected_thread", "panic_scheduler_invariant",
        ),
        "kernel/scheduler_completion.c": (
            "preflight_selected_completion", "commit_selected_completion",
            "panic_completion", "preflight_completion_or_panic",
            "commit_completion_or_panic",
            "micros_scheduler_test_rejects_malformed_completion",
        ),
        "kernel/scheduler_return.c": (
            "panic_invalid_context", "select_user_return",
            "micros_scheduler_select_user_return",
            "micros_scheduler_select_captured_user_return",
        ),
        "kernel/scheduler_interrupt.c": (
            "micros_scheduler_user_trap_enter", "handle_timer_result",
            "micros_scheduler_handle_user_timer",
            "micros_scheduler_enter_supervisor_interrupt",
            "micros_scheduler_handle_supervisor_timer",
        ),
        "kernel/scheduler_idle.c": (
            "micros_scheduler_idle_select", "micros_scheduler_idle_accounting_panic",
        ),
        "kernel/scheduler_test_support.c": (
            "micros_scheduler_test_enter_without_timer",
            "micros_scheduler_test_prepare_supervisor_return",
        ),
    },
    "kernel/scheduler_test.c": {
        "kernel/scheduler_test.c": (
            "micros_scheduler_runtime_run_self_test",
            "micros_scheduler_test_start_production",
        ),
        "kernel/scheduler_test_state.c": (
            "thread_handles_equal", "copy_bytes", "bytes_equal", "clear_bytes",
            "read_satp", "fill_context_pattern", "user_address_of",
            "current_thread_index",
        ),
        "kernel/scheduler_test_fixture.c": (
            "snapshot_start_state", "start_state_matches",
        ),
        "kernel/scheduler_test_completion.c": (
            "completion_state_is_clear", "observe_completion",
            "expect_no_message_completion", "expect_message_completion",
            "completion_test_mismatch", "user_registers_match",
            "micros_scheduler_test_after_start",
            "micros_scheduler_test_after_user_return",
        ),
        "kernel/scheduler_test_interrupt.c": (
            "test_uart_interrupt_enable", "arm_external_interrupt",
            "run_supervisor_external_test",
            "micros_scheduler_test_handle_external_interrupt",
            "micros_scheduler_test_note_selector_entry",
            "micros_scheduler_test_handle_idle_timer",
        ),
        "kernel/scheduler_test_trap.c": ("micros_scheduler_test_handle_user_trap",),
        "kernel/scheduler_test_finish.c": ("finish_test",),
    },
    "kernel/scheduler_invalid_test.c": {
        "kernel/scheduler_invalid_test.c": (
            "micros_scheduler_invalid_runtime_run_self_test",
            "micros_scheduler_test_start_production",
        ),
        "kernel/scheduler_invalid_test_state.c": (
            "copy_bytes", "bytes_equal", "read_satp", "user_address_of",
            "fill_context_pattern",
        ),
        "kernel/scheduler_invalid_test_fixture.c": (),
        "kernel/scheduler_invalid_test_observation.c": (
            "micros_scheduler_invalid_test_before_user_timer",
            "micros_scheduler_invalid_test_after_user_timer",
            "micros_scheduler_invalid_test_handle_user_trap",
            "micros_scheduler_invalid_test_report",
        ),
    },
    "tests/host/scheduler_test.c": {
        "tests/host/scheduler_test.c": ("main",),
        "tests/host/scheduler_test_fixture.c": (
            "handles_equal", "context_pattern", "setup_fixture",
        ),
        "tests/host/scheduler_queue_test.c": (
            "test_priority_queues_and_tail_order", "test_runtime_flag_zero_boundary",
            "test_policy_replacement_is_atomic", "test_corrupt_queue_state_is_rejected",
        ),
        "tests/host/scheduler_return_test.c": (
            "test_current_selection_and_preemption_repair",
            "test_quantum_rotation_and_nonpreemptible_current",
            "test_current_block_and_stale_plan_are_atomic",
        ),
        "tests/host/scheduler_accounting_test.c": (
            "test_accounting_owners_are_separate",
            "test_trap_accounting_uses_minimal_route",
        ),
    },
    "tests/host/scheduler_model_test.c": {
        "tests/host/scheduler_model_test.c": (
            "next_random", "micros_scheduler_model_test_run",
        ),
        "tests/host/scheduler_model_fixture.c": (
            "context_pattern", "setup_fixture", "initialize_model",
        ),
        "tests/host/scheduler_model_reference.c": (
            "handles_equal", "handle_is_null", "queue_find", "queue_remove",
            "queue_head", "queue_tail_without_preemption", "queue_tail",
            "pick_ready", "admit", "plan_and_commit",
        ),
        "tests/host/scheduler_model_projection.c": ("model_matches",),
        "tests/host/scheduler_model_prelude.c": ("run_required_prelude",),
        "tests/host/scheduler_model_operations.c": (),
    },
}
INITIAL_SYMBOL_OWNER_MIGRATION = tuple(
    (original, final, name)
    for original, owners in ORIGINAL_FUNCTIONS.items()
    for final, names in owners.items()
    for name in names
    if original != final
)
SYMBOL_OWNER_MIGRATION = ()
INITIAL_FUNCTION_MIGRATION = (
    ("kernel/scheduler_core.c", "micros_scheduler_commit_ipc_transitions"),
    ("kernel/scheduler_core.c", "micros_scheduler_core_validate"),
    ("kernel/scheduler_core.c", "micros_hart_plan_user_return"),
    ("kernel/scheduler.c", "select_user_return"),
    ("kernel/scheduler_test.c", "micros_scheduler_test_handle_user_trap"),
    ("kernel/scheduler_test.c", "finish_test"),
    ("kernel/scheduler_test.c", "micros_scheduler_runtime_run_self_test"),
    ("kernel/scheduler_invalid_test.c", "micros_scheduler_invalid_runtime_run_self_test"),
    ("tests/host/scheduler_test.c", "test_accounting_owners_are_separate"),
    ("tests/host/scheduler_model_test.c", "model_matches"),
    ("tests/host/scheduler_model_test.c", "run_required_prelude"),
    ("tests/host/scheduler_model_test.c", "micros_scheduler_model_test_run"),
)
FUNCTION_MIGRATION = ()
SHORT_PHASE_ROOTS = (
    ("kernel/scheduler.c", "micros_scheduler_start"),
    ("kernel/scheduler.c", "micros_scheduler_idle_select"),
)

STABLE_ARTIFACT_HASHES = {
    "include/micros/scheduler_core.h": "62976539f042a654003034157beefcefe25f90a5e92247461fb1fe3f704044f1",
    "include/micros/scheduler.h": "3852373ba41e8f81ab94612c7b39de61b50968408eaef46e905ef5f15a79519a",
    "include/micros/kernel_objects.h": "106fe5aaa03b06b1746c73aa24588749d4cfe02c6b1e4a11efc1f5317f7523e3",
    "include/micros/user_execution.h": "6749ae47d60aea26368efd27b76468b4693e3c70e809d186c1491b9a698e7cca",
    "include/micros/timer.h": "6f4aa2c00c0d800139e736a4d4432a3b563fe048ec54cb61ea124d69c4ae0743",
    "kernel/scheduler_core_internal.h": "c9378e2e926b28400c474a35f6eb102aeed017a01fe1d367bb1f33687974bfb6",
    "kernel/kernel_object_runtime_internal.h": "23917cbd674d926ec7ffe8700747fc4626f3a31dc4620ed4f7d4ee8147f57c72",
    "kernel/ipc_buffer_internal.h": "91291137fd7977c7004fcfd18412de3edad489d1d2124bfd182988b5ccf80f1d",
    "kernel/kernel_objects.c": "453c3cfbfb02d1c014226a315106a157676a7b195be06f561bb3abbda042db4a",
    "kernel/timer.c": "b664019fb42c480719882865dd7ab8d3e0510f4599b0e7da8e839e1f984f76d8",
    "tests/host/ipc_current_guard_test.c": "b023a05e9f0bd003383aa8e85901ec5215163136b9caf88c329ef9b2e8023220",
    "arch/riscv64/scheduler_context.h": "3b854415cf846f8c940afbed956a975651b44a6630fcd77eed7470150beb81ae",
    "arch/riscv64/trap_context.h": "584a45235e8eea9a973ccaebb0ace4b84f22778f360625a68feecf1dc276e5d3",
    "arch/riscv64/scheduler.S": "4c75823725dc68cc8e9b5e5d7051946e2418b322014a6e3e5fe1d72df5bc85ea",
    "arch/riscv64/scheduler_test.S": "59294154d903d9a50e66a9d40a6cfc5ffcdeac13c451a46d5f83dd2b4434970a",
    "arch/riscv64/user_entry.S": "7dca2e414e36db2851f7f7ded760fd78ca8da0a74b9fee677aee8699b7f6d9d2",
    "arch/riscv64/trap.S": "e541ebbc7c1ab6e68eb366ec9ea353bc381db55364b999c7516e30a56c96666d",
}

# Literal frozen function/body/signature/guard and semantic-fact inventories
# are inserted below; no expected value is learned from a current C source.
FROZEN_FUNCTIONS = {
    "kernel/scheduler_core.c": """
thread_handles_equal 10 987b6596eb05092463991a644a803d82c67265da530a82b789691109b81b41c5
hart_handles_equal 10 987b6596eb05092463991a644a803d82c67265da530a82b789691109b81b41c5
thread_handle_is_null 6 b7f627da19f4c74a6234972f09c36de23be34799ed4e861da861943be4fc4ffe
hart_handle_is_null 4 b7f627da19f4c74a6234972f09c36de23be34799ed4e861da861943be4fc4ffe
null_thread_handle 6 a63998f5b84a5022ec628b14bc5b289e8cd92e38afbb0b0ce9e9efd1c0fa901c
resolve_thread_mutable 19 a59b069c3e9acb539e4991b13b7b1d48f252600068df5ed38c589f831d98f515
resolve_hart_mutable 19 dfb8d274accf7f124538ea7bb0556c00d5994b36eb51c2071288444a3383efc4
thread_is_current 19 d97cfdbbeceea952d1fd31f44c0a503645de08aa0788013fcafee3f7c7d9da92
hart_has_assigned_thread 23 44bc92385a4fc8087b335df41ef17a5f0da2488a69bd63ad215f9716063bc7f2
queue_dequeue 67 e8f45a9b23bc16a1dd868de2b7c8026cbb928eb270faa67a4379bcaad8c3aea9
queue_enqueue_tail_raw 22 542a432b4e4bf44ed8b8f5d03c16ade924f88fc700cedaf4743b9e40cc43d241
queue_enqueue_tail 41 6883496d82cc921f7ebffe5b554c458f3392f47a4b4165d5f159227871590f14
queue_pick 17 2fde2d15644301681675caf9fa11408b844de4eb9add0b4612994700f0445277
preflight_tail_enqueue 29 966da1f659c3087e04cef66f943f59c5858c10d4b6c866d067383deb5b7da61a
queue_enqueue_tail_prevalidated 44 c899a6feeefec51a971fd17a12b3c1443706e4ba78440b2a426d290b2be12364
ipc_transition_clear_flags_are_valid 13 64e75b2c64b461e364f285074e02dc637d08ac6c0215513c5b3330fb1e45fad7
ipc_transition_set_flags_are_valid 8 eb54e5710dc4368929f8f23380f72e441918da547d39bb35bdbb6d53bdc3505d
current_ipc_guard_is_zero 16 ddedb653337e94dfc3313ef342df0f8cccdb1498ef7cf8a0a60fc7f7c22320ba
clear_current_ipc_guard 11 6b994018964182eb32db38b2251528582e6679f7bb2ad63f1c2d82712be09d80
queue_enqueue_head_prevalidated 19 8c705ff53569ad49e08613bd90b36681f64db411d80415918ade169f856fccbc
ipc_blocked_flags_are_valid 13 bb37c03bc259f8935b9e88653fe606dde6e49ac59a8ab3950b56a256105ca3c4
micros_scheduler_preflight_current_ipc 54 e577e00932b0a0d66622e734704ca6ca955880db6c28a1c207316d0b1d256466
micros_scheduler_begin_current_ipc 52 e68606697dcd859dbedaf8180f91d2cff56995eddfa3ba315aa725e8dacba024
micros_scheduler_rollback_current_ipc 57 3edb06e155de8ef549f1f5e42cdbf17a74399f1d88efcea5cf14804af11d7b86
micros_scheduler_commit_current_ipc 61 574b3b65ce50c8c0138ea699618bddb2238cb877149b0e42f90850a7218388dd
micros_scheduler_commit_ipc_transitions 128 fa79dcdefbf7d9bed4c512be8ee5281ed9c53f730b6f4985cad2e8f242277b37
micros_scheduler_prepare_ipc_wake 63 ff86f3d7a395cbdde1c6866b95975d8b0400299d68dc1d36f1edafc4d60bf8aa
micros_scheduler_commit_ipc_wake_prevalidated 18 3a9c803c1e2a59de7bb477a1d9322f3828bf1db1ef5c010a840d62118ad952c8
micros_scheduler_commit_ipc_wake 21 8b628c845899b253960c151fb7b7cdcfaa7106f0b7ce631f19e9bfbbb2451f7b
micros_scheduler_commit_ipc_wake_pair 27 d6cf27c675a2df4e498b9b585e36444eaf4bbfa0637a4570451d0e6ee2a94821
micros_scheduler_commit_ipc_call_delivery 25 30fdca40efdd71ba10cff04037a33447a797eec4085c3f41f1a8618c2e482ef8
micros_scheduler_commit_ipc_reply_receive_wait 26 d59831053e971f270e76f8efa0d69dfdab3ed6748c99e1135cbd3512e2a0194c
micros_scheduler_commit_ipc_reply_receive_delivery 32 af54223ac608c8c6806b8c9b19634eaab6b0191a715884396df8b1b08df9710d
add_overflows 4 8f2d13f4d95fed145bd156742c51c3cfd82c2d8e8cde09ae4b7a56c45eaace38
bitmap_clear 12 33bbafa0144aa1f0900903509379b113aafa78bd8ba9ab73f8c5849bbdfb2159
bitmap_set 4 a6f00ceffffedf1a83995250c54f5551954ed6bdbdfa7b05b6332426c6feda9f
bitmap_test 6 0ae195992224faaa46c9b2f3197285763467c07faa716b0539173434b90b51ec
scratch_load 41 95d329fcca2a1a467bb6e5bcb6e086f65d01bea98221871c351bfc157b0e5918
scratch_remove 16 4a44517b4c0170b48345a62036adfb469c49a5f8c4ca6741ee79b583b57988b4
scratch_head 12 1636b7affd9f5c3546c799a050bb1ff31294268477d22f2295667535f4298702
scratch_tail 6 0af73f16458ebe4a449b6d20f383e0cdb66165dd4b443f331ed1a7df038a335f
scratch_pick 15 c0828ee9617e7e87c5acd7a1cf124e2c92f8151c8c15caf2516c44748eabec91
micros_scheduler_core_validate 272 e1b7a8091f16cbc520b168d5c5b794b915c44f15fd2ea874b092041e16332f9b
micros_thread_scheduler_admit_preflight 60 e471c2f78c3a805d053e559227827d38ae0fda187fdb446dc4e3628ef611679b
micros_thread_scheduler_admit 41 61c9523d92a530fefea52929ad56b69da23dc0fc01feed251df6053fd4eba983
micros_thread_scheduler_hold 50 427b1fda160ac089e14c31bc935572ee1e9159703f652d0646f884a5255edd2e
micros_thread_scheduler_remove 35 59458427604dfa94eea61c9531f9a5adeb50cd638d173ad02fc2f1de70c45e03
micros_thread_runtime_flags_set 56 e0037c3bfd2959299218ae94870c5cbfc576ebbde5763ddf2468e74c7f4c3849
micros_thread_runtime_flags_unset 65 56ff5e27478cd631a39631a71300b48e51b62cda5e4f64b3d31b2ef3185af77f
micros_thread_install_policy 68 85db21e6d73f4da24aa3acb9243784b3e959bdeaa0e1c15bf586d4276eea35a6
micros_hart_pick_ready 28 5a6c81c870fd3a7d0882d5e71422b5cc33369d3bbc480338d5a77f9ae9206ddc
micros_hart_plan_user_return 185 05cdc17a7f3784c2dbd2337304ce96afcee55dda5d880482b472e3c7b0192ee6
plans_equal 55 408909701c4553aaa1ef39b1ee367c298b384486c27d264a38e723651ffd1a4e
micros_hart_commit_user_return 25 d0ee17d2e4f448226de3f3748949d56e70c867df3e3c42e3a0e08737fad46387
micros_scheduler_apply_return_plan 59 9fc4fd22602ea1820b072c8e02d94380f0aff82563519bd1de1f6f5d28241a11
micros_scheduler_accounting_initialize 29 612382cd038ad2f2c9655bdaadad7dc417f89a5d0d9b020f583b45aa86f721b3
micros_scheduler_account_user_trap 64 63287bc0f77aa77a11a06ee1adbe1250bc20f58aa7edb71759b72e43c028b8a7
micros_scheduler_account_idle_trap 40 08631bdd00f5558155639a6259438af015b4096e6b58a8c6d0c99307c5964bfe
micros_scheduler_account_enter_thread 59 376ed26cd7639d361391ea44b23a6515206152332712d2222be17f4a24a0aae9
micros_scheduler_account_enter_idle 35 fbba48714cf84f637efd0868152360cf58b244dc947b6b505c634c262526510c
""",
    "kernel/scheduler.c": """
map_object_error 23 0b1c40a16b2d272063c4f7d51bdae713ceb10628c5ffeb9f799891304dd7cedf
authoritative_objects 4 a3048b5eaa701a85ce0fe295cf1b2541b273d26a17366cca0650dca33a759b12
copy_user_context 13 514fcf08b1f7e0a6489c4bd45fa6739848555d4eb872c8556c9cae829b89780f
clear_bytes 9 83b5eca7616629894536d08ade1222c4b8a3311c2617fd326670f8bec4f73b11
preflight_selected_completion 59 f8d9b00c6f09c6785375fdb7827ca85e7a01c2c9d38f94ca11fb65edc7144480
commit_selected_completion 30 fae5328f48c93c37f8fce926cf761f6746d5c831585bd9a96c35f4ad49e4dd27
validate_selected_thread 31 5edf877a04b18c367d5ea538cfaf945cb25b2821d7311adadb8c66e9b113063a
load_selected_context 17 d5464a843a7f3ba39392b5dc16a74d5c56283d1ba4a0b3a723dc9446e49c11fb
panic_invalid_context 28 da00a1e86bdda98244290ab12ff4e5970ee2db5fac4a97fc7d3bf4b0e13317fc
panic_scheduler_invariant 12 23b74e1b732e114e31ca4592c43a4cc0a3d8f979bbedd495d4b2bc7c0006e392
panic_completion 14 da319c336287803fd17f2e1ca2207d840da81eb0f16e93ccbe093d22ff73fccc
preflight_completion_or_panic 19 7612a7b1bc94c63d3eae23ba60252daa263f44fdbd27740334f4ef9762a533aa
commit_completion_or_panic 13 bee4a9be8b227b9f5997bc6c5a671692ab4895300ca252484c3b550870feb054
preflight_kernel_interval 25 cfc92c9bb6d5fbe826d41fb11ba550ee3f758994c813ac9192fefd6bbc66c4ba
activate_prevalidated_root 24 f5a42bea64d1316827c3e843d2235be0e780cc23bf417fb1db75ef41fad99076
enter_selected_thread 35 8d49fc416842033fd54f7de7e89027c87f572344b7aba9b74422c94f5c1acb2a
micros_scheduler_initialize 48 6796ad103d92459b65e825bc9eb21f04cbc03da0486f79f6fae9bde64ce86083
micros_scheduler_is_initialized 4 75eef00291e4add0c880632d32e6fa9ba9c6331c6e4f2896d57da5131a58a893
micros_scheduler_admit 40 44d818b6a185514bdc3f13a384f7357d96a89a220b2c7532efda970defca4593
micros_scheduler_prepare_start_timer 30 32ddf1f30cbb5cc30321811113069441762b4941884f418918605a72d5f852cd
micros_scheduler_start 106 92a27c9d91ab905c4d8a7054f00e3dea6da7eb374378795e51c3f893b768c459
micros_scheduler_user_trap_enter 31 efa96a3d90aaf0259f7ec132462da0e3e95993734d9aa99258e75e7b8c1a85db
handle_timer_result 17 8f0a0121b95cbf5b7a8a56014e80a256c42f4a6e09f0b9f3b0be7697006f184d
micros_scheduler_handle_user_timer 17 d1a3ee7246e2b7dd2bdf065d077f7f35881d684dd2594dacbf3941ef04445d27
select_user_return 137 f65d6479baec4cad710cdb9989a5940c6bc905c4223654f77ba3313488cbeef5
micros_scheduler_select_user_return 7 e8092cf877bc63a7a665e27576e7fedb18684ad1bb3ac29bff0b02b0f7f64ff0
micros_scheduler_select_captured_user_return 8 6e6f823031d94f8035a4dd06be3e8d90d33e2f14092c83a89655175f32f0a892
micros_scheduler_test_rejects_malformed_completion 16 0d42af87f48891f2130ea9e19994da4ff2268210dfd9abc19638107fb6112437
micros_scheduler_enter_supervisor_interrupt 33 2959661e3c0b608a541090119a2a41b2c13a7d9f204c6e319b7c07cb8dfbd646
micros_scheduler_handle_supervisor_timer 32 1cf5339b5ca4f686e51f6a2e8f08bdda64333fd3f3e464f5f679e8e34994d442
micros_scheduler_idle_select 90 ef61abfc8aa8dd62d2f365ea12c5d0b39e8c84f3c8e02e37aae6cab08e762960
micros_scheduler_idle_accounting_panic 9 9aa878a34c9c90aa9625f114eeeecef1e05b57e9b690fcb209bf13d92e22b848
micros_scheduler_test_enter_without_timer 67 dfcbd35ef16da713d1ce870594bcb1b3966b6aaf7f77ea35545c6baac5a1e85e
micros_scheduler_test_prepare_supervisor_return 57 1c1571d80937816ad40033f6d07742c9904fae08156ec63c199d08467b4f1e38
""",
    "kernel/scheduler_test.c": """
test_uart_interrupt_enable 4 a1a8e89bfd34bd14931e1f443e3d768df0f688071d3898e2f831c698eb38a410
arm_external_interrupt 25 b40a713ce55786f1ac808313691773b7dd8a55dd1f19ecba76e6816b0122426a
run_supervisor_external_test 30 ffd2c31b989b3eefa04e64bfcdb23252a2829beb72b86c618ed5364377b11513
thread_handles_equal 10 987b6596eb05092463991a644a803d82c67265da530a82b789691109b81b41c5
copy_bytes 10 f362f9eafdc31b89c91d63f627b5b7677d1ab2db6db613aa5dd125418b4936bf
bytes_equal 17 dcf8caf71d4c656e520f018c619ddde5003695c1150ef8ad1ccf89d0222a3b5a
clear_bytes 9 83b5eca7616629894536d08ade1222c4b8a3311c2617fd326670f8bec4f73b11
completion_state_is_clear 18 48cc16c0814d31c8a3f99b49b4cff96c1bfa09dd077427cd3a4d674f409c657f
observe_completion 60 21ad2e1c29e0c72edbe129b6921cb82a5e534785ed3a6ebab94018cbfd65942b
expect_no_message_completion 39 c74ecf31fea8453bd24ca31ebec80338aed10ea2ca56fe703eb441fac4612875
expect_message_completion 54 e3ab2f5813fd7bbfacb58686ecdfb9bd46fe41001a3b2e74ecef429f2461d923
completion_test_mismatch 9 434714833ad71cfbac840d322a12283ecaaa4899dfa32a921c5a7820c298f96b
read_satp 7 0de88dc898ea21122bcfeaed7f43947f860fb687d915fe7deac2183c94f125c9
fill_context_pattern 19 901a8f20802242769ac247e9a92abf47a4b5debce7c92eaeb42fde678315435c
user_address_of 8 ad07a9aa920eeb7fd95c3fc173385d9d4067fc16d4dfe1dc0f0c9cfede73e243
current_thread_index 18 ec1c2c0953fc76d406361f41a798419aedbd608e56307e50bafc25b065244b8d
user_registers_match 59 90b582d1fe0e68e056cb70527cd7951e5b7223b16118ff49bb7837cdd02c470f
snapshot_start_state 14 85d832bcf5547d1b1759057ea6b9867f0e434c5caec3671e26d33f05c7723c9e
start_state_matches 18 1fb6521b5491660853d793f5766df2817a7f453e0593bebaa1dad16cbcec773c
micros_scheduler_test_after_start 26 eedc5a9444ea68a9668c12fba4c948d064bc34a652faf04f1fa7dd0d3a0b989f
micros_scheduler_test_after_user_return 47 40e51fba3ea76de8c03f1a3cdfd15bc3349987f05eb4ef7d7bf5900c220b7f8d
micros_scheduler_test_handle_external_interrupt 90 7723abfccdb6debee40fe9bfa564980673f83e09ca97939bbbd8564366841389
micros_scheduler_test_handle_user_trap 232 eed7181d4d6d53a30faa298c1be088b8f44d9b3eccb2d2f62f39955e5af23b92
micros_scheduler_test_note_selector_entry 4 c64f64b7b186dbd072c3208395b6fe274ff92d1743757fac45e640f5a5920468
micros_scheduler_test_handle_idle_timer 40 73bdcc83eac5d2a394741f645e0ed5c3da5d0a32154152d78f421c6cea32d4f4
micros_scheduler_test_start_production 16 80cdab3f74a9d7abd479542c31dc3f22e7ede1b7a094c3da97df6478f0d62463
finish_test 144 0e4069ad09385e3933bfd06864842940cfde16600eec5e544d92da2d31197436
micros_scheduler_runtime_run_self_test 204 11b68f50b0a27824bef49e12a20194d04e0cd7614d297593936ecfc3a8d3a2d5
""",
    "kernel/scheduler_invalid_test.c": """
copy_bytes 10 f362f9eafdc31b89c91d63f627b5b7677d1ab2db6db613aa5dd125418b4936bf
bytes_equal 17 dcf8caf71d4c656e520f018c619ddde5003695c1150ef8ad1ccf89d0222a3b5a
read_satp 7 0de88dc898ea21122bcfeaed7f43947f860fb687d915fe7deac2183c94f125c9
user_address_of 8 ad07a9aa920eeb7fd95c3fc173385d9d4067fc16d4dfe1dc0f0c9cfede73e243
fill_context_pattern 19 901a8f20802242769ac247e9a92abf47a4b5debce7c92eaeb42fde678315435c
micros_scheduler_invalid_test_before_user_timer 28 f6a4a4e0b264b5cc4f18b7b2e1f36658529a8f5905ef97615d561bd60efe8832
micros_scheduler_invalid_test_after_user_timer 39 119a9c8d63e13f6d7a1fa1e8f07e5e9d22efdfb98bf291293494f1a28ac1986a
micros_scheduler_invalid_test_handle_user_trap 33 c0e80f8460f7dc6a7ba0d80eff71236890ddfe18aea86d41839b252e8c07718c
micros_scheduler_invalid_test_report 76 526adcc4c2a36e158ee4634e280ee622955372a51552927ad066b869e881c98d
micros_scheduler_test_start_production 16 80cdab3f74a9d7abd479542c31dc3f22e7ede1b7a094c3da97df6478f0d62463
micros_scheduler_invalid_runtime_run_self_test 125 aee5f0f78284b8b01c9006a584e79db3bac370fd7d90d86b1e563520703be93d
""",
    "tests/host/scheduler_test.c": """
handles_equal 10 987b6596eb05092463991a644a803d82c67265da530a82b789691109b81b41c5
context_pattern 18 a7b9a6443d2c066ebda5c73f65d97cc8c20cf1d385e3d5119a2b5a6dd83fcbb7
setup_fixture 63 33442a9a59d683ae6fd7be41d23918cd4268aa4cc5dec4f7c80e9dab13aebdf5
test_priority_queues_and_tail_order 97 a4567a1e1b220467d5366cb7fc85ae091cf78e738289310106396bdbfa67a019
test_runtime_flag_zero_boundary 85 f2e9e29437b7b2fc97c7fc4190f0322dd87bfbf0b3f3cee0461d5c7796026e95
test_policy_replacement_is_atomic 74 9e5ca58a92fc7559c7166bc9d50fede4f2acdbb79f0ab87a3d93c4ce9203314b
test_current_selection_and_preemption_repair 98 b4e37cdd529c1203d0b38817d28997e4b0df94367f129effac3c8b5a7b7e4b17
test_quantum_rotation_and_nonpreemptible_current 98 4498389685c33b26beb9ecf1c57f883d697cc8f3400a7c241447d67fd6d18d68
test_current_block_and_stale_plan_are_atomic 88 31dfd7856f1adf1386824f2860c7cb2377c43ff41ef456a94eea5688c2487900
test_accounting_owners_are_separate 176 df75bead2f3a7527fce58901db1695f330af0263593b7e58168d21a7d87f3f69
test_trap_accounting_uses_minimal_route 102 bed69a841478b9aa39e9750bfd0e22e6d996b6b4448ce29e067674b8246636cf
test_corrupt_queue_state_is_rejected 80 becac50b6c0fd6dba1381e9d16c2af15c6f5c2b92ea960789d40450368d5b69a
main 67 5cdf47be5a0ace6531aa596deabec85bd6670e65d3c322fd8f92c3ab9571e9e4
""",
    "tests/host/scheduler_model_test.c": """
handles_equal 10 987b6596eb05092463991a644a803d82c67265da530a82b789691109b81b41c5
handle_is_null 4 b7f627da19f4c74a6234972f09c36de23be34799ed4e861da861943be4fc4ffe
context_pattern 18 a7b9a6443d2c066ebda5c73f65d97cc8c20cf1d385e3d5119a2b5a6dd83fcbb7
setup_fixture 62 1b2a8d8cd1041384008a01ed633e3a575885ef3758b11db16e05ec043605ba93
next_random 5 b7f1e0e758da23743191c85e72e0ee2b499d0e410dc70c153c7c16eeb6c16ab6
queue_find 20 c5ae50375437738bed41e61295c029ad8c13f0e99dae5590c2448cc8582c040d
queue_remove 22 d6dafa2bed73a0923f5663590f7706aeab402ddedb6a7283766c5d9e8634b106
queue_head 17 26dad1068188fbc9317c4df5b37d0e9def03826c62e7f4f9d5c2f099f943a1cf
queue_tail_without_preemption 12 dfc563e84d157ecefca4cc5d8608bc07e8d6376abccc0469ae83ff37f2722c86
queue_tail 33 bb9b1f68374e50ea8fd8fe1e1c3fbc595e7c491e9197f02ab75e7f86af7a94e5
pick_ready 18 f4326f6b061c4f4e31ce4c7b757d546f9975b419997c4eaf1fc6c8a27063019c
admit 31 3ccd1d8239ab5c5f58f8eeb8dd43b8343970885968a1147e0d0ce31504c1e65e
plan_and_commit 110 0138b821405eed473467dcabcc63f7639ac519f36561fa115b08a554ee114cdf
model_matches 181 35a1205cb2e765fca3e6323450858cec6e352003e9ecc084e3d243b9f046bb0d
initialize_model 25 a52e4824c32f6ee5c63dd0b1b9d48f1a49c80303744827a969829bfae444833d
run_required_prelude 253 568bf9ee571fa3edf8d0f0359d2c04d9d45b23b38838caf1eaee7df5c55124b1
micros_scheduler_model_test_run 416 c784366702c90829a6d45cb003d8cf292d8243c64ceac726de34d0fa1ffe83d1
""",
}
FROZEN_FACTS = {
    ("kernel/scheduler_test.c", "micros_scheduler_test_handle_user_trap"): """
Cc627088628b1dffcbb732d0f C340dff9652d89b162d4784a4 Cd6cceba52fa48275a78cae8b C54650025e83b43b0e5a218b2
C7dd81804aee58792b7ac4b41 Ca9201fb2f9de6fba103dfc82 C54de1189bec5867fac46f2d0 Cb0fd5fc976b5a165b65305c5
Cac98dd40626138361efac51c C0bd1ae7c4a0d9bd15092526b Cece9307890a5a26179a99f12 C958b708a4a550f9f846360d3
Ce87726726023b1d0a712a2b1 C926fb47540d485c66e102ec2 Cc2ac2f51f28fa34a9dc1c326 C5ae0952f8cfd94ce10d428a4
C664f26466fbdfdd31433795b C448a21f58d65f632e8d67bf3 C6318b3adaab50d45d4973be7 C3ef012ea7a8a997cb9cca3c8
C570d79daa628a3b9f11d51b4 C2b0cb928d22acea54217e50c Cd7173a16fc35ea881987d175 C09a448286755d5cb853ba1e8
Ca9f3a511d936b5285b2d8d7d C3baf62bf088b7edb64c36698 Cf5d780a424cd0be3dab2cec3 P235cc1249dcb4a6c1dd77435
P42f83dbd4ccd6f2d994b75ae Pf89f352956ec3365637d1611 P4ca3fc1d4ee72358f1a0a50f P5fea47c2e920d66604969667
P8325087518e3879e76ca1e9b P0c735dac843eb3647a817bc8 P96d58450420635b5d5713500 P9da81f60f0dfcfcd9e93d08b
P12c2b888bb052bdce03ee68e Pbe7267e6799c95809ff204c3 Pe36706ed7b00a26214947cba P7727f18db32b84493482812c
P5f8a649eb05662cdc9a9f0c2 Pf38c9957d272aa96f70ef168 Pe99534b627b9adfe340614a8 P92fe893a939f70305ee14e38
P082ce604950ad5c756ac2fc3 P1cff74285ef4ed61ac2f2618 Pe1a0f919c530f067615f542a P593c72c0cf6db002a3205014
P8f7ca48901cb9216b4361953 Pfacb797f15f2235cdf0d8733 P95166923b87d3671ebd90620 E4ba1bea22bd91ee511364ab6
E4ba1bea22bd91ee511364ab6 E4ba1bea22bd91ee511364ab6 E4ba1bea22bd91ee511364ab6 E4ba1bea22bd91ee511364ab6
E4ba1bea22bd91ee511364ab6 E4ba1bea22bd91ee511364ab6 E4ba1bea22bd91ee511364ab6 E4ba1bea22bd91ee511364ab6
E58b621506b25becc99d9efff E58b621506b25becc99d9efff E58b621506b25becc99d9efff E58b621506b25becc99d9efff
E58b621506b25becc99d9efff Ee62e99292b992f31ca7c7f13 Ebd5de497492842a964d7f2fa E59c5ee0b1ee3aab54abdc722
E8d828a270d15c1812facb4ed Eb41a9cb25359d73bc95759c5 E842183465d4be23121ee4410 Ef988ca1472889cfe856bf5b8
Efe424677fea90f0331c205b8 E646212092ac4f3aa09fa03d5 W4828eecabd690a634ad0cce9 W671fade9908ed997eaa6ec66
W4e009614e08ad1184418c594 Wcd84e0cdd1242f3b1def0c69 W0ef22c2a6af00c7df7435ccc W7f2431a2c22b314ee933a786
W511b8e0e86a2d274d3009f75 W5f90184147fc290c1c1062da Wf3f250f06141a40ee8dc2b2c Wcd27f112bb1c8adfc698f405
Wb95036f9c87012e4ac3bd390 Wd44d7c7b0a4ca48adb05a78b W96e9bd7387a524561f448939 W427ffb9af8b14ec1873cd70c
Wab5604c4a47b1ecd1c009a81 Wab5604c4a47b1ecd1c009a81 W9074535ba3ea6ee210881f89 Wae95d7a7bd382a11ae9325b0
W374cbe3a423d9761cd22c307 W5df436c6687895f2ab9c7041 W25c6e6193a04a96441549aa7 W89b8279cec0127fd6a52e0b4
W25c04aaf0478c857ddd0620b Wcf6bbf55714b862d7c3b71ca
""",
    ("kernel/scheduler_test.c", "finish_test"): """
Cd9cd058ef7a26985da98fbfb Cc627088628b1dffcbb732d0f Cb7fa5fecbd428b538d5fab91 C66d1a85fc8c13ba202cd0fdc
Cb64329aebb7c319e03dce003 Cf604032daae74090babf18ee C322e77d54486edd9fc807003 C18c3c26c4a41589254536501
Cef954e415fd93dc3e1b91cfd C29f47010836a7f807989dff4 C1a5a32704c1bb020403d306a C7235d415f3ba251ba545f094
Cdb8288078a105a341e915c3f Ca611d98330a07c7148b538a2 Cbf508b0b4cfa725204c16e5a Cf3347f8a66163cf70790d202
C392a1d36005273d7d70435b9 C4311a14161fffb4a63f6fe03 Cf87514268a47d76ae8fcb83c C3f13d9fbbbc15fd397ced04f
C3f13d9fbbbc15fd397ced04f C61eef30f7dfd58b5a6fabdfa Cdd623cf252ff45fa5b635d80 C0554f8f2f4ad863352dbf736
C4074ec1048501d56272bf2aa P5bbadcfae93f45866290e6ff Pc03eb40098fdd874024322e9 Pc9f43b35f1d487673ccb0526
P4e59d0f0145608e2e874f8ed Pef046c41c6ce469dc7e79ee7 P805cbb71b19d14d4132339a0 E4ff3ac512c54c9a3d17eeb60
E4ff3ac512c54c9a3d17eeb60 E4ff3ac512c54c9a3d17eeb60 E4ff3ac512c54c9a3d17eeb60 Wc2f25449af8ee786dd98cb54
W4828eecabd690a634ad0cce9
""",
    ("kernel/scheduler_test.c", "micros_scheduler_runtime_run_self_test"): """
C56a6e218ffca268c14f75be1 C56a6e218ffca268c14f75be1 C56a6e218ffca268c14f75be1 C03cd500cd8cb945d1e223cb2
Cd9cd058ef7a26985da98fbfb Cc627088628b1dffcbb732d0f C2759012757375564673a9d3b C22265aa5c67b1f463d08d9e1
Cd72602943c3aa476133cd83d C967ffd5893440dc572c3d7f8 C5f242c8561f491d30bba98ce C883df69b6784a915910066a7
Cbde59711fa127b25a1651c63 Cbdcd6c056369167f98e2515d Cdd500d3d1367ded5ccce0004 Cd459b9fb5e6596475441f7b1
C1472842b6b7af9d64220d946 C22b021e3b0ae42d80e8f2334 Cabd0b9464885bd81b8714d18 C0c48a22786b6b706d99b1e99
C27f290f30faaa78366079234 C27f290f30faaa78366079234 C06734e2b476e5ac2155f8fe6 C730a54ddf538e29ad36ec3fd
C7b2188d9d274d914a5c06ab1 Ccf7942c2623f56a4af244db1 C981e1de14d1019314918e907 C82d830f83503a349ee21eb42
Cf45d6ebcb25b0828932c28b4 Cbf508b0b4cfa725204c16e5a C3b879ac1d7ed265942d70322 C6a4825ae40d285ccd652c0dc
C5c7dcc3b5eb14c34c1e453f5 Cb7a37ef5d1a7d7d8b07f8a17 C89a1faaa774148c3db67f366 C8baa804310d3694a189ca0f9
C343dba0742044f13924669ab C42ed2cae3f30feedc8edffb6 C82bf0aa21f8f50dcabdb9156 Caa2fb0afe8c08147f553475f
C29408f2cd292dfd82b28264f C3f13d9fbbbc15fd397ced04f C0554f8f2f4ad863352dbf736 C4074ec1048501d56272bf2aa
Pe8bec4c6959e2046d13abc1c Pbb9cbfe4ced580fd4197b40c Pd4630f7b378bf94bac99b591 P6174eeb4b8dac2e2662e7dc7
Pc9f43b35f1d487673ccb0526 P97153c63c95a29da40bdc9c4 P7945ae1e679aac4cdc22a0d6 P5f66a48633e4bb17d1f7e5f8
P4f46761a9941ab693770c79b P9e240201afeda370a92fcd30 P24d2610fbfa56bdeab3f0aa1 P805cbb71b19d14d4132339a0
E4ff3ac512c54c9a3d17eeb60 E4ff3ac512c54c9a3d17eeb60 E4ff3ac512c54c9a3d17eeb60 E4ff3ac512c54c9a3d17eeb60
E4ff3ac512c54c9a3d17eeb60 E4ff3ac512c54c9a3d17eeb60 E4ff3ac512c54c9a3d17eeb60 E4ff3ac512c54c9a3d17eeb60
E4ff3ac512c54c9a3d17eeb60 E4ff3ac512c54c9a3d17eeb60 W65f9344a3defd26952bd14d9 W379946dbe1a9cbb9dde1a8fb
W8b92a8094f8942f20b8f0fbb Waee61594488a64e0ce496a05 Wdf32d4880068404091f942d4 Wb5154cce2a228b662f5f9e4b
W15811cbf717d04427930bde7 Wdaad6dff51d32a2160d4d484 W5c6d4508dc8f9a9fb0b6fcc4 Wcf7cb9e4dce09334eabe2b49
W1bd772e8307b5e388f227c92 W37fa29bf23548e9b0ec0648c W826c59ccaf93e1dc9f1423e2 W1c1d22bb4b12dc031301facf
Wf9d42a2a1ec7b3286025848a W58900ee9b3d562513345fa26 W748023d242af20258ec0f8fd Wd3d097d05408823516c923f2
""",
    ("kernel/scheduler_invalid_test.c", "micros_scheduler_invalid_runtime_run_self_test"): """
C03cd500cd8cb945d1e223cb2 Cc627088628b1dffcbb732d0f Cd72602943c3aa476133cd83d Cbde59711fa127b25a1651c63
Cbdcd6c056369167f98e2515d Ca068fea27f51c53fb78786e6 Ceea4223866f66eb000fc31c1 C1472842b6b7af9d64220d946
C22b021e3b0ae42d80e8f2334 Cabd0b9464885bd81b8714d18 C91a23ba9e34f5d1b1c2af5e7 C27f290f30faaa78366079234
C27f290f30faaa78366079234 C6af08c0fec094f868bb755b6 C7b2188d9d274d914a5c06ab1 Ccf7942c2623f56a4af244db1
C981e1de14d1019314918e907 C82d830f83503a349ee21eb42 C42ed2cae3f30feedc8edffb6
Caa2fb0afe8c08147f553475f Cf096cc194ccef1a6c30f8b4f C3f13d9fbbbc15fd397ced04f C0554f8f2f4ad863352dbf736
C4074ec1048501d56272bf2aa P5aa3d04fdcea8f128242efbe Pc9f43b35f1d487673ccb0526 P7afa06e37afe0ba2febf9770
P8c6bc0ab1280fe16b88ee05f P5f66a48633e4bb17d1f7e5f8 P805cbb71b19d14d4132339a0 E4ff3ac512c54c9a3d17eeb60
E4ff3ac512c54c9a3d17eeb60 E4ff3ac512c54c9a3d17eeb60 E4ff3ac512c54c9a3d17eeb60 W65f9344a3defd26952bd14d9
Wac55d520aed0a0dfb7a23b47 W8b92a8094f8942f20b8f0fbb Wdf32d4880068404091f942d4 W5c6d4508dc8f9a9fb0b6fcc4
W86e89d9464eea60a1455147d We90561124d9bac0141b2248e W7d32d62143fad20d86d7b413 Wcd8c201cf51d09b033f061d3
W9bdcaa4ec3079f062d2c2667 W2a13e96b757983b86cefcfd8 W80ed63a80061c5782f30315b
""",
    ("tests/host/scheduler_test.c", "test_accounting_owners_are_separate"): """
C486be4bde26b2c3e5d102a5d C486be4bde26b2c3e5d102a5d C8550e84c3f9db5697c26ee5a C8550e84c3f9db5697c26ee5a
Cd8e0eb2b7b50a7590c508eed C3c43257dd99aac6acabba616 Ce2fa2de7854c0555d68cc9b7 C7240b5de4e5874c15ed31a0f
C4261f28e42df6b6831bf8816 C5d01aaaaccc2efecd03c687c Cc0904a1fc39927a8bce542a7 C0e01b5a6636fa5d48962fd4f
C9ea90803d97de5eeeb8f0bc3 C9ea90803d97de5eeeb8f0bc3 C9ea90803d97de5eeeb8f0bc3 Cbc600eb535b1b843e7762735
Cbc600eb535b1b843e7762735 Cbc600eb535b1b843e7762735 C59d6f70daa9e9e7d73412667 C59d6f70daa9e9e7d73412667
C59d6f70daa9e9e7d73412667 C5777b82309b5774a61baaa42 C5777b82309b5774a61baaa42 C5777b82309b5774a61baaa42
Caa71eb7b8dce5280e198cdae Ca7a666c6a4766bf84b00ac75 C267505c7b3f1dc2489b05b95 Cafc33b70f1d9b8812ff1e964
C82afec4704c58da2dcd9e1f4 C80a42cb5943de1e2336ab6e2 C16c331a1b5a062773ca65566 C00a74a21e5de125d855f1f2f
Cf9f0c8b859993bb932a05131 Cf9f0c8b859993bb932a05131 Ceb597c87e79ab86d6d66fd30 C9edf9f9167cba93612efe478
Ccb568bc31dc861d0ec396e4e Cbd8ed50e35804a33bd9d2b85 Ce923fb84bd17e86b2a745341 C808d67f566690af5f125c403
C03b1eda38b6c41ff083c4da5 C57c403d8195e174aa6d98116 Ccece72a852bc2081ea8ea358 C34227a25ca25196d17802ce3
C7b7f51e2af4ca650baef7059 C637c79c5fd50d22ac44bf929 Cb03dc66a9c90ad4bfa1f350c C840f9ca1984e71f9b667d3e3
Cc377b86796e94967292759b0 Ce6c49ab7a0d3baf3ed4f2e51 C42f1d04407e3461b3856e2bc C3f9d29a903b8ccd321b9477d
C57062a2f5bca68fdacf9e293 C7b3170b340ee036995af59b1 C88ddd620de7b9157778b0616 C5cb15c016ebfad4d6dfdd039
Cfe19c5aa454e474257c4bee6 C07b966d88ef0f9af1a1d8b74 C6b516928998548cc6fa3f05b C556f05099605b76f16df376b
C268e3a21dab7535fb11cb1c6 C004893b4a4bd3c2390cab8ba Cf1a7fc4261c06429c3192f78 Ce19893696deb7d6a472675ee
Ce535402d68d013ccd6a5ed93 Ea292155051e6195d23c4e06a W6466df46153ff9bf530caa9f
""",
    ("tests/host/scheduler_model_test.c", "model_matches"): """
Cea4a7b88cd79b8b9bcc6eff6 C86c67c2d05bc4eafdc51e24e C29a0162f0636f709328fb54c C3bc23788e09db3d9b41cb4f5
Ce4729569f7b8fcd011668757 C26782ea08d47c0bf7cb54225 Cb4fd9f272f6144c3de75b4c4 C60cb7d7dae3a3a733e1f4266
Ca8f24f79f5431b347ecd9dea C291c3cef08a60dbe44b44c03 Cb6af6918c4e69d128a622c72 Ca8ccc39bc35c54707d60ad02
C16768dbf11aa02471754f99f C8e5a483d4bb33517387e47b3 Ca580cbb92fef5cd5615192ba Ce56a2a716d376b58cdab74a1
Cce856aa71e15d1273b1e4f49 Cf779ea6d811a6787ff582f21 C75215e16c2142b4801f03bc1 C680dfdd72c5da18ab38a4752
C821a58f969d50644154e0e96 C821a58f969d50644154e0e96 C33d5f4c7226b1b98310d8981 C3a3ecc20c4ab5e06d387c056
C6f4e436b6fe55b5b74f9b621 C008ffe0f9bdad5c2e8f6a056 C9b743f855947fc79322773a7 C4e3c7798a87cadf070440150
P2f70dba1c5f35e9e2b1808dc Pe4da218e59b9829500c88de4 Pe4da218e59b9829500c88de4 P9a5ca6e69d685a528c7011f5
P3ca5424c868a2ae4ab0f242c P276a3f93ec4d118022f31fe4 Pbfc02bddc8ce14620547168f P269ca2aa988264faa43c864c
P5004a255a7bb53758f84069c P6111fd12c71cb283a724d475 P3250663f075fb69a8ea22d2a Pa88bf20a609967eada732dad
P937f0f374e925751cbb76d88 P9281b0b8280e063b2712194a P736454b6c4ba10c0002b81ec P0633b516d019186baaefb544
P0ba293e935a1ecace90735b5 Paa20dde6229dd66d781650e2 Pf02ea09c63939d9485dcf3cb P83cab0c4300c5dc5fc4274eb
Pf7fd5a15646c761226a2f8c4 P5ce6a7dc841498ad596a6b00 P2b52b4d37a7138f8160836f1 P2b7cae6ff7850c1eae5bf3f2
P4a671046a90b40cfead47657 E24d91c019b37ae362b86d42b Ea292155051e6195d23c4e06a W43753d96ae8a6c17d31dd19c
We3fa22159642cd2ddf16cf45 W06b2a8d56b8dec35ed85298e W4dbbe36c1a5db01e95ee9734 Wfb07fe02f39c4d867fbdf2c3
W13d13f62648697cf138b95c2 W7eaf180710e90f6f5e3ae8ab W982316b2fb49abf5388e62b5 We683d6080e42aecbdcbe384f
W0d7311eda5487bf226182bb1 W6de89b5cd1aa06e0c18f9949 W507e471796e129e0631f02e4 Wd838411f90b312503f135562
We2b36b18b49f2b6ab297873b
""",
    ("tests/host/scheduler_model_test.c", "run_required_prelude"): """
C7129b80e0f60499dbae097b2 C9ed776d89ea23ac5c1b56229 C64e4966b4a90c67c77c58041 Cbb0836ac95343967bf9dfc6e
Cbb0836ac95343967bf9dfc6e Cbb0836ac95343967bf9dfc6e Cbb0836ac95343967bf9dfc6e C8113d66a8471b91472b6d972
Cdd7db3dc0aacb61be87c7bf2 Cdd7db3dc0aacb61be87c7bf2 C11ab10204ba03c89ff9f83f8 C0993f735ce62a24124487d2f
C5435530f620bc35e77519d62 C8e2d7983339d96c140440777 C13db3f334827990c0b60a9fe C13db3f334827990c0b60a9fe
C13db3f334827990c0b60a9fe C869e6daed6b1701f47ad18db C5d01aaaaccc2efecd03c687c C70b2f2526827e8abf97bb731
C26394f63ee757ca0880fda3c C42de2cbde20a6afebd4d2749 Cbd8ed50e35804a33bd9d2b85 Ca64cf78c7c38cd7463b777e2
C571c932a7716daf758dc15e1 C932d11a9e808e68dc924cde0 Cdf4ab2f1ac372f28b6130d35 Cb1307554e246eee72623ce4a
Cd3f2d3c64ca25aaab04006a2 C95c35c9a5e276c094196ea5c C6cbc69c85a64943a9c22a1f9 C70b805fd5afedce146b4b702
C95d0b189a6c096b5bcf1a2e7 Cc6edc0e6f36c4c4e458c850b P7b60b1de084c43f3cb1bcf50 P64040035f8fd68a56c03b9ea
P7fe470d1dc1f71d544237769 P7fe470d1dc1f71d544237769 Pe5e1ed3bfa92eac2f64d90f9 P2221a0133423b5d2a7042c2d
P2221a0133423b5d2a7042c2d P07a3ca2d56e509ccf85a37f2 P198bb8c2e068fadb18f7a3f4 P2090d3b369047a4348679c60
P2b7f4427e41a2298f9c626b6 Pd788a887893e8294eae23cb0 P8d27feb1a0a87645516a6022 P0426e7283a93da12aef55b0e
P0426e7283a93da12aef55b0e P23822a831bf35cee84a4c286 Pfd43c454efc701c90ee320f5 Pbb20e85c77f26838b0901f66
Pc9e0e8f85ad8d20650cecbf0 P9e72dc1933ba063ae429b96b Pe29001e6c08e229c73aed634 Eedbf7c73ca48cfd173749904
Eedbf7c73ca48cfd173749904 Eedbf7c73ca48cfd173749904 Eedbf7c73ca48cfd173749904 Eedbf7c73ca48cfd173749904
Eedbf7c73ca48cfd173749904 Eedbf7c73ca48cfd173749904 Eedbf7c73ca48cfd173749904 Eedbf7c73ca48cfd173749904
Eedbf7c73ca48cfd173749904 Eedbf7c73ca48cfd173749904 Eedbf7c73ca48cfd173749904 Eedbf7c73ca48cfd173749904
Eedbf7c73ca48cfd173749904 Eedbf7c73ca48cfd173749904 Eedbf7c73ca48cfd173749904 Eedbf7c73ca48cfd173749904
Eedbf7c73ca48cfd173749904 Ea292155051e6195d23c4e06a Wd16c981edc96fe7769e54527 W6575578e802928db3ed02618
Wfb13af3b6f53151715405b81 Wfb13af3b6f53151715405b81 Wfa73ecece99c9b5eaf1d1a4e Wfa73ecece99c9b5eaf1d1a4e
Wfa73ecece99c9b5eaf1d1a4e Wfa73ecece99c9b5eaf1d1a4e Wfecb1f467266b812d353a9d6 W02c3aa58026c607b5b07dd01
W02c3aa58026c607b5b07dd01 W8a1a10fc3c790b66be45c7c7 W00fe2b8c05f68cdb8c3de583 W1e88781bbe1638ba3ca067c4
W1e88781bbe1638ba3ca067c4 W4c1001dd8e9769d71a3e99a3 W4c1001dd8e9769d71a3e99a3 W5757e1a4ab88f163201fb663
W5757e1a4ab88f163201fb663 W6a299643e8eaf6798c85df5c Wa2fb4fea1c8a50687b6c5a36 Wf9809f675cf3976779af7c95
Wcba35eb27c708f77f3dea8b8 W085b8b3c20dc0a307c46b56e Wcf3c3a7e5d0ecb6711c109e3 W1f9668646ab6e3a0541bd62d
W4727187fcf73db943b3d543c W5af7c5cb5d7d4ec49c5caad0 W550ed36af8f25bf2b607a554 W900c34df22b61b1efec6463c
W3bbc562f4eb4eceb74da9eab W53a366e311c9ff87a977c779 W1b2d668a87a08d0630694329 Wde6e735911abf2273a64d21c
W2684511e60943ecb574fd589 Wfa9fd972e804c2d4371c37a9 Wffc52334daaea485a23f5bfc Wfa6e1cb84fcaf8be485edcba
W7cef509067c2394595c4b066 W4a502c011cdf1d0eec49c611 W4a502c011cdf1d0eec49c611 Wa884226a22a57e797c17c3d2
Wa884226a22a57e797c17c3d2 W0583b043e8d095e24ee4ed32 Wdccbd9d0d6af957b5fa0b7b7 W6e1f632ae4acf40a2ca27ad1
Wbeb7f82e2e8cfa6bb772111c W88e4a06e6de3a0f59ab91b53 W88e4a06e6de3a0f59ab91b53 Wccb7388a43ad960b3a11430b
Wb66715cb3a3cc9a4fdd5dd10 Wbf095859fc0239de152e1832 Wb4455717c129a0e568da92dd Wdd03166876035b737ebf9b5e
Wd9bf2445473f52f38ac47917
""",
    ("tests/host/scheduler_model_test.c", "micros_scheduler_model_test_run"): """
C6791c9bae722408bca34f27f C8550e84c3f9db5697c26ee5a C9b1ca19ebd8e1594053e3db8 Cbc54fe04f8c5360b9779e8c8
Cbf5fdd31d2e5e5b394bd3e84 C99deaec637e527a706fb3a38 Cd3f2d3c64ca25aaab04006a2 C69473c18be2d74ab0ba29159
C69473c18be2d74ab0ba29159 C69473c18be2d74ab0ba29159 C69473c18be2d74ab0ba29159 C69473c18be2d74ab0ba29159
C69473c18be2d74ab0ba29159 C69473c18be2d74ab0ba29159 C69473c18be2d74ab0ba29159 C69473c18be2d74ab0ba29159
Ce5e09d35179c322c4d63a50d Ce5e09d35179c322c4d63a50d Ce5e09d35179c322c4d63a50d C5e6189297aa09ead91de8a5e
C5e6189297aa09ead91de8a5e C5e6189297aa09ead91de8a5e C5e6189297aa09ead91de8a5e C5e6189297aa09ead91de8a5e
C5e6189297aa09ead91de8a5e C5e6189297aa09ead91de8a5e C5e6189297aa09ead91de8a5e C5e6189297aa09ead91de8a5e
C95d0b189a6c096b5bcf1a2e7 C32619c7ded74f7b53fb3408a C32619c7ded74f7b53fb3408a C32267d7a02239a02130e2a17
Ce63add3354a19d8cb681ffd0 C320798c48f74574fb562ed3c C0a1de19f132a2b9fd8e008ef C45f66649cfcebde2b14306f7
C8fafba9ade4f344b28f5cfb4 Cfc44a4d60365dc51dca4902b Cb32b324d012212ef1fc500d9 Cc1e3a54160217e7648d961d0
Cc4e80df57021bdb732f38008 Cfb57cc6f305502c26c527ed0 C26b288412dcf18879ac4e5cf P405e2e9c3c517497fa26b740
P5d226d42472413d575cb2371 P7f8e8491909ce85880fd58d8 P8c301d9500caa37e2c293446 P8c301d9500caa37e2c293446
P8c301d9500caa37e2c293446 P8c301d9500caa37e2c293446 P8c301d9500caa37e2c293446 P2c1e7eefb68578f7f5ad6e77
Pb5a9ef12ecb87e9db151c13e Pb5a9ef12ecb87e9db151c13e Pb5a9ef12ecb87e9db151c13e Pb5a9ef12ecb87e9db151c13e
Pb5a9ef12ecb87e9db151c13e Pb5a9ef12ecb87e9db151c13e Pb5a9ef12ecb87e9db151c13e Pb5a9ef12ecb87e9db151c13e
Pb5a9ef12ecb87e9db151c13e Pfd43c454efc701c90ee320f5 Pfd43c454efc701c90ee320f5 Pfd43c454efc701c90ee320f5
Pfd43c454efc701c90ee320f5 Pfd43c454efc701c90ee320f5 P2f3d96539165a068c9cd20c7 P2f3d96539165a068c9cd20c7
P2f3d96539165a068c9cd20c7 P2f3d96539165a068c9cd20c7 P2f3d96539165a068c9cd20c7 P2f3d96539165a068c9cd20c7
P2f3d96539165a068c9cd20c7 P3c4564453443c7961cab4858 P0f730dc8c845298fa11d4486 P0f730dc8c845298fa11d4486
P9b3c42c93dd24fe20a336bca Pf55a49659ea80af4e01762d2 P52fbfcdd40f3826a0cf40c6a P2f2c4bb3d7a10651da08d574
P450c7a7aff7d53e850f93fc8 P30062476fc61cdbe397f0e2e P8f2ee0efc2635096d2e3fe5e P84d02329d130eba9b7dd8421
P70570b12f1debcb9e84debb1 P721bf964727f54db0bc3f08d Pab416ff1c589c36e6965c9c6 Pdcee3402bcf29238e7255a23
Eedbf7c73ca48cfd173749904 Eedbf7c73ca48cfd173749904 Eedbf7c73ca48cfd173749904 Ed3bb12dc024358e1c462a08e
Ed3bb12dc024358e1c462a08e Ed3bb12dc024358e1c462a08e Ed3bb12dc024358e1c462a08e Ed3bb12dc024358e1c462a08e
Ed3bb12dc024358e1c462a08e Ed3bb12dc024358e1c462a08e Ed3bb12dc024358e1c462a08e Ed3bb12dc024358e1c462a08e
Ed3bb12dc024358e1c462a08e Ed3bb12dc024358e1c462a08e Ed3bb12dc024358e1c462a08e Ed3bb12dc024358e1c462a08e
Ed3bb12dc024358e1c462a08e Ed3bb12dc024358e1c462a08e Ed3bb12dc024358e1c462a08e Ea292155051e6195d23c4e06a
W9a52010ac3b237bd80850572 W628221d37b4d472c528b5cf1 W395fb6484eaf9da4a9f12639 W2f3a54de5f6f857565f9ae39
Wa884226a22a57e797c17c3d2 Wf411dd5bebe62f9a5e629dd7 Wd389771b7adc009560076ac2 W9b0d2c2563ce4a54fc0e659b
W3e15c60667e3c88b19e4c90f Wa2fb4fea1c8a50687b6c5a36 Wa2fb4fea1c8a50687b6c5a36 Wcba35eb27c708f77f3dea8b8
Wcba35eb27c708f77f3dea8b8 W0583b043e8d095e24ee4ed32 W93c6b16f8af7fc5019e57f19 W8fc47b73a933f41f10d22564
Wd9bf2445473f52f38ac47917 W900c34df22b61b1efec6463c W900c34df22b61b1efec6463c W5af7c5cb5d7d4ec49c5caad0
W5af7c5cb5d7d4ec49c5caad0 Wce42c82c689ac641e7f11a9b W01f1fa3bb2fdaebcb9922594 W0b89a49f3106e978979d309f
W6b16ef4a272ec046369984be Wfb67e4649e46703f5ba76ba0 W0b677e909825410a388be391 W0b677e909825410a388be391
Wb8b1d3c722ec78cc2496c3a7 W2fc47a56cdda39a58b846491 We5636919c31582d45cea18e7 We2744a6c75846388cd602bc9
Wabdd56e8fe3a726e0e114c01 Wabdd56e8fe3a726e0e114c01 Wabdd56e8fe3a726e0e114c01 Wabdd56e8fe3a726e0e114c01
W8c35606253a29602faa65a64 W8c35606253a29602faa65a64 W4a2c6af5ad9c9403eb67ae43 W66edb0ba297b6a48a93fcf54
W66edb0ba297b6a48a93fcf54 W66edb0ba297b6a48a93fcf54 W66edb0ba297b6a48a93fcf54 Wba23eaa71d0cfdc44652a9ff
W02c3aa58026c607b5b07dd01 W5e663e8c6616741d67f8fcef W3e8a724a58bcb312104e0119 Wf938accd12da8b3f24e6d3d2
W10d1eb8c90afe3a807ff7586 Wed55570b52f887a4f8936a90 We91c18a6dead1a92224f36b9 We91c18a6dead1a92224f36b9
W9ec7b91718d629e079f1bb44 W5757e1a4ab88f163201fb663 Wd6b83905c6f3cdc31895516d W0dcaba2b45c7513a7df3b79e
W0dcaba2b45c7513a7df3b79e W98874e5e9bbd53d97f8090b2 W202ba3460add13ff22eb094f Wccb7388a43ad960b3a11430b
W6e060217f2c70dfb7d5400af Wa6e732de1fd819176bb3bb92 W84ada898a3c9cc00f0a40612 Wdd03166876035b737ebf9b5e
Wdf47f0ac7ceb2dbc5e6d4732 W1a820d9b3c3016d6daa56b63 W89dd6f1e9be9fbf4620bb907 W16c180843e4a30c251c6dd8e
W71f8ad9610d63df957f08acf Wde6e735911abf2273a64d21c W4727187fcf73db943b3d543c W1faf188de36e335bb0e158bd
W9b7cf82b6e93b490f13e3b42 W1ffbada5fcd405a66d9f7c40
""",
    ("kernel/scheduler_core.c", "micros_scheduler_commit_ipc_transitions"): """
C722d7f3826e611fd536f3d66 Cc68518dfff3131094e723619 C9f5a184ade7fb14935dbd593 C60ea7c00b254564985be0ecf
C59a81e3fa965f9fc960c2362 Cb7d8dbbf14b3b41b7d51cd50 Ca87d40c9479d43308dd3bc49 C38483fca54bcefce2c2596aa
C4b0e1603005913c2f800181c P7fe2a3b7a0c8786db537dce3 P73fddc98eecee7e2254c5501 P73fddc98eecee7e2254c5501
P73fddc98eecee7e2254c5501 Pbc1c0add5bbc177539c1c62f Pdbcbd2a63fbe76f3c8634448 P7e8349574ad51783f05daf1a
Pb5a9ef12ecb87e9db151c13e Pb5a9ef12ecb87e9db151c13e Pb5a9ef12ecb87e9db151c13e Pb5a9ef12ecb87e9db151c13e
Pb2b61feb5bc639cfed63d355 P5aa93baf699528e87cc75d3d P5cba2f164b7fb833d0bfe977 P6b93da7354892eda844e6238
Ec80ac4c2c3d12607bc516dd0 Ec80ac4c2c3d12607bc516dd0 Ec80ac4c2c3d12607bc516dd0 E9feee70894ecd52831d45d48
E9feee70894ecd52831d45d48 E9feee70894ecd52831d45d48 Eb9795a7173838786fc310ab4 Eb9795a7173838786fc310ab4
E24d91c019b37ae362b86d42b E27a2c1ee1fab40d965898d53 E33fa14a87901c21362169b98 We6eadb3c0fcca6ade1197194
W947c4042e610e2e8d3252ba8 W27116c223aa314f2222430b7 W9d5e497cdb10d31eacfc0b19 Wb9666a778f81259df5149cb4
W02f098651c18916eac7a8723 W9d707b598079024f86ce9ee9 W68197ad58eab43ba779f3046 Wb2b15359bc28a8466cfaecb7
W427d7034c705d1688fb6893b
""",
    ("kernel/scheduler_core.c", "micros_scheduler_core_validate"): """
Ca85b4d98661904a26a869f0e C6fd635f98cb1e7b4feae848d C6fd635f98cb1e7b4feae848d C6fd635f98cb1e7b4feae848d
C027d9747363ed533462cf724 Cff43cbfa6d67e30150ba13a6 C887f1ec6802f765cef712ee9 C887f1ec6802f765cef712ee9
C48bc23d6743a26a4372bc63d Ca9415925f5dcc2eac69f26ac Ca9415925f5dcc2eac69f26ac C2047cb7b75bcdf22a45a6540
C121d1cf7cc710cc9f8e5cd4c C121d1cf7cc710cc9f8e5cd4c C7ab50213b377bc4ccf57ac90 C52fdb5321666a70a72749036
C2009f2dd9b70d616b6a42ec4 C74c21861fdc0c92c66b3a38a C74c21861fdc0c92c66b3a38a Cbf9ba115a70c48a45c0819fa
Pb5a9ef12ecb87e9db151c13e P1481ff4a66cdf376c44e518c P1481ff4a66cdf376c44e518c P58d3d78a7b6168e21570dd04
Pde0bc4a8e3260b0a8df41f17 P2a77662c8ea1cf7f337cc095 Pb74309293a45f1751c44fc10 P118b783059f3470c6a73c120
Pb384a03a7ae573879e81ea7a P15b1403a5f32fb45605d0e05 P153ed02b4e2c04143222d8b6 Pba8c4bd9f5969a2e489f8fd5
P3250663f075fb69a8ea22d2a P4f13ba361e8ced88d9b6b840 Pbae12ec7c618562dc025a1de P53b004bdb565585fb1a21ed6
P9f18eb926cdc9ed216d0e03e P10660bc1b7b865bd7123d4c4 P83a180cae12c2f7c4a8c66aa Pfe7c71ec02411e52ac968ac4
Pf685755f598c3129dc65384f Pf685755f598c3129dc65384f P0e6561cfd04d9ca37df99508 P0fc16183ed706246e5161e5d
Pc3c680fb07fbf31c6b94a900 Pad235ceae5a65ef797ad7df4 Pd56be5c16a59271542560741 P8c68deab8bd5129453860139
Pf174ec866456da31f4dcd7c3 Pce0cc668cb0ddc288c77c7e2 E9feee70894ecd52831d45d48 E24d91c019b37ae362b86d42b
E24d91c019b37ae362b86d42b E24d91c019b37ae362b86d42b E24d91c019b37ae362b86d42b E27a2c1ee1fab40d965898d53
E27a2c1ee1fab40d965898d53 E27a2c1ee1fab40d965898d53 E27a2c1ee1fab40d965898d53 E27a2c1ee1fab40d965898d53
E27a2c1ee1fab40d965898d53 E27a2c1ee1fab40d965898d53 E27a2c1ee1fab40d965898d53 E27a2c1ee1fab40d965898d53
E27a2c1ee1fab40d965898d53 E27a2c1ee1fab40d965898d53 E27a2c1ee1fab40d965898d53 E27a2c1ee1fab40d965898d53
E27a2c1ee1fab40d965898d53 E27a2c1ee1fab40d965898d53 E27a2c1ee1fab40d965898d53 E27a2c1ee1fab40d965898d53
E27a2c1ee1fab40d965898d53 E33fa14a87901c21362169b98 We798a91ff2b2b2c0aec31c66 W0663edaf6adbadea4f6998de
W487ead75bf9b02cd835bb3ef W5034e57f7401b059841c1839 Wefab417be6e3338a9f43ad17 W8c02d4c12310c82f4c74f8ae
W83d7423b5aec24ac2c9ebc0b W3c83958d8409bbaf4f9e5845 Weaeb0c5b6d6074ed2dc5c293 W7048f796a6f1dbf36cd19283
Wa828f85b89cc2a09d373fe55 W15621be64c2a06d9ba43b385 W80af47229ad01e26f9a50d13 W713072ae083525e49b92b2ff
""",
    ("kernel/scheduler_core.c", "micros_hart_plan_user_return"): """
C60ea7c00b254564985be0ecf C4842e7aba4b8e92c817e9006 C6fd635f98cb1e7b4feae848d C6fd635f98cb1e7b4feae848d
C6fd635f98cb1e7b4feae848d Cc2a2aa29b816aba91b0c6980 C9bf1a3eb631b7599872e101a Cb19c2068034cfcf3f96454ac
C7e64db42a40e7c6304ce847e Ccb22c5c2b93911416732ce3e C19510e088ce4a47bd7a18b9e C19510e088ce4a47bd7a18b9e
C5d7e42735b15e2104da57152 Cbfd767724460d61120d087d6 C61397eadc50b4fde71372ff1 C5a87a604498785e20ec76f85
C48bc23d6743a26a4372bc63d C48bc23d6743a26a4372bc63d C48bc23d6743a26a4372bc63d C48bc23d6743a26a4372bc63d
C73863d7737f4d2b446614180 C2f59075240ab4bb4f81a5da8 Pfa02cc858cf432b448432125 Pffcdcc47a3601e57a4395959
Pffcdcc47a3601e57a4395959 Pb5a9ef12ecb87e9db151c13e Pb5a9ef12ecb87e9db151c13e Pb5a9ef12ecb87e9db151c13e
Pe418e38012a8581074b3185c Pfc44e368062fec95de80ebcd P83a180cae12c2f7c4a8c66aa P54d16b863295173fe89f4c12
P29322e77e28c6235dbfe2664 P02b0dbf718899805ac331672 P805cbb71b19d14d4132339a0 Pd4eee7d1459e38366ba9c8c0
Pd4eee7d1459e38366ba9c8c0 P3d2a8fb0cf0c84d3b2ac7a81 P9cf0992c703a4cc73b13b4ed Paf9ae891fdbbbdeab6e6bcde
Pc515879596a5eaaede62e673 P3250663f075fb69a8ea22d2a Pa967b28dc78ea8fdef9dc54e P08b99b1c1978cf3a704ba4a2
Ec80ac4c2c3d12607bc516dd0 E9feee70894ecd52831d45d48 E9feee70894ecd52831d45d48 E9feee70894ecd52831d45d48
Eb9795a7173838786fc310ab4 Eb9795a7173838786fc310ab4 Ed3bb12dc024358e1c462a08e Ed3bb12dc024358e1c462a08e
Ed3bb12dc024358e1c462a08e E27a2c1ee1fab40d965898d53 E33fa14a87901c21362169b98 Wb0b4381be82b07076585a328
Wd4f922747443bf8ba77a8701 W36a3b29cb0280f3068bae870 We6eadb3c0fcca6ade1197194 We51b4acda47b01291563ee40
Wbcc34b1ab8e5c9b5bbf1a3ef W62b2c38bff96cff07074b6d7 W161d7135ad7124157b9ceb0a W56c86b4ffbe75f5426360dc3
W0d7fda1a069a645ee347bdba We75032ee71b4ac264ed603fc W0f086c7f567f911d230a1a41 W0a9136a67670502bd6c4f4ae
Wed6e073a84b4ce11600eaf8a Wfbc9b49b8cf9459c6f33f908 Wd23c8fb4dbfe9cd9613d21ed W8d757a41ad399d4d6d8dff86
W09ceed8e99bff2986d2f7df1 W8d3ab556c3e67fae23287570 Wbae36d5a118e001dd84ea11f W72c7c0b98970221463a1326b
W7134d7952f95da7d48af4871 W08061a9fcf02a70feb315e34 W3cfda0fe5b9e2cc088db237e W52235eeccdf53c7114192232
Wb1155f4c9d76bf1cf8860708 Wf3ba0f58540e9f158bf4b9da W80301f287d3ceb6b262048f1 Wddc48632537ca1aec085fbca
Wf8dbf28a3ff86e4326372e59
""",
    ("kernel/scheduler.c", "select_user_return"): """
C5da774019630beca3a287876 C5ae0952f8cfd94ce10d428a4 C29b6f4bcbea88d8f10406a4d Cc1535565ef8daf10c4019b74
Ce4e2341912d030e0936672d0 Ca1757b7ea3add6c5458c07dd C6a8d63d03d6c7dbfdb0edb51 C60806c1096024311097c8aa6
C21937dedf4d624323df870b2 C398e79cf3fafff910551dbe1 C4c62cbafdf3648dfdb0b7313 C4c62cbafdf3648dfdb0b7313
C5d41a9bcc6317fb035bfad77 C5d41a9bcc6317fb035bfad77 C7715b4845da8ad9e29152a72 C7715b4845da8ad9e29152a72
C19b7f20d90973eb21c504a28 C41597e577e832a5910175d8c C26cacf70f59bb8d647b249a3 C0a73829075753005ae858444
C0a73829075753005ae858444 Ce3ebc5483973879383448371 Cd674735342207bd1851e3502 Ccc993eff65c1b9178553119b
Cf86e966613c71996d6c50482 P88a458fdca5d26a1cb256d06 P3ca4d08ec49993743f0f627e P8988c5c6bd489f6681cd5e3e
Pc5265bcb86a179a8b2c3aa00 P69e1f70ebfa5956bba36daa4 P6cd04b5966c4d8b86ebbccd3 Pb5a9ef12ecb87e9db151c13e
P12b69e90510c29ac914fee1e P29a3d787b668ddd58ba4b419 P29a3d787b668ddd58ba4b419 P9565009dbb7460c8b8184e34
P9565009dbb7460c8b8184e34 P9565009dbb7460c8b8184e34 P9565009dbb7460c8b8184e34 Ecdfd0fb6bfe00caed9b114fa
Ecdfd0fb6bfe00caed9b114fa Ecdfd0fb6bfe00caed9b114fa Ecbb1dafcc69bfafb20893b51 E1478be28ba2c4e03718dd46d
E1478be28ba2c4e03718dd46d E16f8fe8c88662b61829a06c3 E16f8fe8c88662b61829a06c3 Ec4ee6a76d12d607364e40fba
W51119a8782e1f725774deef5 W81c7ef8498c6cdf513bbbcc0 Wa1d371ecc531bcf30ecf83c4 W996e4e19da768ea0cac2babb
W996e4e19da768ea0cac2babb Wdea51ae372c53db72e5f48ca W6726ebfbb9fc7a142b6aa1fc Wfc8b27d9a2d5d3f78d870bd4
W5d942fb4e5afb021e7b85fff Wf5e56a9df88ec3c560e708a1
""",
    ("kernel/scheduler.c", "micros_scheduler_start"): """
C5da774019630beca3a287876 C8baa804310d3694a189ca0f9 C5ae0952f8cfd94ce10d428a4 C03cd500cd8cb945d1e223cb2
Caa2fb0afe8c08147f553475f Caa2fb0afe8c08147f553475f Caa2fb0afe8c08147f553475f Caa2fb0afe8c08147f553475f
Caa2fb0afe8c08147f553475f Caa2fb0afe8c08147f553475f Caa2fb0afe8c08147f553475f Caa2fb0afe8c08147f553475f
C21937dedf4d624323df870b2 C398e79cf3fafff910551dbe1 C26cacf70f59bb8d647b249a3 C027e69e9cb7be7615f140b33
Cd674735342207bd1851e3502 C5d41a9bcc6317fb035bfad77 C5d41a9bcc6317fb035bfad77 C4c62cbafdf3648dfdb0b7313
C3a82334c14d75ff948927e2a C3dc8fc2deb48358eb88edfc2 C3f42a36b10338bc47edf2387 C7715b4845da8ad9e29152a72
C7dae15d5cc038d74234206ed Cfd255dec21efce861bd5cd0b C269b4c1697f652f27a9f238a
C1e064a81a88d14a53194589b Pd902ac2426e5e4d99fc1c663 Pbb80def48e1c7dd65f8c8afd P6cca91e9a3054e96b0ef0a95
P9565009dbb7460c8b8184e34 P9565009dbb7460c8b8184e34 P9565009dbb7460c8b8184e34 P9565009dbb7460c8b8184e34
Pc8c9d6a0aca59fdfbae13cec P5cde7883d4c9be6161901d30 Pa43c02ce6e1ca861e2952627 P60531da912bca4a64958a1e7
Ebac199082f797d440596f5be Ecdfd0fb6bfe00caed9b114fa E469ab868d1078fadc4ba5434 E16f8fe8c88662b61829a06c3
E16f8fe8c88662b61829a06c3 E16f8fe8c88662b61829a06c3 E1478be28ba2c4e03718dd46d Ee2e7e7efc2c15fb4b37a3da1
W51119a8782e1f725774deef5 We3adae342d68d36afc0b83b8 W81c7ef8498c6cdf513bbbcc0 W856bb2ca90f2efff18a22616
Wa1d371ecc531bcf30ecf83c4 W5d942fb4e5afb021e7b85fff Wf5e56a9df88ec3c560e708a1 W996e4e19da768ea0cac2babb
W996e4e19da768ea0cac2babb
""",
    ("kernel/scheduler.c", "micros_scheduler_idle_select"): """
C5da774019630beca3a287876 C5ae0952f8cfd94ce10d428a4 C69f5513cfe4e8e21c170d985 C42922f346cda488e6305635e
C42922f346cda488e6305635e C42922f346cda488e6305635e C42922f346cda488e6305635e C42922f346cda488e6305635e
C42922f346cda488e6305635e C21937dedf4d624323df870b2 C4c62cbafdf3648dfdb0b7313 C4c62cbafdf3648dfdb0b7313
C5d41a9bcc6317fb035bfad77 C5d41a9bcc6317fb035bfad77 C7715b4845da8ad9e29152a72 C7715b4845da8ad9e29152a72
C26cacf70f59bb8d647b249a3 C027e69e9cb7be7615f140b33 Cd674735342207bd1851e3502 C3f42a36b10338bc47edf2387
C7dae15d5cc038d74234206ed C1e064a81a88d14a53194589b P3e2b8e50b39efd42bdc13787 Pb5a9ef12ecb87e9db151c13e
P12b69e90510c29ac914fee1e Pa7a799fbdf8f8f3e1a26833e Pa7a799fbdf8f8f3e1a26833e P50f86c70ddc9e86ee537e3a4
P54b39ab3d299a87dbba1977e E99d65c933c5f6d6ea0897e57 W51119a8782e1f725774deef5 W81c7ef8498c6cdf513bbbcc0
Wa1d371ecc531bcf30ecf83c4 Wdea51ae372c53db72e5f48ca W6726ebfbb9fc7a142b6aa1fc Wfc8b27d9a2d5d3f78d870bd4
""",
}
FROZEN_MACROS = {
    0: "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
    1: "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
    2: "e2223156584bb2abcc54beea97e88467ac2e2e7cce4232f27d7e143c02197628",
    3: "e9d543374864c8a054fc146b235e6a6520784cf512ba1a9f1d303e8a3cf15512",
    4: "93fd24f372674cf6f2464869f1e734b612161573ce394096124fa5c99f7a2637",
    5: "4a01eb702f76ff47e1ae012dadeadd79e75b64f74cca03443761b9cf9461970c",
}
FROZEN_CONSTANTS = {
    2: "c9271b8e7650af1a389e7065baf2a2c3664bd1e27136fae85f5b5ff0af7d197b",
    3: "6550a42b4b842a4dc00c9840de12576e089b7697fc5f8e9295b82f3cd1972307",
}
FROZEN_QEMU_COMMANDS = {
    "test-qemu-scheduler": "ea6101ac8ef2c78fcada514dee73ea92519fb54dd52ac4c4a681a01a72269d3e",
    "test-qemu-scheduler-invalid-context": "8af3e72835266bff7ff2c108780f19424ad301049604dbc46cfebe7c1159bf78",
}
FUNCTION_GUARDS = {
    "micros_scheduler_test_rejects_malformed_completion": (
        "ifdef MICROS_BUILD_SCHEDULER_TEST",
    ),
    "micros_scheduler_test_enter_without_timer": (
        "if defined ( MICROS_BUILD_USER_EXECUTION_TEST ) || defined ( MICROS_BUILD_ADDRESS_SPACE_HANDOFF_TEST ) || defined ( MICROS_BUILD_IPC_ECALL_CORE_TEST ) || defined ( MICROS_BUILD_GRANT_SYSCALL_TEST )",
    ),
    "micros_scheduler_test_prepare_supervisor_return": (
        "if defined ( MICROS_BUILD_USER_EXECUTION_TEST ) || defined ( MICROS_BUILD_ADDRESS_SPACE_HANDOFF_TEST ) || defined ( MICROS_BUILD_SCHEDULER_TEST ) || defined ( MICROS_BUILD_IPC_ECALL_CORE_TEST ) || defined ( MICROS_BUILD_IPC_SYSCALL_TEST ) || defined ( MICROS_BUILD_GRANT_SYSCALL_TEST ) || defined ( MICROS_BUILD_USER_RUNTIME_TEST )",
    ),
}

RUNTIME_FIELDS = """
bool scheduler_initialized; bool scheduler_timer_enabled;
bool scheduler_start_timer_prepared; uint64_t scheduler_preemption_interval;
struct micros_user_context selected_context; uint64_t selected_root;
uint64_t scheduler_kernel_root;
"""
NORMAL_FIELDS = """
struct micros_process_handle processes[2]; struct micros_thread_handle threads[2];
micros_endpoint_t endpoints[2]; struct micros_endpoint_registry *registry;
struct micros_user_context prepared_contexts[2]; uint64_t code_physical[2];
uint64_t counter_physical[2]; uint64_t stack_physical[2];
uintptr_t kernel_stack_bottom[2]; uintptr_t kernel_stack_top[2];
uint64_t baseline_owned; uint64_t baseline_free; uint64_t previous_counter[2];
uint64_t ecall_count[2]; uint64_t timer_trap_count; uint64_t switch_count;
size_t last_running; bool supervisor_returned; bool start_boundary_observed;
bool start_completion_observed; bool idle_requested; bool idle_woke_thread;
bool spurious_idle_bypassed; uint64_t selector_entry_count;
uint64_t selector_entries_before_spurious; uint64_t kernel_ticks_before_start;
struct micros_kernel_objects start_snapshot; uint64_t start_satp_snapshot;
bool start_sie_snapshot; bool start_stie_snapshot; uint64_t start_timer_attempts;
enum test_completion_kind expected_completion[2];
uint64_t expected_completion_result[2]; struct micros_ipc_message expected_message;
bool message_completion_deferred; bool message_completion_observed;
bool captured_completion_observed; bool error_completion_observed;
bool error_completion_staged; bool idle_completion_observed;
volatile enum test_external_origin external_origin;
bool user_external_started; bool user_external_observed;
bool user_external_return_observed; bool supervisor_external_observed;
bool idle_external_started; bool idle_external_observed;
bool idle_external_completion_observed;
"""
INVALID_FIELDS = """
struct micros_process_handle processes[2]; struct micros_thread_handle threads[2];
uint64_t code_physical[2]; uint64_t counter_physical[2]; uint64_t stack_physical[2];
uintptr_t kernel_stack_bottom[2]; uintptr_t kernel_stack_top[2];
struct micros_kernel_objects timer_objects_snapshot;
struct micros_kernel_objects scheduler_snapshot_candidate;
struct micros_trap_frame timer_frame_snapshot; uint64_t timer_satp_snapshot;
"""
NATIVE_FIELDS = """
struct micros_kernel_objects objects; struct micros_process_handle process;
struct micros_thread_handle threads[8]; struct micros_hart_handle harts[2];
"""
CONTEXTS = (
    None,
    ("scheduler_runtime_context", "kernel/scheduler_context.c", RUNTIME_FIELDS),
    ("scheduler_test_context", "kernel/scheduler_test_state.c", NORMAL_FIELDS),
    ("scheduler_invalid_test_context", "kernel/scheduler_invalid_test_state.c", INVALID_FIELDS),
    ("scheduler_host_fixture", "tests/host/scheduler_test_fixture.c", NATIVE_FIELDS),
    ("scheduler_model_fixture", "tests/host/scheduler_model_fixture.c", NATIVE_FIELDS),
)
CORE_CONSUMERS = (
    "kernel_objects_test", "frame_ownership_test", "scheduler_test",
    "endpoint_test", "ipc_model_test", "bootstrap_control_test", "pm_test", "micros",
)
CASE_LABELS = (
    "priority queues preserve tail order", "runtime flags follow zero boundary",
    "policy replacement is atomic", "current selection repairs preemption",
    "quantum rotation retains nonpreemptible current",
    "current block and stale plan are atomic", "accounting owners are separate",
    "trap accounting uses minimal route", "corrupt queue state is rejected",
    "seeded two-hart model", "current IPC scheduler guard",
)
COVERAGE_FIELDS = (
    "preempt_higher", "enqueue_same", "enqueue_lower", "remove_middle",
    "flag_first", "flag_additional", "flag_partial_clear", "flag_final_clear",
    "return_keep", "return_switch", "return_idle", "quantum_renew",
    "thread_to_kernel", "kernel_to_thread", "kernel_to_idle", "idle_to_kernel",
    "policy_runnable", "policy_blocked",
)
QEMU_WORKFLOWS = (
    "test-qemu-smoke", "test-qemu-panic", "test-qemu-trap", "test-qemu-timer",
    "test-qemu-uart-console", "test-qemu-frame-allocator", "test-qemu-trap-panic",
    "test-qemu-mmu", "test-qemu-object-model", "test-qemu-endpoint",
    "test-qemu-grant", "test-qemu-grant-syscall", "test-qemu-user-runtime",
    "test-qemu-bootstrap-launcher", "test-qemu-vm-handoff", "test-qemu-pm-service",
    "test-qemu-vm-scratch-preemption", "test-qemu-tty", "test-qemu-ramfs",
    "test-qemu-vfs", "test-qemu-elf-spawn", "test-qemu-vm-ready-early", "test-qemu-vm-self-fault",
    "test-qemu-vm-self-fault-sealed", "test-qemu-bootstrap-ready-timeout",
    "test-qemu-bootstrap-manifest-panic", "test-qemu-ipc", "test-qemu-ipc-ecall-core",
    "test-qemu-ipc-syscall", "test-qemu-ipc-syscall-panic", "test-qemu-nested-trap",
    "test-qemu-frame-ownership", "test-qemu-user-address-space",
    "test-qemu-address-space-handoff", "test-qemu-user-execution",
    "test-qemu-scheduler", "test-qemu-scheduler-invalid-outgoing",
    "test-qemu-scheduler-invalid-next",
)
FAMILY_METADATA = (
    (True, QEMU_WORKFLOWS, ("test-ipc-model",), ()),
    (True, QEMU_WORKFLOWS, ("test-ipc-model",), ()),
    (False, ("test-qemu-scheduler",), (), ()),
    (False, ("test-qemu-scheduler-invalid-outgoing", "test-qemu-scheduler-invalid-next"), (), ()),
    (True, (), (), ()),
    (True, (), (), ()),
)

MODEL_OPERATION_HANDLERS = (
    (0, "apply_hold", "hold", "thread"),
    (1, "apply_wake", "wake", "thread"),
    (2, "apply_set_no_quantum", "set-no-quantum", "thread"),
    (3, "apply_clear_no_quantum", "clear-no-quantum", "thread"),
    (4, "apply_return", "return", "kernel"),
    (5, "apply_enter_thread", "enter-thread", None),
    (6, "apply_user_trap", "user-trap", None),
    (7, "apply_enter_idle", "enter-idle", None),
    (8, "apply_idle_trap", "idle-trap", None),
    (9, "apply_policy", "policy", "thread"),
)


def digest(text: str) -> str:
    return hashlib.sha256(compact(text).encode("utf-8")).hexdigest()


def family_index(path: str) -> int:
    for index, sources in enumerate(SOURCE_SETS.values()):
        if path in sources or path == PRIVATE_HEADERS[index]:
            return index
    raise AssertionError(f"unowned scheduler path: {path}")


def original_name(name: str, family: int) -> str:
    identities = {
        name for names in ORIGINAL_FUNCTIONS[ORIGINAL_SOURCES[family]].values()
        for name in names
    }
    if name in identities:
        return name
    prefix = FAMILY_PREFIXES[family]
    suffix = name[len(prefix):] if name.startswith(prefix) else ""
    return suffix if suffix in identities and not suffix.startswith("micros_") else name


def context_fields(family: int) -> tuple[str, ...]:
    context = CONTEXTS[family]
    if context is None:
        return ()
    return tuple(
        re.search(r"([A-Za-z_]\w*)\s*(?:\[[^]]+\])?\s*$", field).group(1)
        for field in context[2].split(";") if field.strip()
    )


def split_arguments(text: str) -> tuple[str, ...]:
    parts = []
    start = 0
    depth = 0
    for index, char in enumerate(c_lexical_text(text)):
        if char in "([{":
            depth += 1
        elif char in ")]}":
            depth -= 1
        elif char == "," and depth == 0:
            parts.append(text[start:index].strip())
            start = index + 1
    if text[start:].strip():
        parts.append(text[start:].strip())
    return tuple(parts)


def normalized_body(
    function: Function, family: int, records: dict[str, Function] | None = None,
) -> str:
    """Normalize only recognized private spellings and typed existing state."""
    text = function.body
    context = CONTEXTS[family]
    if context is not None:
        type_name = context[0]
        parameter_names = re.findall(
            rf"\bstruct\s+{type_name}\s*\*\s*(\w+)", function.signature,
        )
        local_pattern = (
            rf"\bstruct\s+{type_name}\s*\*\s*(\w+)\s*=\s*"
            rf"({FAMILY_PREFIXES[family]}\w+)\s*\(\s*\)\s*;"
        )
        locals_found = re.findall(local_pattern, text)
        context_names = parameter_names + [name for name, _ in locals_found]
        for name in context_names:
            fields = "|".join(map(re.escape, context_fields(family)))
            lexical = c_lexical_text(text)
            accesses = tuple(re.finditer(
                rf"\b{re.escape(name)}\s*->\s*({fields})\b", lexical,
            ))
            for access in reversed(accesses):
                if lexical[:access.start()].rstrip().endswith(("->", ".")):
                    raise AssertionError(f"member selector is not borrowed context: {name}")
                text = text[:access.start()] + access.group(1) + text[access.end():]
        text = re.sub(local_pattern, "", text)
        # A private original may acquire only this typed borrowed parameter.
        # Do not erase arbitrary extra call arguments or new helper calls.
        for called, arguments, start, end in reversed(call_sites(text)):
            callee = (records or {}).get(called)
            if callee is None or original_name(called, family) == called and called not in {
                name for names in ORIGINAL_FUNCTIONS[ORIGINAL_SOURCES[family]].values()
                for name in names
            }:
                continue
            formal_text = callee.signature[
                callee.signature.find("(") + 1:callee.signature.rfind(")")
            ]
            formals = split_arguments(formal_text)
            actuals = list(split_arguments(arguments))
            for index in range(len(formals) - 1, -1, -1):
                if re.fullmatch(rf"struct\s+{type_name}\s*\*\s*\w+", formals[index]):
                    if len(actuals) != len(formals) or actuals[index] not in context_names:
                        raise AssertionError(f"unverified context transport to {called}")
                    actuals.pop(index)
            text = text[:start] + called + "(" + ", ".join(actuals) + ")" + text[end:]
    return re.sub(
        r"\b[A-Za-z_]\w*\b",
        lambda match: original_name(match.group(), family),
        text,
    )


def signature_shape(signature: str, name: str) -> tuple[str, tuple[str, ...]]:
    lexical = c_lexical_text(signature)
    selected = re.search(rf"\b{re.escape(name)}\s*\(", lexical)
    if selected is None:
        raise AssertionError(f"missing function signature: {name}")
    opening = lexical.find("(", selected.start())
    closing = matching_lexical(lexical, opening)
    parameter_types = []
    type_words = {"void", "bool", "char", "short", "int", "long", "float", "double"}
    for argument in split_arguments(signature[opening + 1:closing]):
        identifiers = re.findall(r"\b[A-Za-z_]\w*\b", argument)
        if (
            len(identifiers) > 1
            and identifiers[-1] not in type_words
            and identifiers[-2] not in {"struct", "enum", "union", "const", "volatile"}
        ):
            argument = re.sub(r"\b[A-Za-z_]\w*\s*$", "", argument)
        parameter_types.append(compact(argument))
    return compact(signature[:selected.start()]), tuple(parameter_types)


def object_definitions(text: str) -> tuple[str, ...]:
    return tuple(
        declaration for declaration in file_scope_objects(text)
        if not (
            re.match(r"\s*extern\b", declaration)
            and "=" not in c_lexical_text(declaration)
        )
    )


def call_sites(text: str) -> tuple[tuple[str, str, int, int], ...]:
    lexical = c_statement_text(text)
    result = []
    for match in re.finditer(r"\b([A-Za-z_]\w*)\s*\(", lexical):
        name = match.group(1)
        if name in {"if", "for", "while", "switch", "sizeof", "_Alignof"}:
            continue
        open_at = lexical.find("(", match.start())
        end = matching_lexical(lexical, open_at)
        result.append((name, text[open_at + 1:end], match.start(), end + 1))
    return tuple(result)


def semantic_facts(text: str) -> Counter[str]:
    """Concrete calls, conditions, writes, and exits; no C evaluation/inlining."""
    lexical = c_lexical_text(text)
    statements = c_statement_text(text)
    text = "".join(
        original if before == after else after
        for original, before, after in zip(text, lexical, statements)
    )
    lexical = statements
    facts = Counter()
    for name, arguments, _, _ in call_sites(text):
        facts["call:" + digest(name + "(" + arguments + ")")] += 1
    for match in re.finditer(r"\b(if|for|while|switch)\s*\(", lexical):
        open_at = lexical.find("(", match.start())
        end = matching_lexical(lexical, open_at)
        facts["condition:" + digest(text[match.start():end + 1])] += 1
    for match in re.finditer(r"\b(return|goto)\b[^;]*;|\b(?:break|continue)\s*;", lexical):
        facts["exit:" + digest(text[match.start():match.end()])] += 1
    start = 0
    parens = 0
    for index, char in enumerate(lexical):
        if char == "(":
            parens += 1
        elif char == ")":
            parens -= 1
        elif char in "{}" and parens == 0:
            start = index + 1
        elif char == ":" and parens == 0 and re.fullmatch(
            r"\s*(?:case\s+(?:\d+|[A-Za-z_]\w*)|default)\s*:",
            lexical[start:index + 1],
        ):
            start = index + 1
        elif char == ";" and parens == 0:
            statement = text[start:index + 1]
            selected = c_lexical_text(statement)
            if re.search(r"(?<![=!<>])=(?!=)|\+\+|--|\+=|-=|&=|\|=", selected):
                facts["write:" + digest(statement)] += 1
            start = index + 1
    return facts


def fact_keys(text: str) -> Counter[str]:
    # A 96-bit SHA prefix keeps this finite inventory readable; complete
    # pending-body/stable-artifact fingerprints use the full SHA-256.
    return Counter({
        {"call": "C", "condition": "P", "write": "W", "exit": "E"}[
            key.split(":", 1)[0]
        ] + key.split(":", 1)[1][:24]: count
        for key, count in semantic_facts(text).items()
    })


def parsed_fact_inventory(frozen: str) -> Counter[str]:
    return Counter(frozen.split())


def fact_text(
    function: Function, family: int, records: dict[str, Function],
    automatic_model: bool = False,
) -> str:
    text = normalized_body(function, family, records)
    # Only original automatic values may be transported by typed reference.
    # This is not alias analysis and never normalizes registry/thread authority.
    for type_name, names in (
        ("micros_scheduler_return_plan", ("plan", "candidate", "rejected_plan")),
        ("scheduler_accounting_commit", ("accounting",)),
        ("scheduler_completion_commit", ("completion",)),
        ("micros_user_context", ("outgoing",)),
        ("model_state", ("model",)),
    ):
        for name in names:
            if not re.search(
                rf"\bstruct\s+{type_name}\s*\*\s*{name}\b", function.signature,
            ):
                continue
            if type_name == "micros_scheduler_return_plan" and name == "candidate":
                text = re.sub(
                    r"\bsizeof\s*\(\s*candidate\s*\)",
                    "sizeof_pointer_candidate",
                    text,
                )
                text = re.sub(
                    r"\bsizeof\s*\(\s*\*\s*candidate\s*\)",
                    "sizeof(candidate)",
                    text,
                )
                text = re.sub(
                    r"(\(\s*(?:const\s+)?unsigned\s+char\s*\*\s*\))\s*candidate\b",
                    r"\1 &candidate",
                    text,
                )
            text = re.sub(rf"\b{name}\s*->", name + ".", text)
            text = re.sub(rf"\(\s*\*\s*{name}\s*\)", name, text)
            if (
                type_name == "scheduler_accounting_commit" and name == "accounting"
                or automatic_model and type_name == "model_state" and name == "model"
            ):
                for called, arguments, start, end in reversed(call_sites(text)):
                    actuals = split_arguments(arguments)
                    if name in actuals:
                        transported = [
                            "&" + name if actual == name else actual
                            for actual in actuals
                        ]
                        text = (
                            text[:start] + called + "("
                            + ", ".join(transported) + ")" + text[end:]
                        )
    return text


def require_order(text: str, *fragments: str) -> None:
    selected = compact(text)
    offset = 0
    for fragment in fragments:
        expected = compact(fragment)
        position = selected.find(expected, offset)
        if position < 0:
            raise AssertionError(f"missing/reordered source fact: {fragment}")
        offset = position + len(expected)


def require_error_propagation(text: str, call: str, success: str) -> None:
    """Accept a direct return, or one exact checked result with a caller exit."""
    selected = compact(text)
    escaped = re.escape(call)
    if re.search(rf"\breturn {escaped}\s*\(", selected):
        return
    checked = re.search(
        rf"\b(\w+) = {escaped}\s*\([^;]*\) ;\s*"
        rf"if \( \1 != {re.escape(success)} \) \{{\s*"
        rf"(?:(?:riscv_irq_restore\s*\([^;]*\)|\([^;]*\)) ;\s*)?"
        rf"(?:return \1 ;|return map_object_error \( \1 \) ;|goto failure ;|"
        rf"(?:MICROS_(?:TRAP_)?PANIC|\w*panic\w*)\s*\()",
        selected,
    )
    if checked:
        return
    if re.search(
        rf"if \( ! {escaped}\s*\([^;]*?\) \) \{{ "
        r"(?:return false ;|return MICROS_SCHEDULER_TEST_MISMATCH ;|goto failure ;)", selected,
    ):
        return
    raise AssertionError(f"unchecked/dropped caller result: {call}")


def require_loop_projection(driver: str, step_helper: str = "") -> None:
    selected = compact(driver)
    require_order(
        driver, "for (step = 1; step <= MODEL_STEPS; ++step)",
        "next_random(&random_state)", "model_matches",
    )
    self_draws = [call for call, _, _, _ in call_sites(driver) if call == "next_random"]
    if len(self_draws) != 1:
        raise AssertionError("one post-update random draw per model step")
    lexical = c_statement_text(driver)
    loop = re.search(r"\bfor\s*\(\s*step\s*=", lexical)
    if loop is None:
        raise AssertionError("missing replay loop")
    opening = lexical.find("(", loop.start())
    closing = matching_lexical(lexical, opening)
    brace = closing + 1
    while brace < len(lexical) and lexical[brace].isspace():
        brace += 1
    if brace == len(lexical) or lexical[brace] != "{":
        raise AssertionError("model replay loop requires its complete body")
    end = matching_lexical(lexical, brace, "{", "}")
    body = driver[brace + 1:end]
    require_order(body, "next_random(&random_state)", "model_matches")
    if "continue" in tokens(body):
        raise AssertionError("skipped/rejected steps must reach common projection")
    if step_helper and re.search(
        rf"\breturn\s+{re.escape(step_helper)}\s*\(", c_statement_text(body),
    ):
        raise AssertionError("local helper success cannot exit the model caller")
    if re.search(r"\breturn\s+true\s*;", body[:body.rfind("model_matches")]):
        raise AssertionError("premature driver success bypasses common projection")


def guarded_success_conditions(text: str) -> tuple[str, ...]:
    lexical = c_statement_text(text)
    result = []
    for selected in re.finditer(r"\bif\s*\(", lexical):
        opening = lexical.find("(", selected.start())
        closing = matching_lexical(lexical, opening)
        brace = closing + 1
        while brace < len(lexical) and lexical[brace].isspace():
            brace += 1
        if brace < len(lexical) and lexical[brace] == "{":
            end = matching_lexical(lexical, brace, "{", "}")
            if compact(text[brace + 1:end]) == "return true ;":
                result.append(compact(text[selected.start():closing + 1]))
    return tuple(result)


def model_operation_continuation_facts(
    driver: Function, dispatch: Function, records: dict[str, Function],
) -> Counter[str]:
    require_loop_projection(driver.body, dispatch.name)
    require_error_propagation(driver.body, dispatch.name, "true")
    entries = [arguments for name, arguments, _, _ in call_sites(driver.body) if name == dispatch.name]
    if len(entries) != 1 or split_arguments(entries[0])[-1] != "&operation":
        raise AssertionError("model step must retain its exact caller-owned operation label")
    lexical = c_statement_text(dispatch.body)
    switch = re.search(r"\bswitch\s*\(\s*value\s*%\s*10\s*\)", lexical)
    if switch is None:
        raise AssertionError("model operation decoder changed")
    opening = lexical.find("{", switch.end())
    end = matching_lexical(lexical, opening, "{", "}")
    body = dispatch.body[opening + 1:end]
    labels = tuple(re.finditer(r"\bcase\s+(\d+)\s*:|\bdefault\s*:", c_statement_text(body)))
    observed = tuple(int(label.group(1)) if label.group(1) is not None else 9 for label in labels)
    if observed != tuple(contract[0] for contract in MODEL_OPERATION_HANDLERS):
        raise AssertionError("model case order or membership changed")
    skips = 0
    for index, (_, name, operation, kind) in enumerate(MODEL_OPERATION_HANDLERS):
        case_end = labels[index + 1].start() if index + 1 < len(labels) else len(body)
        case = body[labels[index].end():case_end]
        calls = [arguments for called, arguments, _, _ in call_sites(case) if called == name]
        if len(calls) != 1 or split_arguments(calls[0])[-1] != "operation_out":
            raise AssertionError("operation label output lost across dispatch")
        require_error_propagation(case, name, "true")
        if not compact(case).endswith("break ;") or tokens(case).count("break") != 1:
            raise AssertionError("successful operation must leave only its dispatch case")
        if name not in records:
            raise AssertionError(f"missing model operation: {name}")
        handler = records[name]
        text = fact_text(handler, 5, records, automatic_model=True)
        require_order(text, f'operation = "{operation}";', "*operation_out = operation;")
        if any(called in {"next_random", "model_matches"} for called, _, _, _ in call_sites(text)):
            raise AssertionError("an operation cannot draw randomness or replace common projection")
        conditions = guarded_success_conditions(text)
        expected = ()
        if kind == "thread":
            expected = (compact(
                "if (thread->assigned && model.harts[hart].accounting_owner "
                "== MICROS_SCHEDULER_ACCOUNTING_THREAD)"
            ),)
        elif kind == "kernel":
            expected = (compact(
                "if (model.harts[hart].accounting_owner "
                "!= MICROS_SCHEDULER_ACCOUNTING_KERNEL)"
            ),)
        if conditions != expected:
            raise AssertionError("skipped operation changed its original accounting-owner guard")
        if not compact(text).endswith("return true ;"):
            raise AssertionError("operation success lost its local continuation")
        if len(re.findall(r"\breturn\s+true\s*;", c_statement_text(text))) != len(expected) + 1:
            raise AssertionError("unverified early operation success")
        if "break" in tokens(text) or "continue" in tokens(text):
            raise AssertionError("an operation helper cannot exit its caller's control scope")
        skips += len(expected)
    return Counter({key: count * skips for key, count in fact_keys("break;").items()})


def require_selector_modes(normal: str, captured: str, selector: str) -> None:
    require_order(normal, "return select_user_return(hart, frame, true);")
    require_order(captured, "return select_user_return(hart, frame, false);")
    require_order(
        selector, "if (store_outgoing)", "micros_user_execution_validate_context",
        "micros_user_execution_store_context",
        "else if (hart->current_thread.slot != 0 || hart->current_thread.generation != 0)",
        "return MICROS_SCHEDULER_ERROR_STATE;",
    )


def require_selected_sequence(text: str) -> None:
    require_order(
        text, "validate_selected_thread", "preflight_completion_or_panic",
        "load_selected_context", "micros_timer_prepare_return",
        "preflight_kernel_interval", "commit_completion_or_panic",
        "micros_scheduler_apply_return_plan", "enter_selected_thread",
    )


def require_same_image(text: str) -> None:
    require_order(
        text, "micros_ipc_buffer_commit_write(&completion->buffer_plan, &completion->message)",
        "selected_context.a0 = completion->abi_result;",
        "copy_user_context(&completion->thread->user_context, &selected_context)",
        "completion->thread->ipc_receive_buffer = 0;",
        "completion->thread->ipc_delivery_pending = false;",
        "clear_bytes(&completion->thread->ipc_inbound_message",
        "completion->thread->ipc_staged_result = MICROS_IPC_OK;",
    )
    if re.search(r"\bmicros_(?:user_address_space|ipc_buffer_prepare)_", text):
        raise AssertionError("fallible translation after completion commit")


def require_all_record_preflight(text: str) -> None:
    require_order(
        text, "other_index < index", "micros_scheduler_core_validate",
        "resolve_thread_mutable", "preflight_tail_enqueue",
        "for (index = 0; index < request_count; ++index) {",
        "transitions[index].thread->runtime_flags = transitions[index].resulting_flags;",
        "queue_enqueue_tail_prevalidated",
    )
    selected = compact(text)
    first_store = selected.find("transitions [ index ] . thread -> runtime_flags =")
    last_preflight = selected.rfind("preflight_tail_enqueue")
    if first_store < last_preflight:
        raise AssertionError("first-record mutation precedes all-record preflight")
    loops = []
    lexical = c_lexical_text(text)
    for match in re.finditer(
        r"\bfor\s*\(\s*index\s*=\s*0\s*;\s*index\s*<\s*request_count\s*;"
        r"\s*\+\+index\s*\)\s*\{", lexical,
    ):
        opening = lexical.find("{", match.start())
        loops.append(text[opening + 1:matching(text, opening, "{", "}")])
    candidates = [loop for loop in loops if "resolve_thread_mutable" in loop]
    if len(candidates) != 1 or "preflight_tail_enqueue" not in candidates[0]:
        raise AssertionError("preflight must visit every input candidate")
    if re.search(
        r"transitions\s*\[\s*index\s*\]\s*\.\s*thread\s*->\s*runtime_flags\s*=",
        candidates[0],
    ):
        raise AssertionError("authoritative store inside candidate preflight loop")


def require_minimal_accounting(text: str, idle: bool = False) -> None:
    names = {name for name, _, _, _ in call_sites(text)}
    allowed = {
        "resolve_hart_mutable", "thread_handle_is_null", "thread_handles_equal",
        "null_thread_handle", "resolve_thread_mutable", "hart_handles_equal",
        "add_overflows",
    }
    if names - allowed:
        raise AssertionError(f"non-minimal trap accounting calls: {sorted(names - allowed)}")
    require_order(text, "delta = counter - hart->accounting_started_at;")
    if idle:
        require_order(
            text, "add_overflows(hart->idle_counter_ticks, delta)",
            "return MICROS_KERNEL_OBJECT_ERROR_OVERFLOW;",
            "hart->idle_counter_ticks += delta;",
        )
    else:
        require_order(
            text, "delta >= thread->remaining_counter_ticks ? 0 : thread->remaining_counter_ticks - delta",
        )
    require_order(
        text, "hart->accounting_owner = MICROS_SCHEDULER_ACCOUNTING_KERNEL;",
        "hart->accounting_started_at = counter;",
    )


def require_guarded_next_corruption(
    root: Function, records: dict[str, Function],
) -> None:
    expected = ("ifdef MICROS_BUILD_SCHEDULER_INVALID_NEXT_TEST",)
    sites = []
    pattern = (
        r"\bobjects\s*->\s*threads\s*\[\s*threads\s*\[\s*1\s*\]"
        r"\s*\.\s*slot\s*\]\s*\.\s*user_context\s*\.\s*sp\s*=\s*1\s*;"
    )
    for record in records.values():
        body = normalized_body(record, 3, records)
        for selected in re.finditer(pattern, c_lexical_text(body)):
            guards = record.guards + cpp_guards(body, selected.start())
            sites.append((record.name, guards))
    if len(sites) != 1 or sites[0][1] != expected:
        raise AssertionError("next-context corruption requires its one exact profile guard")
    owner = sites[0][0]
    if owner != root.name:
        calls = [
            start for name, _, start, _ in call_sites(root.body) if name == owner
        ]
        if len(calls) != 1 or cpp_guards(root.body, calls[0]) != expected:
            raise AssertionError("next-context injection call escaped its profile guard")


def frozen_functions(original: str) -> dict[str, tuple[int, str]]:
    return {
        name: (int(lines), fingerprint)
        for name, lines, fingerprint in (
            line.split() for line in FROZEN_FUNCTIONS[original].splitlines()
            if line.strip()
        )
    }


def source_texts() -> dict[str, str]:
    return {
        path: kernel_layout_source.read_path(REPOSITORY_ROOT / path, encoding="utf-8")
        for path in (*ALL_SOURCES, *PRIVATE_HEADERS)
        if kernel_layout_source.is_file_path(REPOSITORY_ROOT / path)
    }


def family_records(texts: dict[str, str], family: int) -> dict[str, Function]:
    result = {}
    for path in tuple(SOURCE_SETS.values())[family]:
        for name, record in function_records(texts.get(path, "")).items():
            if name in result:
                raise AssertionError(f"duplicate definition in family: {name}")
            result[name] = record
    return result


def find_original(records: dict[str, Function], family: int, name: str) -> Function:
    matches = [
        record for spelling, record in records.items()
        if original_name(spelling, family) == name
    ]
    if len(matches) != 1:
        raise AssertionError(f"expected exactly one original definition: {name}")
    return matches[0]


def phase_records(
    root: Function, family: int, records: dict[str, Function],
    active: tuple[str, ...] = (),
) -> tuple[Function, ...]:
    """Visit only new named phases; original callees keep their own obligations."""
    if root.name in active:
        raise AssertionError(f"recursive phase: {root.name}")
    result = [root]
    identities = {
        name for names in ORIGINAL_FUNCTIONS[ORIGINAL_SOURCES[family]].values()
        for name in names
    }
    for name, _, _, _ in call_sites(root.body):
        if name in records and original_name(name, family) not in identities:
            result.extend(phase_records(
                records[name], family, records, (*active, root.name),
            ))
    if len(result) > 128:
        raise AssertionError("scheduler phase graph exceeds its finite contract")
    return tuple(result)


def phase_text(
    root: Function, family: int, records: dict[str, Function],
    automatic_model: bool = False,
) -> str:
    """Order source facts at concrete phase call sites, without interpreting C."""
    phase_records(root, family, records)
    automatic_model = automatic_model or root.name == "micros_scheduler_model_test_run"
    text = fact_text(root, family, records, automatic_model)
    original_identities = {
        name for names in ORIGINAL_FUNCTIONS[ORIGINAL_SOURCES[family]].values()
        for name in names
    }
    for name, _, _, end in reversed(call_sites(text)):
        if name in records and original_name(name, family) not in original_identities:
            text = (
                text[:end] + "\n{"
                + phase_text(records[name], family, records, automatic_model)
                + "}\n" + text[end:]
            )
    return text


def phase_call_names(root: Function, family: int, records: dict[str, Function]) -> tuple[str, ...]:
    identities = frozen_functions(ORIGINAL_SOURCES[family])
    result = []
    for name, _, _, _ in call_sites(root.body):
        result.append(original_name(name, family))
        if name in records and original_name(name, family) not in identities:
            result.extend(phase_call_names(records[name], family, records))
    return tuple(result)


def macro_records(text: str) -> tuple[tuple[str, str], ...]:
    result = []
    lines = macro_definition_text(text).splitlines()
    index = 0
    while index < len(lines):
        block = [lines[index]]
        name = re.match(r"\s*#\s*define\s+(\w+)", block[0]).group(1)
        while block[-1].rstrip().endswith("\\"):
            index += 1
            block.append(lines[index])
        joined = "\n".join(block)
        if not re.fullmatch(r"\s*#\s*define\s+\w+\s*", joined):
            result.append((name, joined))
        index += 1
    return tuple(result)


def strip_static(text: str) -> str:
    return re.sub(r"\bstatic\s+", "", text)


def struct_body(text: str, name: str) -> str:
    matches = tuple(re.finditer(rf"\bstruct\s+{re.escape(name)}\s*\{{", c_lexical_text(text)))
    if len(matches) != 1:
        raise AssertionError(f"expected one complete struct {name}")
    start = text.find("{", matches[0].start())
    return text[start + 1:matching(text, start, "{", "}")]


def metadata_for(path: str) -> tuple:
    return (
        validation_plan.requires_full_tier([path]),
        tuple(validation_plan.affected_workflows([path])),
        tuple(validation_plan.affected_slow_models([path])),
        tuple(validation_plan.affected_image_workflows([path])),
    )


# Role words constrain named work, not cosmetic helper spelling. Every
# discharged long root needs at least two new substantive phases as well.
PHASE_ROLES = {
    "micros_scheduler_commit_ipc_transitions": (
        r"shape|duplicate|argument", r"validat", r"preflight|candidate", r"commit|apply",
    ),
    "micros_scheduler_core_validate": (
        r"base", r"witness|seen", r"hart|account", r"queue", r"current|stack", r"thread|membership",
    ),
    "micros_hart_plan_user_return": (
        r"clear|preflight|initial", r"load", r"repair|preempt", r"select|renew",
        r"project|queue", r"publish|copy",
    ),
    "select_user_return": (
        r"outgoing|custody|argument", r"plan", r"idle", r"selected|context",
        r"timer", r"account", r"commit|install",
    ),
    "micros_scheduler_test_handle_user_trap": (
        r"common|context|counter", r"ecall", r"message", r"captured", r"timer|error",
        r"external", r"idle|hold",
    ),
    "finish_test": (r"proof|external", r"evidence|check", r"teardown|release", r"balance|baseline", r"report|finish"),
    "micros_scheduler_runtime_run_self_test": (
        r"initial|baseline|setup", r"process|image|context", r"admi|fence",
        r"malformed", r"start|failure",
    ),
    "micros_scheduler_invalid_runtime_run_self_test": (
        r"initial|payload|setup", r"process|image|context", r"admi|fence", r"corrupt|next",
    ),
    "test_accounting_owners_are_separate": (
        r"fixture|initial|select", r"reject|owned|plan", r"thread|interval",
        r"idle", r"wrap|reset",
    ),
    "model_matches": (r"validat", r"hart|account|current", r"queue", r"thread|policy|link"),
    "run_required_prelude": (
        r"hart_?0|initial|preempt", r"hart_?1|placement", r"policy|flag",
        r"exhaust|rotat", r"idle|wake",
    ),
    "micros_scheduler_model_test_run": (
        r"fixture|initial", r"prelude", r"step|draw|decod|operat",
        r"project|matches", r"coverage",
    ),
    "micros_scheduler_start": (r"preflight|context", r"timer", r"account", r"commit|enter"),
    "micros_scheduler_idle_select": (r"preflight|plan", r"idle", r"selected|context", r"timer", r"account", r"commit|enter"),
}


class SchedulerSourceLayoutTest(unittest.TestCase):
    maxDiff = 2000

    @classmethod
    def setUpClass(cls) -> None:
        cls.texts = source_texts()
        cls.records = tuple(family_records(cls.texts, index) for index in range(6))
        cls.cmake = kernel_layout_source.read_path(REPOSITORY_ROOT / "CMakeLists.txt", encoding="utf-8")

    def original(self, family: int, name: str) -> str:
        record = find_original(self.records[family], family, name)
        return normalized_body(record, family, self.records[family])

    def phases(self, family: int, name: str) -> str:
        record = find_original(self.records[family], family, name)
        return phase_text(record, family, self.records[family])

    def test_exact_inventory_and_frozen_migration_cardinality(self) -> None:
        self.assertEqual(6, len(SOURCE_SETS))
        self.assertEqual(38, len(ALL_SOURCES))
        self.assertEqual(38, len(set(ALL_SOURCES)))
        self.assertEqual(6, len(PRIVATE_HEADERS))
        self.assertEqual((60, 34, 28, 11, 13, 17), tuple(
            sum(len(names) for names in owners.values())
            for owners in ORIGINAL_FUNCTIONS.values()
        ))
        self.assertEqual((47, 29, 26, 9, 12, 15), tuple(
            sum(original == path for original, _, _ in INITIAL_SYMBOL_OWNER_MIGRATION)
            for path in ORIGINAL_SOURCES
        ))
        self.assertEqual(138, len(INITIAL_SYMBOL_OWNER_MIGRATION))
        self.assertEqual(12, len(INITIAL_FUNCTION_MIGRATION))
        self.assertEqual(17, len(STABLE_ARTIFACT_HASHES))
        self.assertEqual(163, sum(len(frozen_functions(path)) for path in ORIGINAL_SOURCES))
        self.assertEqual(set((*INITIAL_FUNCTION_MIGRATION, *SHORT_PHASE_ROOTS)), set(FROZEN_FACTS))
        self.assertEqual(38, len(QEMU_WORKFLOWS))
        for original, owners in ORIGINAL_FUNCTIONS.items():
            self.assertEqual(
                set(frozen_functions(original)),
                {name for names in owners.values() for name in names},
            )
        missing = [path for path in (*ALL_SOURCES, *PRIVATE_HEADERS) if path not in self.texts]
        self.assertEqual([], missing, "accepted 38/six inventory is not implemented")
        discovered = {
            path.relative_to(REPOSITORY_ROOT).as_posix()
            for pattern in ("kernel/scheduler*.c", "tests/host/scheduler*.c")
            for path in kernel_layout_source.glob_paths(REPOSITORY_ROOT, pattern)
        }
        self.assertEqual(set(ALL_SOURCES), discovered)
        headers = {
            path.relative_to(REPOSITORY_ROOT).as_posix()
            for pattern in ("kernel/scheduler*.h", "tests/host/scheduler*.h")
            for path in kernel_layout_source.glob_paths(REPOSITORY_ROOT, pattern)
        }
        self.assertEqual({*PRIVATE_HEADERS, "kernel/scheduler_core_internal.h"}, headers)

    def test_active_migrations_only_shrink_and_empty_is_literal(self) -> None:
        import ast

        for initial, active, name in (
            (INITIAL_SYMBOL_OWNER_MIGRATION, SYMBOL_OWNER_MIGRATION, "SYMBOL_OWNER_MIGRATION"),
            (INITIAL_FUNCTION_MIGRATION, FUNCTION_MIGRATION, "FUNCTION_MIGRATION"),
        ):
            self.assertEqual(len(active), len(set(active)))
            self.assertLessEqual(set(active), set(initial))
            if not active:
                module = ast.parse(kernel_layout_source.read_path(Path(__file__), encoding="utf-8"))
                values = [
                    node.value for node in module.body if isinstance(node, ast.Assign)
                    and any(isinstance(target, ast.Name) and target.id == name for target in node.targets)
                ]
                self.assertEqual(1, len(values))
                self.assertIsInstance(values[0], (ast.Tuple, ast.List, ast.Set, ast.Dict))
                self.assertEqual(0, len(values[0].elts if hasattr(values[0], "elts") else values[0].keys))

    def test_all_original_owners_and_pending_bodies_are_frozen(self) -> None:
        pending = {(path, name): final for path, final, name in SYMBOL_OWNER_MIGRATION}
        for family, (original, owners) in enumerate(ORIGINAL_FUNCTIONS.items()):
            frozen = frozen_functions(original)
            for final, identities in owners.items():
                for name in identities:
                    with self.subTest(original=original, symbol=name):
                        matches = [
                            (path, record)
                            for path in tuple(SOURCE_SETS.values())[family]
                            for spelling, record in function_records(self.texts.get(path, "")).items()
                            if original_name(spelling, family) == name
                        ]
                        expected = original if (original, name) in pending else final
                        self.assertEqual([expected], [path for path, _ in matches])
                        if len(matches) != 1:
                            continue
                        record = matches[0][1]
                        self.assertEqual(FUNCTION_GUARDS.get(name, ()), record.guards)
                        is_phase_root = (original, name) in (*INITIAL_FUNCTION_MIGRATION, *SHORT_PHASE_ROOTS)
                        is_pending = (original, name) in pending or (original, name) in FUNCTION_MIGRATION
                        if is_pending or not is_phase_root:
                            self.assertEqual(
                                frozen[name][1],
                                digest(normalized_body(record, family, self.records[family])),
                                "pending/non-phase bodies preserve predicates, arguments, writes and exits",
                            )
                        if name.startswith("micros_"):
                            self.assertEqual(name, record.name, "supported names are not private aliases")

    def test_complete_original_supported_definition_signatures(self) -> None:
        headers = "\n".join(
            kernel_layout_source.read_path(REPOSITORY_ROOT / path, encoding="utf-8")
            for path in (
                "include/micros/scheduler.h", "include/micros/scheduler_core.h",
                "kernel/scheduler_core_internal.h",
            )
        )
        declarations = c_declaration_text(headers)
        for family in (0, 1):
            for record in self.records[family].values():
                if not record.name.startswith("micros_"):
                    continue
                match = re.search(rf"\b{record.name}\s*\([^;]*\)\s*;", declarations)
                if match is None:
                    # Assembly-private entry points already existed locally.
                    self.assertIn(record.name, {
                        "micros_scheduler_idle_select", "micros_scheduler_idle_accounting_panic",
                    })
                    continue
                boundary = max(
                    declarations.rfind(";", 0, match.start()),
                    declarations.rfind("}", 0, match.start()),
                ) + 1
                start = c_signature_start(declarations, boundary, match.start())
                expected = signature_shape(
                    declarations[start:match.end()].rstrip("; \n"), record.name,
                )
                self.assertEqual(expected, signature_shape(record.signature, record.name))

    def test_stable_artifacts_are_literal_reviewed_base_hashes(self) -> None:
        for path, expected in STABLE_ARTIFACT_HASHES.items():
            with self.subTest(path=path):
                value = (
                    restored_kernel_objects_source().encode() if path == "kernel/kernel_objects.c"
                    else kernel_layout_source.read_bytes_path(REPOSITORY_ROOT / path)
                )
                self.assertEqual(expected, hashlib.sha256(value).hexdigest())

    def test_cmake_exact_ordered_sets_and_every_existing_consumer(self) -> None:
        for name, paths in SOURCE_SETS.items():
            with self.subTest(source_set=name):
                block = cmake_command(self.cmake, "set", name)
                self.assertEqual(paths, tuple(re.findall(r"(?:kernel|tests/host)/\w+\.c", block)))
        for path in ALL_SOURCES:
            self.assertEqual(1, self.cmake.count(path), path)
        consumers = {}
        for target in re.findall(r"\badd_executable\s*\(\s*(\w+)", self.cmake):
            block = cmake_command(self.cmake, "add_executable", target)
            if "${MICROS_SCHEDULER_CORE_SOURCES}" in block:
                consumers[target] = block
        self.assertEqual(set(CORE_CONSUMERS), set(consumers))
        for target, block in consumers.items():
            self.assertEqual(1, block.count("${MICROS_SCHEDULER_CORE_SOURCES}"), target)
        micros = cmake_command(self.cmake, "add_executable", "micros")
        self.assertEqual(1, micros.count("${MICROS_SCHEDULER_RUNTIME_SOURCES}"))
        scheduler = cmake_command(self.cmake, "add_executable", "scheduler_test")
        require_order(
            scheduler, "${MICROS_KERNEL_OBJECT_CORE_SOURCES}", "${MICROS_SCHEDULER_CORE_SOURCES}",
            "tests/host/ipc_current_guard_test.c", "${MICROS_SCHEDULER_MODEL_HOST_TEST_SOURCES}",
            "${MICROS_SCHEDULER_HOST_TEST_SOURCES}",
        )
        for source_set, condition in (
            ("MICROS_SCHEDULER_QEMU_TEST_SOURCES", "MICROS_BUILD_SCHEDULER_TEST"),
            ("MICROS_SCHEDULER_INVALID_QEMU_TEST_SOURCES", "MICROS_BUILD_SCHEDULER_INVALID_OUTGOING_TEST OR MICROS_BUILD_SCHEDULER_INVALID_NEXT_TEST"),
        ):
            blocks = cmake_if_blocks(" ".join(self.cmake.split()), condition)
            relevant = [block for block in blocks if "${" + source_set + "}" in block]
            self.assertEqual(1, len(relevant), source_set)
            require_order(relevant[0], "arch/riscv64/scheduler_test.S", "${" + source_set + "}")
            self.assertEqual(1, self.cmake.count("${" + source_set + "}"))
        options = cmake_command(self.cmake, "set", "MICROS_TARGET_TEST_OPTIONS")
        for option in (
            "MICROS_BUILD_SCHEDULER_TEST", "MICROS_BUILD_SCHEDULER_INVALID_OUTGOING_TEST",
            "MICROS_BUILD_SCHEDULER_INVALID_NEXT_TEST",
        ):
            self.assertEqual(1, options.split().count(option))
        require_order(
            self.cmake, "foreach(option_name IN LISTS MICROS_TARGET_TEST_OPTIONS)",
            "if(${option_name})", "math(EXPR MICROS_TARGET_TEST_COUNT",
            "if(MICROS_TARGET_TEST_COUNT GREATER 1)", "message(FATAL_ERROR",
        )

    def test_scheduler_target_ctest_tiers_and_qemu_commands_stay_owned(self) -> None:
        require_order(
            cmake_command(self.cmake, "target_compile_features", "scheduler_test"),
            "scheduler_test PRIVATE c_std_17",
        )
        includes = cmake_command(self.cmake, "target_include_directories", "scheduler_test")
        self.assertIn('"${CMAKE_CURRENT_SOURCE_DIR}/include"', includes)
        self.assertIn('"${CMAKE_CURRENT_SOURCE_DIR}"', includes)
        warnings = cmake_command(self.cmake, "target_compile_options", "scheduler_test")
        require_order(warnings, "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-Wmissing-prototypes", "-Wstrict-prototypes")
        sanitizer = "\n".join(cmake_if_blocks(
            " ".join(self.cmake.split()),
            'MICROS_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU"',
        ))
        require_order(
            cmake_command(sanitizer, "target_compile_options", "scheduler_test"),
            "-fno-omit-frame-pointer", "-fsanitize=address,undefined",
        )
        self.assertIn("-fsanitize=address,undefined", cmake_command(sanitizer, "target_link_options", "scheduler_test"))
        self.assertEqual(1, len(re.findall(r"add_test\s*\(\s*NAME\s+scheduler-core\s+COMMAND\s+scheduler_test\s*\)", self.cmake)))
        for tier in ("test-unit-fast", "test-unit"):
            self.assertRegex(cmake_command(self.cmake, "add_custom_target", tier), r"\bDEPENDS\b[\s\S]*\bscheduler_test\b")
        self.assertNotRegex(self.cmake, r"set_tests_properties\s*\(\s*scheduler-core\b")
        for name, expected in FROZEN_QEMU_COMMANDS.items():
            self.assertEqual(expected, digest(cmake_command(self.cmake, "add_custom_target", name)), name)

    def test_validation_metadata_independent_exact_four_dimensions(self) -> None:
        self.assertEqual(QEMU_WORKFLOWS, validation_plan.QEMU_WORKFLOWS)
        equivalences = dict(validation_plan.PATH_CLASSIFICATION_EQUIVALENTS)
        for family, (source_set, paths) in enumerate(SOURCE_SETS.items()):
            expected_children = (*paths[1:], PRIVATE_HEADERS[family])
            with self.subTest(source_set=source_set):
                self.assertEqual(expected_children, equivalences.get(paths[0]))
            for path in (*paths, PRIVATE_HEADERS[family]):
                with self.subTest(path=path):
                    self.assertEqual(FAMILY_METADATA[family], metadata_for(path))
        self.assertIn(STRUCTURAL_TEST_PATH, validation_plan.HOST_UNIT_ONLY_INPUTS)
        self.assertEqual((False, (), (), ()), metadata_for(STRUCTURAL_TEST_PATH))

    def test_private_headers_are_declarations_only_and_family_qualified(self) -> None:
        for path, text in self.texts.items():
            with self.subTest(path=path):
                self.assertNotRegex(text, r'(?m)^\s*#\s*include\s*[<"][^">]+\.c[">]')
                self.assertNotRegex(text, r"#\s*(?:define|undef)\s+__(?:FILE|LINE)__")
                family = family_index(path)
                for include in re.findall(r'(?m)^\s*#\s*include\s*"([^"]+)"', text):
                    if Path(include).name == "scheduler_test_internal.h":
                        self.assertEqual(PRIVATE_HEADERS[family], include)
                    if include in PRIVATE_HEADERS:
                        self.assertEqual(PRIVATE_HEADERS[family], include)
                    self.assertNotIn(
                        include,
                        tuple(header for index, header in enumerate(PRIVATE_HEADERS) if index != family),
                    )
                if path not in PRIVATE_HEADERS:
                    continue
                self.assertEqual({}, c_function_definitions(text))
                self.assertEqual((), object_definitions(text))
                for name, macro in macro_records(text):
                    if name in {"EXPECT_TRUE", "EXPECT_ERROR", "EXPECT_UNCHANGED", "MODEL_FAIL"}:
                        self.assertEqual(4 if name.startswith("EXPECT") else 5, family)
                    else:
                        self.assertNotRegex(
                            macro, rf"^\s*#\s*define\s+{re.escape(name)}\(",
                            "new executable macro",
                        )
                        self.assertNotRegex(macro, r"\b(?:return|if|for|while|goto)\b|->|[{};]")
                self.assertNotRegex(text, r"\bextern\s+(?!const\b|const\s)[^;()]*\b(?:objects|context|fixture|plan_scratch)\s*;")

    def test_exact_state_consolidations_and_no_new_global_duration(self) -> None:
        for family, (source_set, paths) in enumerate(SOURCE_SETS.items()):
            context = CONTEXTS[family]
            declarations = [
                (path, declaration)
                for path in (*paths, PRIVATE_HEADERS[family])
                for declaration in object_definitions(self.texts.get(path, ""))
                if not re.search(r"\bconst\b", declaration)
            ]
            if family == 0:
                workspace = [(path, text) for path, text in declarations if re.search(r"\bplan_scratch\b", text)]
                self.assertEqual(1, len(workspace))
                expected_owner = (
                    paths[0] if any(
                        name == "micros_hart_plan_user_return"
                        for _, _, name in SYMBOL_OWNER_MIGRATION
                    ) else "kernel/scheduler_core_return_plan.c"
                )
                self.assertEqual(expected_owner, workspace[0][0])
                self.assertEqual(
                    compact("static struct scheduler_plan_scratch plan_scratch;"),
                    compact(workspace[0][1]),
                )
                self.assertEqual(workspace, declarations, "no second scratch/cache/authority")
                full_text = "\n".join(self.texts.get(path, "") for path in (*paths, PRIVATE_HEADERS[family]))
                self.assertEqual(
                    compact("uint16_t slots[MICROS_SCHEDULER_PRIORITY_COUNT][MICROS_THREAD_CAPACITY]; size_t counts[MICROS_SCHEDULER_PRIORITY_COUNT];"),
                    compact(struct_body(full_text, "scheduler_plan_scratch")),
                )
                continue
            type_name, owner, fields = context
            consolidated = [(path, declaration) for path, declaration in declarations if re.search(rf"\bstruct\s+{type_name}\b", declaration)]
            if not consolidated:
                # Only untouched, explicitly pending families may retain old state.
                expected_pending = {
                    entry for entry in INITIAL_SYMBOL_OWNER_MIGRATION if entry[0] == paths[0]
                }
                self.assertLessEqual(expected_pending, set(SYMBOL_OWNER_MIGRATION), source_set)
                legacy = [
                    compact("static " + declaration.strip() + (" = 2" if " last_running" in declaration else "") + ";")
                    for declaration in fields.split(";") if declaration.strip()
                ]
                if family == 4:
                    legacy = [entry.replace("[ 8 ]", "[ TEST_THREAD_COUNT ]") for entry in legacy]
                if family == 5:
                    legacy = [
                        entry.replace("[ 8 ]", "[ MODEL_THREAD_COUNT ]").replace("[ 2 ]", "[ MODEL_HART_COUNT ]")
                        for entry in legacy
                    ]
                actual = [compact(declaration) for _, declaration in declarations]
                if family == 2:
                    legacy.extend(compact("volatile uint64_t " + name + ";") for name in (
                        "micros_scheduler_test_idle_bypass_once", "micros_scheduler_test_idle_bypass_observed",
                    ))
                self.assertCountEqual(legacy, actual, "only reviewed original private fields may remain pending")
                continue
            self.assertEqual(1, len(consolidated), source_set)
            self.assertEqual(owner, consolidated[0][0])
            self.assertRegex(consolidated[0][1], rf"^static\s+struct\s+{type_name}\b")
            full_text = "\n".join(self.texts.get(path, "") for path in (*paths, PRIVATE_HEADERS[family]))
            actual_fields = struct_body(full_text, type_name)
            actual_fields = re.sub(r"\b(?:TEST|MODEL)_THREAD_COUNT\b", "8", actual_fields)
            actual_fields = re.sub(r"\bMODEL_HART_COUNT\b", "2", actual_fields)
            self.assertEqual(compact(fields), compact(actual_fields), source_set)
            initializer = consolidated[0][1].split("=", 1)
            if family == 2:
                self.assertEqual(2, len(initializer))
                self.assertEqual(compact("{.last_running = 2};"), compact(initializer[1]))
            elif len(initializer) == 2:
                self.assertEqual(compact("{0};"), compact(initializer[1]))
            extra = [entry for entry in declarations if entry not in consolidated]
            expected_extra = []
            if family == 2:
                expected_extra = [
                    ("kernel/scheduler_test.c", compact("volatile uint64_t " + name + ";"))
                    for name in ("micros_scheduler_test_idle_bypass_once", "micros_scheduler_test_idle_bypass_observed")
                ]
            self.assertCountEqual(expected_extra, [(path, compact(text)) for path, text in extra])
            storage = re.search(rf"\bstruct\s+{type_name}\s+(\w+)", consolidated[0][1]).group(1)
            for record in self.records[family].values():
                for variable, getter in re.findall(
                    rf"\bstruct\s+{type_name}\s*\*\s*(\w+)\s*=\s*({FAMILY_PREFIXES[family]}\w+)\s*\(\s*\)\s*;",
                    record.body,
                ):
                    self.assertIn(getter, self.records[family], variable)
                    borrow = self.records[family][getter]
                    self.assertEqual(compact("return &" + storage + ";"), compact(borrow.body))
                    self.assertRegex(borrow.signature, rf"\bstruct\s+{type_name}\s*\*")
                    self.assertIn(borrow.name, function_records(self.texts[owner]))

    def test_automatic_witness_candidates_plans_snapshots_and_entry_custody(self) -> None:
        for family, records in enumerate(self.records):
            for record in records.values():
                self.assertNotRegex(record.body, r"\bstatic\s+(?!const\b)", (family, record.name))
        validate = self.original(0, "micros_scheduler_core_validate")
        self.assertRegex(validate, r"\bbool\s+seen\s*\[\s*MICROS_THREAD_CAPACITY\s*\]")
        self.assertRegex(validate, r"\bbool\s+current_seen\s*\[\s*MICROS_THREAD_CAPACITY\s*\]")
        batch = self.original(0, "micros_scheduler_commit_ipc_transitions")
        self.assertRegex(batch, r"\btransitions\s*\[\s*MICROS_THREAD_CAPACITY\s*\]")
        planner = self.original(0, "micros_hart_plan_user_return")
        self.assertRegex(planner, r"\bstruct\s+micros_scheduler_return_plan\s+candidate\s*;")
        for name in ("select_user_return", "micros_scheduler_start", "micros_scheduler_idle_select"):
            text = self.original(1, name)
            for type_name, variable in (
                ("micros_scheduler_return_plan", "plan"),
                ("scheduler_accounting_commit", "accounting"),
                ("scheduler_completion_commit", "completion"),
            ):
                self.assertRegex(text, rf"\bstruct\s+{type_name}\s+{variable}\s*;")
        accounting = self.original(4, "test_accounting_owners_are_separate")
        for name in ("plan", "rejected_plan"):
            self.assertRegex(accounting, rf"\bstruct\s+micros_scheduler_return_plan\s+{name}\s*;")
        model = self.original(5, "micros_scheduler_model_test_run")
        self.assertRegex(model, r"\bstruct\s+model_state\s+model\s*;")
        self.assertRegex(model, r"\buint32_t\s+random_state\s*=\s*seed\s*;")
        for family, name in (
            (2, "micros_scheduler_runtime_run_self_test"),
            (3, "micros_scheduler_invalid_runtime_run_self_test"),
        ):
            root = self.original(family, name)
            self.assertRegex(root, r"\buintptr_t\s+saved_status\s*;")
            require_order(root, "saved_status = riscv_irq_save();", "(void)saved_status;", "micros_scheduler_test_enter();", "failure:", "riscv_irq_restore(saved_status);")
            if family == 2:
                require_order(root, "micros_scheduler_test_enter();", "finish_test();")
            for record in self.records[family].values():
                if record.name != name:
                    self.assertNotIn("micros_scheduler_test_enter", {called for called, _, _, _ in call_sites(record.body)})

    def test_functions_stay_bounded_and_discharge_real_named_work(self) -> None:
        for family, records in enumerate(self.records):
            original = ORIGINAL_SOURCES[family]
            frozen = frozen_functions(original)
            for record in records.values():
                name = original_name(record.name, family)
                if record.lines > FUNCTION_REVIEW_LINE_LIMIT:
                    self.assertIn((original, name), FUNCTION_MIGRATION, record.name)
                    self.assertGreater(frozen[name][0], FUNCTION_REVIEW_LINE_LIMIT)
            for path, name in (*INITIAL_FUNCTION_MIGRATION, *SHORT_PHASE_ROOTS):
                if path != original or (path, name) in FUNCTION_MIGRATION:
                    continue
                if (path, name) in SHORT_PHASE_ROOTS and any(entry[0] == original for entry in SYMBOL_OWNER_MIGRATION):
                    continue
                root = find_original(records, family, name)
                phases = phase_records(root, family, records)
                new = [phase for phase in phases[1:] if len(semantic_facts(phase.body)) > 2]
                self.assertGreaterEqual(len({phase.name for phase in new}), 2, name)
                names = " ".join(phase_call_names(root, family, records))
                offset = 0
                for role in PHASE_ROLES[name]:
                    match = re.search(r"\b\w*(?:" + role + r")\w*\b", names[offset:])
                    self.assertIsNotNone(match, (name, "missing named domain phase", role))
                    if match:
                        offset += match.start()
                for phase in phases:
                    for called, _, _, _ in call_sites(phase.body):
                        if called not in records or original_name(called, family) in frozen:
                            continue
                        callee = records[called]
                        return_type = signature_shape(callee.signature, callee.name)[0]
                        if re.search(r"\bbool\b|\benum\b", return_type):
                            success = (
                                "true" if re.search(r"\bbool\b", return_type)
                                else "MICROS_KERNEL_OBJECT_OK" if "micros_kernel_object_error" in return_type
                                else "MICROS_SCHEDULER_TEST_CONTINUE" if "micros_scheduler_test_trap_action" in return_type
                                else "MICROS_SCHEDULER_OK"
                            )
                            require_error_propagation(normalized_body(phase, family, records), called, success)

    def test_all_phase_source_facts_are_preserved_not_summarized(self) -> None:
        for (original, name), expected in FROZEN_FACTS.items():
            family = ORIGINAL_SOURCES.index(original)
            root = find_original(self.records[family], family, name)
            actual = Counter()
            for phase in phase_records(root, family, self.records[family]):
                actual.update(fact_keys(fact_text(
                    phase, family, self.records[family],
                    automatic_model=name == "micros_scheduler_model_test_run",
                )))
            if name == "micros_scheduler_model_test_run":
                actual.update(model_operation_continuation_facts(
                    root, self.records[5]["scheduler_model_apply_operation"],
                    self.records[5],
                ))
            missing = parsed_fact_inventory(expected) - actual
            self.assertEqual({}, dict(missing), (name, "lost predicates/calls/arguments/writes/return values/control exits"))

    def test_next_context_fault_keeps_its_exact_compile_time_profile(self) -> None:
        root = find_original(
            self.records[3], 3, "micros_scheduler_invalid_runtime_run_self_test",
        )
        require_guarded_next_corruption(root, self.records[3])

    def test_first_start_hook_keeps_its_scheduler_test_profile(self) -> None:
        root = find_original(self.records[1], 1, "micros_scheduler_start")
        calls = [
            start for name, _, start, _ in call_sites(root.body)
            if name == "micros_scheduler_test_after_start"
        ]
        self.assertEqual(1, len(calls))
        self.assertEqual(
            ("ifdef MICROS_BUILD_SCHEDULER_TEST",),
            cpp_guards(root.body, calls[0]),
        )

    def test_minimal_accounting_and_nonrecursive_validation(self) -> None:
        for name, idle in (
            ("micros_scheduler_account_user_trap", False),
            ("micros_scheduler_account_idle_trap", True),
        ):
            require_minimal_accounting(self.original(0, name), idle)
        validate = self.phases(0, "micros_scheduler_core_validate")
        require_order(validate, "micros_kernel_objects_validate_base")
        self.assertNotIn("micros_kernel_objects_validate", {
            name for name, _, _, _ in call_sites(validate)
        })
        require_all_record_preflight(self.phases(0, "micros_scheduler_commit_ipc_transitions"))
        planner = self.phases(0, "micros_hart_plan_user_return")
        require_order(
            planner, "unsigned char *bytes = (unsigned char *)&candidate;",
            "index < sizeof(candidate)", "bytes[index] = 0;",
            "micros_scheduler_core_validate", "scratch_load",
            "MICROS_THREAD_RTS_PREEMPTED", "++renew_count > MICROS_THREAD_CAPACITY",
            "candidate.ready_head", "unsigned char *destination = (unsigned char *)plan;",
            "const unsigned char *source = (const unsigned char *)&candidate;",
            "destination[index] = source[index];", "return MICROS_KERNEL_OBJECT_OK;",
        )
        require_order(
            self.original(0, "micros_hart_commit_user_return"),
            "micros_hart_plan_user_return", "plans_equal", "micros_scheduler_apply_return_plan",
        )
        self.assertNotRegex(self.original(0, "micros_scheduler_apply_return_plan"), r"\breturn\s+\w")
        self.assertNotIn("micros_scheduler_core_validate", {
            name for name, _, _, _ in call_sites(self.original(0, "micros_scheduler_apply_return_plan"))
        })

    def test_selected_completion_modes_error_propagation_and_install_order(self) -> None:
        selector = self.phases(1, "select_user_return")
        require_selector_modes(
            self.original(1, "micros_scheduler_select_user_return"),
            self.original(1, "micros_scheduler_select_captured_user_return"),
            selector,
        )
        require_selected_sequence(selector)
        require_same_image(self.original(1, "commit_selected_completion"))
        preflight = self.original(1, "preflight_selected_completion")
        require_order(
            preflight, "if (!resolved->ipc_delivery_pending)", "micros_thread_ipc_state_is_clear",
            "if (completion->receive_buffer == 0)", "return SCHEDULER_COMPLETION_OK;",
            "micros_ipc_buffer_prepare_write", "MICROS_IPC_BUFFER_ERROR_MESSAGE_FAULT",
        )
        require_order(
            self.original(1, "enter_selected_thread"),
            "activate_prevalidated_root(hart, selected_root, frame)",
            "micros_user_execution_install_return_frame(frame, &selected_context, hart)",
            "hart->kernel_counter_ticks = accounting->kernel_total;",
            "hart->accounting_started_at = accounting->counter;",
            "hart->accounting_owner = MICROS_SCHEDULER_ACCOUNTING_THREAD;",
            "hart->accounted_thread = selected;",
        )
        require_order(
            self.phases(1, "micros_scheduler_start"),
            "preflight_kernel_interval", "riscv_irq_restore(saved_status);",
            "return scheduler_error;", "micros_timer_prepare_return", "micros_timer_start",
            "preflight_kernel_interval", '"scheduler-start-accounting"',
            "commit_completion_or_panic", "micros_scheduler_apply_return_plan",
            "enter_selected_thread", "micros_scheduler_test_after_start", "micros_riscv_enter_user",
        )
        idle = self.phases(1, "micros_scheduler_idle_select")
        require_selected_sequence(idle)
        require_order(
            idle, "MICROS_SCHEDULER_RETURN_ENTER_IDLE", "micros_timer_prepare_return",
            "preflight_kernel_interval", "micros_scheduler_apply_return_plan",
            "MICROS_SCHEDULER_ACCOUNTING_IDLE", "return;", "validate_selected_thread",
        )
        supervisor = self.original(1, "micros_scheduler_handle_supervisor_timer")
        self.assertNotRegex(supervisor, r"\b(?:select_user_return|enter_selected_thread|micros_scheduler_select_\w+)\s*\(")
        require_error_propagation(supervisor, "micros_scheduler_enter_supervisor_interrupt", "MICROS_SCHEDULER_OK")

    def test_private_call_direction_and_one_declaration_per_cross_file_helper(self) -> None:
        allowed = (
            ((), (0,), (0,), (0, 1, 2), (0, 1, 2), (0, 1, 2), (0, 2), (0, 6), (0, 2)),
            ((1, 2), (), (1,), (1, 2), (1,), (1, 2), (1,)),
            ((1, 2, 6), (), (1, 3), (1,), (1, 3), (1, 3, 4), (1, 4)),
            ((1, 2), (), (1,), (1,)),
            ((1, 2, 3, 4), (), (1,), (1,), (1,)),
            ((1, 2, 3, 4, 5), (), (1,), (1, 2), (1, 2), (1, 2)),
        )
        for family, paths in enumerate(SOURCE_SETS.values()):
            owners = {
                name: path for path in paths
                for name in function_records(self.texts.get(path, ""))
            }
            edges = {}
            header = self.texts.get(PRIVATE_HEADERS[family], "")
            for name, record in self.records[family].items():
                source = paths.index(owners[name])
                callees = {called for called, _, _, _ in call_sites(record.body) if called in owners}
                edges[name] = callees
                for called in callees:
                    target = paths.index(owners[called])
                    if source != target:
                        self.assertIn(target, allowed[family][source], (owners[name], name, owners[called], called))
                        callee = self.records[family][called]
                        self.assertNotRegex(callee.signature, r"\bstatic\b")
                        if not called.startswith("micros_"):
                            self.assertTrue(called.startswith(FAMILY_PREFIXES[family]), called)
                            self.assertEqual(1, len(re.findall(rf"\b{called}\s*\([^;{{}}]*\)\s*;", c_lexical_text(header))))
                if family == 5 and owners[name].endswith("_projection.c"):
                    for called in callees:
                        if owners[called].endswith("_reference.c"):
                            self.assertIn(original_name(called, family), {"handle_is_null", "handles_equal", "queue_find"})
            def visit(name: str, stack: tuple[str, ...]) -> None:
                self.assertNotIn(name, stack, ("recursive scheduler call graph", stack, name))
                for called in edges[name]:
                    visit(called, (*stack, name))
            for name in edges:
                visit(name, ())

    def test_case_model_prelude_coverage_and_rejection_projection_contract(self) -> None:
        main = self.original(4, "main")
        self.assertEqual(CASE_LABELS, tuple(re.findall(r'\{\s*"([^"]+)"\s*,', main)))
        require_order(
            main, "if (!tests[index].run())", 'fprintf(stderr, "not ok %zu - %s\\n"',
            "return 1;", 'printf("ok %zu - %s\\n"', "return 0;",
        )
        driver = self.original(5, "micros_scheduler_model_test_run")
        require_order(driver, "const uint32_t seed = UINT32_C(0x5c4ed026);", "uint32_t random_state = seed;")
        require_loop_projection(driver)
        new_calls = {
            called for called, _, _, _ in call_sites(driver)
            if called in self.records[5] and original_name(called, 5) not in frozen_functions(ORIGINAL_SOURCES[5])
        }
        for called in new_calls:
            require_loop_projection(driver, called)
        all_model = "\n".join(
            self.texts.get(path, "") for path in (*tuple(SOURCE_SETS.values())[5], PRIVATE_HEADERS[5])
        )
        for constant, value in (("MODEL_THREAD_COUNT", "8"), ("MODEL_HART_COUNT", "2"), ("MODEL_STEPS", "4096")):
            self.assertRegex(all_model, rf"\b{constant}\s*=\s*{value}\b")
        coverage = struct_body(all_model, "model_coverage")
        self.assertEqual(COVERAGE_FIELDS, tuple(re.findall(r"\bsize_t\s+(\w+)\s*;", coverage)))
        graph = self.phases(5, "micros_scheduler_model_test_run")
        self.assertEqual(COVERAGE_FIELDS, tuple(re.findall(r"\bcoverage\s*\.\s*(\w+)\s*==\s*0", graph)))
        self.assertIn("scheduler model coverage incomplete\\n", graph)
        self.assertEqual(
            compact("*state = *state * UINT32_C(1664525) + UINT32_C(1013904223); return *state;"),
            compact(self.original(5, "next_random")),
        )
        prelude = self.phases(5, "run_required_prelude")
        self.assertNotIn("next_random", {name for name, _, _, _ in call_sites(prelude)})
        require_order(
            prelude, "admit(model, 0, 0, 10, 60)", "admit(model, 1, 0, 10, 60)",
            "admit(model, 2, 0, 10, 60)", "plan_and_commit(model, 0)",
            "counter += 10", "micros_scheduler_account_enter_thread", "counter += 15",
            "micros_scheduler_account_user_trap", "admit(model, 3, 0, 3, 60)",
            "admit(model, 4, 1, 8, 50)", "admit(model, 5, 1, 8, 50)",
            "admit(model, 6, 1, 12, 50)", "admit(model, 7, 1, 1, 50)",
            "micros_thread_scheduler_hold", "micros_thread_runtime_flags_set",
            "micros_thread_install_policy", "micros_thread_runtime_flags_unset",
            "micros_thread_install_policy", "counter += 5", "counter += 100",
            "size_t slots[] = {4, 5, 6, 7};", "micros_thread_scheduler_hold",
            "plan_and_commit(model, 1)", "micros_scheduler_account_enter_idle",
            "micros_scheduler_account_idle_trap", "micros_thread_runtime_flags_unset",
        )
        for name in ("admit", "plan_and_commit"):
            self.assertNotIn("model_matches", {called for called, _, _, _ in call_sites(self.original(5, name))})
        invalid = self.original(3, "micros_scheduler_invalid_test_after_user_timer")
        require_order(
            invalid, "copy_bytes(&scheduler_snapshot_candidate, objects, sizeof(scheduler_snapshot_candidate))",
            "scheduler_snapshot_candidate.harts[hart_handle.slot].timer = timer_objects_snapshot.harts[hart_handle.slot].timer;",
            "bytes_equal(&scheduler_snapshot_candidate, &timer_objects_snapshot, sizeof(scheduler_snapshot_candidate))",
            "bytes_equal(frame, &timer_frame_snapshot, sizeof(*frame))",
            "read_satp() == timer_satp_snapshot",
        )
        accounting = self.phases(4, "test_accounting_owners_are_separate")
        require_order(
            accounting, "setup_fixture()", "harts[0], 100", "threads[0], 120",
            "rejected_plan = plan;", "memcmp(&rejected_plan, &plan, sizeof(plan)) == 0",
            "harts[0], 170", "threads[0], 200", "harts[0], 300",
            "harts[0], 330", "harts[0], 380", "setup_fixture()",
            "UINT64_MAX - UINT64_C(29)", "UINT64_MAX - UINT64_C(9)", "harts[0], 10",
        )

    def test_macro_and_constant_evidence_is_not_replaced_with_aliases(self) -> None:
        for family, paths in enumerate(SOURCE_SETS.values()):
            all_text = "\n".join(self.texts.get(path, "") for path in (*paths, PRIVATE_HEADERS[family]))
            macros = macro_records(all_text)
            self.assertEqual(len(macros), len({name for name, _ in macros}))
            if family == 4:
                macros = tuple(
                    (
                        name,
                        normalized_body(Function(
                            "assertion_macro",
                            "void assertion_macro(struct scheduler_host_fixture *fixture)",
                            text, 1, (),
                        ), family, self.records[family]),
                    )
                    for name, text in macros
                )
            macro_text = "\n".join(text for _, text in sorted(macros))
            self.assertEqual(FROZEN_MACROS[family], digest(macro_text), paths[0])
            if family in FROZEN_CONSTANTS:
                constants = [
                    compact(strip_static(declaration))
                    for path in paths
                    for declaration in object_definitions(self.texts.get(path, ""))
                    if re.search(r"\bconst\b", declaration)
                ]
                self.assertEqual(FROZEN_CONSTANTS[family], digest("\n".join(sorted(constants))))


class SchedulerSourceSafetyFixtureTest(unittest.TestCase):
    """Independent expected examples; never read changing scheduler C as fixtures."""

    def test_function_records_reuses_lexing_for_repeated_body_checks(self) -> None:
        source = "void leaf(void) {}\nvoid root(void) {" + "leaf();" * 200 + "}\n"
        function_records.cache_clear()
        with patch("tests.host.c_source_records.c_lexical_text", wraps=c_lexical_text) as lexical:
            first = function_records(source)
            second = function_records(source)
        self.assertEqual({"leaf", "root"}, set(first))
        self.assertIs(first, second)
        self.assertEqual(1, lexical.call_count)

    def test_supported_signatures_compare_types_not_parameter_spelling(self) -> None:
        declaration = (
            "enum result check(const struct objects *objects, "
            "struct hart_handle hart, uint64_t counter)"
        )
        definition = declaration.replace("*objects,", "*registry,").replace(
            "hart,", "hart_handle,",
        )
        expected = signature_shape(declaration, "check")
        self.assertEqual(expected, signature_shape(definition, "check"))
        for changed in (
            definition.replace("enum result", "bool"),
            definition.replace("const struct objects", "struct objects"),
            definition.replace("struct hart_handle", "struct thread_handle"),
            definition.replace("uint64_t", "uint32_t"),
        ):
            self.assertNotEqual(expected, signature_shape(changed, "check"))
        self.assertEqual(
            "static void",
            signature_shape("static void phase(bool *seen, enum owner mode)", "phase")[0],
        )

    def test_extern_declarations_do_not_create_storage(self) -> None:
        source = (
            "extern const uint64_t code_address;\n"
            "extern volatile uint64_t idle_probe;\n"
            "extern uint64_t defined = 0;\n"
            "static uint64_t state;\n"
        )
        self.assertEqual(
            {"extern uint64_t defined = 0;", "static uint64_t state;"},
            {item.strip() for item in object_definitions(source)},
        )

    def test_selector_mode_flip_and_partial_null_are_rejected(self) -> None:
        normal = "return select_user_return(hart, frame, true);"
        captured = "return select_user_return(hart, frame, false);"
        selector = """
if (store_outgoing) {
    micros_user_execution_validate_context(current, &outgoing);
    micros_user_execution_store_context(current, &outgoing);
} else if (hart->current_thread.slot != 0 || hart->current_thread.generation != 0) {
    return MICROS_SCHEDULER_ERROR_STATE;
}
"""
        require_selector_modes(normal, captured, selector)
        for left, right, body in (
            (normal.replace("true", "false"), captured, selector),
            (normal, captured.replace("false", "true"), selector),
            (normal, captured, selector.replace("hart->current_thread.slot != 0 ||", "")),
            (normal, captured, selector.replace("return MICROS_SCHEDULER_ERROR_STATE;", "return MICROS_SCHEDULER_OK;")),
        ):
            with self.assertRaises(AssertionError):
                require_selector_modes(left, right, body)

    def test_timer_preflight_commit_order_rejects_reordering(self) -> None:
        positive = """
validate_selected_thread(objects, plan.selected);
preflight_completion_or_panic(objects, plan.selected, &completion, hart, frame);
load_selected_context(plan.selected);
micros_timer_prepare_return(hart);
preflight_kernel_interval(hart, &accounting);
commit_completion_or_panic(&completion, hart, frame);
micros_scheduler_apply_return_plan(objects, &plan);
enter_selected_thread(hart, plan.selected, &accounting, frame);
"""
        require_selected_sequence(positive)
        for first, second in (
            ("micros_timer_prepare_return(hart);", "commit_completion_or_panic(&completion, hart, frame);"),
            ("load_selected_context(plan.selected);", "micros_timer_prepare_return(hart);"),
            ("preflight_kernel_interval(hart, &accounting);", "micros_scheduler_apply_return_plan(objects, &plan);"),
        ):
            bad = positive.replace(first, "SWAP").replace(second, first).replace("SWAP", second)
            with self.assertRaises(AssertionError):
                require_selected_sequence(bad)

    def test_completion_keeps_prepared_copy_and_same_patched_image(self) -> None:
        positive = """
micros_ipc_buffer_commit_write(&completion->buffer_plan, &completion->message);
selected_context.a0 = completion->abi_result;
copy_user_context(&completion->thread->user_context, &selected_context);
completion->thread->ipc_receive_buffer = 0;
completion->thread->ipc_delivery_pending = false;
clear_bytes(&completion->thread->ipc_inbound_message, sizeof(completion->thread->ipc_inbound_message));
completion->thread->ipc_staged_result = MICROS_IPC_OK;
"""
        require_same_image(positive)
        for bad in (
            positive.replace("&completion->buffer_plan", "&recomputed"),
            positive.replace("&selected_context);", "&unpatched_context);"),
            positive.replace("selected_context.a0 = completion->abi_result;", ""),
            positive + "micros_ipc_buffer_prepare_write(owner, address, &plan);",
        ):
            with self.assertRaises(AssertionError):
                require_same_image(bad)

    def test_batch_must_preflight_all_records_before_any_store(self) -> None:
        positive = """
for (index = 0; index < request_count; ++index) {
    for (other_index = 0; other_index < index; ++other_index) { shape(); }
}
micros_scheduler_core_validate(objects);
for (index = 0; index < request_count; ++index) {
    resolve_thread_mutable(objects, transitions[index].handle, &transitions[index].thread);
    preflight_tail_enqueue(objects, transitions[index].hart, transitions[index].handle, priority);
}
for (index = 0; index < request_count; ++index) {
    transitions[index].thread->runtime_flags = transitions[index].resulting_flags;
    queue_enqueue_tail_prevalidated(objects, transitions[index].hart, transitions[index].handle);
}
"""
        require_all_record_preflight(positive)
        for bad in (
            positive.replace("index < request_count", "index < 1", 2),
            positive.replace("preflight_tail_enqueue(objects, transitions[index].hart, transitions[index].handle, priority);", ""),
            positive.replace(
                "resolve_thread_mutable",
                "transitions[index].thread->runtime_flags = transitions[index].resulting_flags;\nresolve_thread_mutable",
            ),
        ):
            with self.assertRaises(AssertionError):
                require_all_record_preflight(bad)

    def test_result_propagation_and_local_return_caller_exit_scope(self) -> None:
        for positive in (
            "return checked_phase(objects);",
            "error = checked_phase(objects); if (error != MICROS_KERNEL_OBJECT_OK) { return error; }",
            "error = checked_phase(objects); if (error != MICROS_KERNEL_OBJECT_OK) { return map_object_error(error); }",
            "if (!checked_phase(objects)) { return false; }",
            "if (!checked_phase(objects)) { goto failure; }",
        ):
            with self.subTest(source=positive):
                require_error_propagation(positive, "checked_phase", "MICROS_KERNEL_OBJECT_OK")
        for bad in (
            "checked_phase(objects); return true;",
            "error = checked_phase(objects); return MICROS_KERNEL_OBJECT_OK;",
            "error = checked_phase(objects); if (error == MICROS_KERNEL_OBJECT_OK) { return error; }",
            "if (!checked_phase(objects)) { return true; }",
            "if (!checked_phase(objects)) { error = error; } return true;",
        ):
            with self.assertRaises(AssertionError):
                require_error_propagation(bad, "checked_phase", "MICROS_KERNEL_OBJECT_OK")
        driver = """
for (step = 1; step <= MODEL_STEPS; ++step) {
    value = next_random(&random_state);
    if (!step_action(value)) { return false; }
    if (!model_matches(&model, step, operation, value)) { return false; }
}
return true;
"""
        require_loop_projection(driver, "step_action")
        for bad in (
            driver.replace("if (!step_action(value)) { return false; }", "return step_action(value);"),
            driver.replace("if (!step_action(value)) { return false; }", "if (!step_action(value)) { continue; }"),
            driver.replace("next_random(&random_state)", "next_random(&random_state) + next_random(&random_state)"),
        ):
            with self.assertRaises(AssertionError):
                require_loop_projection(bad, "step_action")
        require_loop_projection(
            driver.replace("return true;", "return verify_coverage(&model);"),
            "verify_coverage",
        )

    def test_facts_keep_predicates_arguments_writes_results_and_exits(self) -> None:
        source = "if (flags == 0) { result = update(objects, value); return result; }"
        expected = fact_keys(source)
        for changed in (
            source.replace("==", "!="), source.replace("objects, value", "objects, 0"),
            source.replace("result =", "other ="), source.replace("return result", "return 0"),
            source.replace("return result", "goto done"),
        ):
            self.assertTrue(expected - fact_keys(changed))
        self.assertEqual(expected, fact_keys("/* formatting only */\n" + source))

    def test_cpp_guard_is_separate_from_its_C_write_fact(self) -> None:
        write = "objects->threads[threads[1].slot].user_context.sp = 1;"
        guarded = (
            "#ifdef MICROS_BUILD_SCHEDULER_INVALID_NEXT_TEST\n"
            + write + "\n#endif\n"
        )
        self.assertEqual(fact_keys(write), fact_keys(guarded))
        self.assertEqual((), call_sites("#endif\n(void)saved_status;"))

    def test_switch_case_label_is_separate_from_its_C_write_fact(self) -> None:
        write = 'operation = "hold";'
        self.assertEqual(fact_keys(write), fact_keys("case 0:\n" + write))
        self.assertNotEqual(
            fact_keys(write), fact_keys('case 0:\noperation = "wake";'),
        )

    def test_next_context_fault_and_helper_call_keep_the_profile_guard(self) -> None:
        write = "objects->threads[threads[1].slot].user_context.sp = 1;"
        guard = ("ifdef MICROS_BUILD_SCHEDULER_INVALID_NEXT_TEST",)
        body = "#ifdef MICROS_BUILD_SCHEDULER_INVALID_NEXT_TEST\n"
        direct = Function("entry", "void entry(void)", body + write + "\n#endif", 1, ())
        require_guarded_next_corruption(direct, {"entry": direct})
        entry = Function("entry", "void entry(void)", body + "inject(objects);\n#endif", 1, ())
        helper = Function("inject", "void inject(void)", write, 1, guard)
        require_guarded_next_corruption(entry, {"entry": entry, "inject": helper})
        for changed_entry, changed_helper in (
            (Function("entry", entry.signature, "inject(objects);", 1, ()), helper),
            (entry, Function("inject", helper.signature, write, 1, ())),
            (entry, Function("inject", helper.signature, write + write, 1, guard)),
        ):
            with self.assertRaises(AssertionError):
                require_guarded_next_corruption(
                    changed_entry, {"entry": changed_entry, "inject": changed_helper},
                )

    def test_typed_context_transport_never_normalizes_arbitrary_aliases(self) -> None:
        signature = "bool example(struct scheduler_host_fixture *fixture)"
        typed = Function("example", signature, "return fixture->objects.initialized;", 1, ())
        untyped = Function("example", "bool example(void *fixture)", typed.body, 1, ())
        self.assertEqual(compact("return objects.initialized;"), compact(normalized_body(typed, 4)))
        self.assertNotEqual(compact(normalized_body(typed, 4)), compact(normalized_body(untyped, 4)))
        self.assertEqual("scheduler_host_unrecognized", original_name("scheduler_host_unrecognized", 4))
        self.assertEqual("handles_equal", original_name("scheduler_host_handles_equal", 4))
        for body in (
            "return other->fixture->objects.initialized;",
            "return other.fixture->objects.initialized;",
        ):
            with self.subTest(body=body), self.assertRaises(AssertionError):
                normalized_body(Function("example", signature, body, 1, ()), 4)

    def test_automatic_candidate_reference_preserves_byte_extent(self) -> None:
        signature = (
            "void publish(struct micros_scheduler_return_plan *plan, "
            "const struct micros_scheduler_return_plan *candidate)"
        )
        body = """
unsigned char *destination = (unsigned char *)plan;
const unsigned char *source = (const unsigned char *)candidate;
for (index = 0; index < sizeof(*candidate); ++index) {
    destination[index] = source[index];
}
"""
        expected = body.replace(
            "(const unsigned char *)candidate", "(const unsigned char *)&candidate",
        ).replace("sizeof(*candidate)", "sizeof(candidate)")
        positive = Function("publish", signature, body, 1, ())
        self.assertEqual(compact(expected), compact(fact_text(positive, 0, {})))
        for changed in (
            Function("publish", signature, body.replace("sizeof(*candidate)", "sizeof(candidate)"), 1, ()),
            Function("publish", signature.replace("const struct micros_scheduler_return_plan *candidate", "const void *candidate"), body, 1, ()),
        ):
            self.assertNotEqual(compact(expected), compact(fact_text(changed, 0, {})))

    def test_accounting_reference_transport_requires_its_exact_record_type(self) -> None:
        body = "return preflight_kernel_interval(hart, accounting);"
        expected = "return preflight_kernel_interval(hart, &accounting);"
        signature = (
            "enum micros_scheduler_error prepare("
            "struct scheduler_accounting_commit *accounting)"
        )
        typed = Function("prepare", signature, body, 1, ())
        untyped = Function("prepare", signature.replace(
            "struct scheduler_accounting_commit", "void",
        ), body, 1, ())
        self.assertEqual(compact(expected), compact(fact_text(typed, 1, {})))
        self.assertNotEqual(compact(expected), compact(fact_text(untyped, 1, {})))

    def test_original_snapshot_macro_keeps_its_full_object_copy(self) -> None:
        signature = "void assertion_macro(struct scheduler_host_fixture *fixture)"
        body = """
#define EXPECT_UNCHANGED(expected, expression) \\
do { \\
    struct micros_kernel_objects before = fixture->objects; \\
    EXPECT_ERROR((expected), (expression)); \\
    EXPECT_TRUE(memcmp(&fixture->objects, &before, sizeof(fixture->objects)) == 0); \\
} while (false)
"""
        original = body.replace("fixture->objects", "objects")
        typed = Function("assertion_macro", signature, body, 1, ())
        self.assertEqual(digest(original), digest(normalized_body(typed, 4)))
        shortened = body.replace("sizeof(fixture->objects)", "sizeof(uint64_t)")
        self.assertNotEqual(
            digest(original),
            digest(normalized_body(Function(
                "assertion_macro", signature, shortened, 1, (),
            ), 4)),
        )

    def test_model_reference_transport_distinguishes_root_value_from_pointer(self) -> None:
        signature = "bool phase(struct model_state *model)"
        body = "queue_remove(model, hart, priority, slot); model->coverage.flag_first++;"
        record = Function("phase", signature, body, 1, ())
        expected = "queue_remove(&model, hart, priority, slot); model.coverage.flag_first++;"
        self.assertEqual(
            compact(expected),
            compact(fact_text(record, 5, {}, automatic_model=True)),
        )
        self.assertEqual(
            compact(body.replace("model->", "model.")),
            compact(fact_text(record, 5, {})),
        )
        untyped = Function("phase", "bool phase(void *model)", body, 1, ())
        self.assertNotEqual(
            compact(expected),
            compact(fact_text(untyped, 5, {}, automatic_model=True)),
        )

    def test_local_operation_skip_still_reaches_dispatch_and_common_projection(self) -> None:
        driver = Function("driver", "bool driver(void)", """
for (step = 1; step <= MODEL_STEPS; ++step) {
    value = next_random(&random_state);
    if (!dispatch(fixture, &model, thread, step, value, slot, hart, &operation)) {
        return false;
    }
    if (!model_matches(&model, step, operation, value)) { return false; }
}
return true;
""", 1, ())
        dispatch = Function("dispatch", "bool dispatch(void)", """
switch (value % 10) {
case 0:
    if (!apply_hold(fixture, model, thread, step, value, slot, hart, operation_out)) {
        return false;
    }
    break;
}
return true;
""", 1, ())
        handler = Function("apply_hold", "bool apply_hold(struct model_state *model)", """
const char *operation;
operation = "hold";
*operation_out = operation;
if (thread->assigned && model->harts[hart].accounting_owner == MICROS_SCHEDULER_ACCOUNTING_THREAD) {
    return true;
}
++model->coverage.flag_first;
return true;
""", 1, ())
        contract = ((0, "apply_hold", "hold", "thread"),)
        with patch(__name__ + ".MODEL_OPERATION_HANDLERS", contract):
            self.assertEqual(
                fact_keys("break;"),
                model_operation_continuation_facts(
                    driver, dispatch, {"apply_hold": handler},
                ),
            )
            for bad_driver, bad_dispatch, bad_handler in (
                (Function(driver.name, driver.signature, driver.body.replace("return false;", "return true;", 1), 1, ()), dispatch, handler),
                (driver, Function(dispatch.name, dispatch.signature, dispatch.body.replace("    break;", ""), 1, ()), handler),
                (driver, Function(dispatch.name, dispatch.signature, dispatch.body.replace("!apply_hold", "apply_hold"), 1, ()), handler),
                (driver, dispatch, Function(handler.name, handler.signature, handler.body.replace("== MICROS_SCHEDULER_ACCOUNTING_THREAD", "!= MICROS_SCHEDULER_ACCOUNTING_THREAD"), 1, ())),
                (driver, dispatch, Function(handler.name, handler.signature, handler.body + "\nreturn true;", 1, ())),
            ):
                with self.assertRaises(AssertionError):
                    model_operation_continuation_facts(
                        bad_driver, bad_dispatch, {"apply_hold": bad_handler},
                    )


if __name__ == "__main__":
    unittest.main()

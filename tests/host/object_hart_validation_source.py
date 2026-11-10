"""Closed native OBJ01 source and passive primitive observations."""

import ast
import hashlib
from functools import lru_cache
from pathlib import Path
import re
import subprocess

from tests.host.c_source_records import compact, function_records
from tests.host.evidence_authority_source import literal_value, regular_source, single_assignment
from tests.host.evidence_authority_types import AuthorityClass, Fact, digest
from tests.host.selected_release_handoff_source import _body_sites, _render_body
from tests.host.work_count_lexical import compound_layout, function_spans, positional_tokens


CHANNELS = ("aggregate", "stack", "ordinary")
FAMILY = "object-hart-validation-baseline"
NATIVE_FAMILY = "object-hart-native-admission"
SOURCE_PATH = "tests/host/object_hart_validation_source.py"
AUTHORITY_PATH = "tests/host/object_hart_validation_authority.py"
BINDINGS_PATH = "tests/host/object_hart_native_source_bindings.py"
SOURCE_ROLES = {
    "base_membership_sha256": "3d66ad157e198d998eaea7a85ff526ef56cbd039121fb36316e424b71cba1a66",
    "production_paths": (
        "kernel/objects/kernel_objects.c",
        "kernel/objects/kernel_object_state.c",
        "kernel/objects/kernel_object_identity.c",
        "kernel/objects/kernel_object_stack.c",
        "kernel/objects/kernel_object_process.c",
        "kernel/objects/kernel_object_thread.c",
        "kernel/objects/kernel_object_context.c",
        "kernel/objects/kernel_object_hart.c",
        "kernel/objects/kernel_object_validation.c",
        "kernel/objects/kernel_object_private.h",
        "kernel/scheduler/scheduler_core.c",
        "kernel/scheduler/scheduler_core_queue.c",
        "kernel/scheduler/scheduler_core_validate.c",
        "kernel/scheduler/scheduler_core_policy.c",
        "kernel/scheduler/scheduler_core_ipc.c",
        "kernel/scheduler/scheduler_core_ipc_transition.c",
        "kernel/scheduler/scheduler_core_return_plan.c",
        "kernel/scheduler/scheduler_core_return.c",
        "kernel/scheduler/scheduler_core_accounting.c",
        "kernel/scheduler/scheduler_core_private.h",
    ),
    "fixed_adapters": (
        "tests/host/kernel_layout_source.py",
        "tests/host/kernel_mapping_source.py",
        "tests/host/selected_address_space_source.py",
        "tests/host/selected_ownership_source.py",
        "tests/host/selected_release_handoff_source.py",
        "tests/host/test_endpoint_source_layout.py",
        "tests/host/test_kernel_object_source_layout.py",
        "tests/host/test_scheduler_source_layout.py",
        "tools/generate_kernel_mapping_fixture.py",
        "tools/generate_selected_ownership_fixture.py",
        "tools/generate_selected_release_handoff_fixture.py",
        "tools/stack_bound_fatal.py",
    ),
    "unchanged_candidates": (
        "tests/host/memory_native_source_bindings.py",
        "tests/host/panic_trace_source_bindings.py",
        "tests/host/selected_address_space_native_source_bindings.py",
        "tests/host/selected_ownership_native_source_bindings.py",
        "tests/host/selected_ownership_observation_bindings.py",
        "tests/host/selected_release_handoff_native_source_bindings.py",
    ),
    "immutable_additions": (
        "tests/host/object_hart_validation_test.c",
        "tests/host/object_hart_validation_test.h",
        "tests/host/object_hart_validation_source.py",
        "tests/host/object_hart_validation_evidence.py",
        "tests/host/object_hart_validation_authority.py",
        "tests/host/object_hart_native_source_bindings.py",
        "tests/host/test_object_hart_validation.py",
        "tools/generate_object_hart_validation_fixture.py",
        "tools/check_object_hart_validation.py",
    ),
    "immutable_adapters": (
        "CMakeLists.txt",
        "tests/host/evidence_authority_inventory.py",
        "tests/host/evidence_authority_scope.py",
        "tests/host/test_evidence_authority.py",
        "tests/host/test_evidence_authority_scope.py",
        "tests/host/test_panic_trace.py",
        "tests/host/test_python_host_runtime.py",
        "tests/host/test_selected_release_handoff.py",
        "tests/host/test_stack_bound_evidence.py",
        "tests/host/test_stack_source_projection.py",
        "tools/stack_bound_fatal_effects.py",
    ),
}
SOURCE_PROJECTION = {
    "source_reference": "9b880468192e61177ffe57ec69079fe398160a42",
    "source_tree": "80b93b111b5a1387d812da87c539e56da042777b",
    "adapter_reference": "6d43a88f98c978ecdb4c1ec2ba31db3eca4d8c91",
    "predecessor_facts_sha256": "2406b1b080963f2e5adc920fc78ea29c1fd8743dd38f25218003bd994a8e2646",
    "affected_facts_sha256": "df04235e5286127c14a13ce42fdba7eda52bbe8835bb6e72090eccfe62b40c41",
    "native_bindings_sha256": "1acd7ee75347207df663ee10a592a5347456b8a3f045138d82fe26a4bdd3ee15",
    "current_function_contracts_sha256": "0d0dcbe85581fe95e8c4dcd96d9cee60f4d9faf29f1d916132795cdb4924fba2",
    "audit_boundaries_sha256": "910f0a4dec0d7a404bea117eef40c970e5876b5ddf0a82149509ae25257aab70",
    "adapter_sha256": {
        "tests/host/kernel_layout_source.py": "7ea99467cf5062ed7c711547d120a66caa0254c20f90a138a287c94d424af0e7",
        "tests/host/kernel_mapping_source.py": "3e986d8414b060e0d713567486db0625a4eea49c55669085cd9a857df1b50815",
        "tests/host/selected_address_space_source.py": "433a60a81bbe8901cef51a9e327f31fd7439e6ad454981afc1fc151291d18698",
        "tests/host/selected_ownership_source.py": "41b8b9bd7b4de6b857ab427a70aeb1fe23d9e52ead43ae1cfd8dbf53a7513883",
        "tests/host/selected_release_handoff_source.py": "0bf5c421c3dda48ab30ec949db0e3528f03960a64c1cd256abf64482ed53cecb",
        "tests/host/test_endpoint_source_layout.py": "dcccfce7be9b6e01375a669a45f614c4f5b62ddfd21d663a310d5feaeba7edc7",
        "tests/host/test_kernel_object_source_layout.py": "4a4cc5951169490c764d3fcf5f854b3093f184769f10090de6f4376e526700bd",
        "tests/host/test_scheduler_source_layout.py": "9cb3eddc7da68072afa6165836ee9111adb00c8b83169de194180694b515e424",
        "tools/generate_kernel_mapping_fixture.py": "bb068daae53db5e0a36ec7918fc7167070c0d3cfb68cf95277e297280c1e60de",
        "tools/generate_selected_ownership_fixture.py": "ba8f39db43703409092c46d143e9afa419e0c13cf714f8f847242e54503af91a",
        "tools/generate_selected_release_handoff_fixture.py": "ad56ca10f311e54d6d7f2b9b293ae8ab841eb3854e5cc0b954b851877883dcfe",
        "tools/stack_bound_fatal.py": "3360327a02b2bb8e323d4cfb6eb1bda5b1850283dcb365f90dc1c1d602313ba7",
        "tests/host/memory_native_source_bindings.py": "4a90c7ec808b4fa3edd4606b83b250b0e9ddba4cbde09de03df65dea6ec35202",
        "tests/host/panic_trace_source_bindings.py": "fde1633dd1739c3fba7ec9a62b0e53b095d410791c2c74f73f82c1da5386886e",
        "tests/host/selected_address_space_native_source_bindings.py": "5736cc190f48f94975546d209ccb4b36ca7549d09fc7e6b9880ebc2095933254",
        "tests/host/selected_ownership_native_source_bindings.py": "6b67d60370e9ee30c480a730c400589660e11d5919ce4e728b3265d449629903",
        "tests/host/selected_ownership_observation_bindings.py": "5aa42b332643580dc6bebc9fd350f2df08f2cce12fa8fe1a450bebd6b55e81b4",
        "tests/host/selected_release_handoff_native_source_bindings.py": "2827df6a3236463106b325292883ae5ac83a0922a2871a26220126436d9db93f",
    },
    "stack_admission_sha256": {
        "tools/stack_bound_fatal_effects.py": "15efa33028840b06ca23a3fddaea194a4d57b47f988cc2663f7de20993a8c4e9",
        "tests/host/test_stack_bound_evidence.py": "7fd3c40f2792fc7208b533bbd464eafbe572b2c6d813a5a4c28902e4125607b7",
    },
    "retirement_keys": (
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/kernel_object_context.c/2",
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/kernel_object_hart.c/2",
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/kernel_object_validation.c/2",
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/scheduler_core.c/2",
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/scheduler_core_accounting.c/2",
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/scheduler_core_ipc.c/2",
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/scheduler_core_ipc_transition.c/2",
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/scheduler_core_policy.c/2",
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/scheduler_core_private.h/2",
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/scheduler_core_queue.c/2",
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/scheduler_core_return_plan.c/2",
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/scheduler_core_validate.c/2",
        "tests/host/memory_native_source_bindings.py:native-source-admission:MEM02/native-input/kernel/objects/kernel_object_context.c",
        "tests/host/memory_native_source_bindings.py:native-source-admission:MEM02/native-input/kernel/objects/kernel_object_hart.c",
        "tests/host/memory_native_source_bindings.py:native-source-admission:MEM02/native-input/kernel/objects/kernel_object_validation.c",
        "tests/host/memory_native_source_bindings.py:native-source-admission:MEM02/native-input/kernel/scheduler/scheduler_core.c",
        "tests/host/memory_native_source_bindings.py:native-source-admission:MEM02/native-input/kernel/scheduler/scheduler_core_accounting.c",
        "tests/host/memory_native_source_bindings.py:native-source-admission:MEM02/native-input/kernel/scheduler/scheduler_core_ipc.c",
        "tests/host/memory_native_source_bindings.py:native-source-admission:MEM02/native-input/kernel/scheduler/scheduler_core_ipc_transition.c",
        "tests/host/memory_native_source_bindings.py:native-source-admission:MEM02/native-input/kernel/scheduler/scheduler_core_policy.c",
        "tests/host/memory_native_source_bindings.py:native-source-admission:MEM02/native-input/kernel/scheduler/scheduler_core_private.h",
        "tests/host/memory_native_source_bindings.py:native-source-admission:MEM02/native-input/kernel/scheduler/scheduler_core_queue.c",
        "tests/host/memory_native_source_bindings.py:native-source-admission:MEM02/native-input/kernel/scheduler/scheduler_core_return_plan.c",
        "tests/host/memory_native_source_bindings.py:native-source-admission:MEM02/native-input/kernel/scheduler/scheduler_core_validate.c",
        "tests/host/panic_trace_source_bindings.py:source-admission:PLAT03/projected-source/kernel/objects/kernel_object_hart.c",
        "tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/native-input/kernel/objects/kernel_object_context.c",
        "tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/native-input/kernel/objects/kernel_object_hart.c",
        "tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/native-input/kernel/objects/kernel_object_validation.c",
        "tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/native-input/kernel/scheduler/scheduler_core.c",
        "tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/native-input/kernel/scheduler/scheduler_core_accounting.c",
        "tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/native-input/kernel/scheduler/scheduler_core_ipc.c",
        "tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/native-input/kernel/scheduler/scheduler_core_ipc_transition.c",
        "tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/native-input/kernel/scheduler/scheduler_core_policy.c",
        "tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/native-input/kernel/scheduler/scheduler_core_private.h",
        "tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/native-input/kernel/scheduler/scheduler_core_queue.c",
        "tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/native-input/kernel/scheduler/scheduler_core_return_plan.c",
        "tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/native-input/kernel/scheduler/scheduler_core_validate.c",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/native-input/kernel/objects/kernel_object_context.c",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/native-input/kernel/objects/kernel_object_hart.c",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/native-input/kernel/objects/kernel_object_validation.c",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/native-input/kernel/scheduler/scheduler_core.c",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/native-input/kernel/scheduler/scheduler_core_accounting.c",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/native-input/kernel/scheduler/scheduler_core_ipc.c",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/native-input/kernel/scheduler/scheduler_core_ipc_transition.c",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/native-input/kernel/scheduler/scheduler_core_policy.c",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/native-input/kernel/scheduler/scheduler_core_private.h",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/native-input/kernel/scheduler/scheduler_core_queue.c",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/native-input/kernel/scheduler/scheduler_core_return_plan.c",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/native-input/kernel/scheduler/scheduler_core_validate.c",
        "tests/host/selected_ownership_observation_bindings.py:native-source-admission:MEM03/observation/kernel/objects/kernel_object_validation.c/kernel_object_validation_hart_stacks_avoid_peers",
        "tests/host/selected_ownership_observation_bindings.py:native-source-admission:MEM03/observation/kernel/objects/kernel_object_validation.c/kernel_object_validation_thread_context_matches",
        "tests/host/selected_ownership_observation_bindings.py:native-source-admission:MEM03/observation/kernel/scheduler/scheduler_core.c/scheduler_core_thread_is_current",
        "tests/host/selected_release_handoff_native_source_bindings.py:native-source-admission:MEM05/native-input/kernel/objects/kernel_object_context.c",
        "tests/host/selected_release_handoff_native_source_bindings.py:native-source-admission:MEM05/native-input/kernel/objects/kernel_object_hart.c",
        "tests/host/selected_release_handoff_native_source_bindings.py:native-source-admission:MEM05/native-input/kernel/objects/kernel_object_validation.c",
        "tests/host/selected_release_handoff_native_source_bindings.py:native-source-admission:MEM05/native-input/kernel/scheduler/scheduler_core.c",
        "tests/host/selected_release_handoff_native_source_bindings.py:native-source-admission:MEM05/native-input/kernel/scheduler/scheduler_core_accounting.c",
        "tests/host/selected_release_handoff_native_source_bindings.py:native-source-admission:MEM05/native-input/kernel/scheduler/scheduler_core_ipc.c",
        "tests/host/selected_release_handoff_native_source_bindings.py:native-source-admission:MEM05/native-input/kernel/scheduler/scheduler_core_ipc_transition.c",
        "tests/host/selected_release_handoff_native_source_bindings.py:native-source-admission:MEM05/native-input/kernel/scheduler/scheduler_core_policy.c",
        "tests/host/selected_release_handoff_native_source_bindings.py:native-source-admission:MEM05/native-input/kernel/scheduler/scheduler_core_private.h",
        "tests/host/selected_release_handoff_native_source_bindings.py:native-source-admission:MEM05/native-input/kernel/scheduler/scheduler_core_queue.c",
        "tests/host/selected_release_handoff_native_source_bindings.py:native-source-admission:MEM05/native-input/kernel/scheduler/scheduler_core_return_plan.c",
        "tests/host/selected_release_handoff_native_source_bindings.py:native-source-admission:MEM05/native-input/kernel/scheduler/scheduler_core_validate.c",
        "tests/host/test_endpoint_source_layout.py:data-entry:STABLE_HASHES/kernel/kernel_objects.c",
        "tests/host/test_kernel_object_source_layout.py:data-entry:BASELINE_BODIES/0/micros_hart_resolve_context",
        "tests/host/test_kernel_object_source_layout.py:data-entry:BASELINE_BODIES/0/micros_kernel_objects_validate_base",
        "tests/host/test_kernel_object_source_layout.py:data-entry:BASELINE_BODIES/0/micros_thread_capture_execution_context",
        "tests/host/test_kernel_object_source_layout.py:data-entry:BASE_CORE_DIGEST",
        "tests/host/test_kernel_object_source_layout.py:data-entry:STABLE_HASHES/kernel/scheduler_core_validate.c",
        "tests/host/test_scheduler_source_layout.py:data-entry:STABLE_ARTIFACT_HASHES/kernel/kernel_objects.c",
        "tests/host/test_scheduler_source_layout.py:data-table:FROZEN_FACTS/kernel/scheduler_core.c/micros_hart_plan_user_return",
        "tests/host/test_scheduler_source_layout.py:data-table:FROZEN_FACTS/kernel/scheduler_core.c/micros_scheduler_commit_ipc_transitions",
        "tests/host/test_scheduler_source_layout.py:data-table:FROZEN_FACTS/kernel/scheduler_core.c/micros_scheduler_core_validate",
        "tests/host/test_scheduler_source_layout.py:function-body:FROZEN_FUNCTIONS/kernel/scheduler_core.c/micros_hart_pick_ready",
        "tests/host/test_scheduler_source_layout.py:function-body:FROZEN_FUNCTIONS/kernel/scheduler_core.c/micros_scheduler_account_enter_idle",
        "tests/host/test_scheduler_source_layout.py:function-body:FROZEN_FUNCTIONS/kernel/scheduler_core.c/micros_scheduler_account_enter_thread",
        "tests/host/test_scheduler_source_layout.py:function-body:FROZEN_FUNCTIONS/kernel/scheduler_core.c/micros_scheduler_accounting_initialize",
        "tests/host/test_scheduler_source_layout.py:function-body:FROZEN_FUNCTIONS/kernel/scheduler_core.c/micros_scheduler_commit_current_ipc",
        "tests/host/test_scheduler_source_layout.py:function-body:FROZEN_FUNCTIONS/kernel/scheduler_core.c/micros_scheduler_preflight_current_ipc",
        "tests/host/test_scheduler_source_layout.py:function-body:FROZEN_FUNCTIONS/kernel/scheduler_core.c/micros_scheduler_prepare_ipc_wake",
        "tests/host/test_scheduler_source_layout.py:function-body:FROZEN_FUNCTIONS/kernel/scheduler_core.c/micros_scheduler_rollback_current_ipc",
        "tests/host/test_scheduler_source_layout.py:function-body:FROZEN_FUNCTIONS/kernel/scheduler_core.c/micros_thread_install_policy",
        "tests/host/test_scheduler_source_layout.py:function-body:FROZEN_FUNCTIONS/kernel/scheduler_core.c/micros_thread_runtime_flags_set",
        "tests/host/test_scheduler_source_layout.py:function-body:FROZEN_FUNCTIONS/kernel/scheduler_core.c/micros_thread_runtime_flags_unset",
        "tests/host/test_scheduler_source_layout.py:function-body:FROZEN_FUNCTIONS/kernel/scheduler_core.c/micros_thread_scheduler_admit",
        "tests/host/test_scheduler_source_layout.py:function-body:FROZEN_FUNCTIONS/kernel/scheduler_core.c/micros_thread_scheduler_admit_preflight",
        "tests/host/test_scheduler_source_layout.py:function-body:FROZEN_FUNCTIONS/kernel/scheduler_core.c/micros_thread_scheduler_hold",
        "tests/host/test_scheduler_source_layout.py:function-body:FROZEN_FUNCTIONS/kernel/scheduler_core.c/micros_thread_scheduler_remove",
        "tests/host/test_scheduler_source_layout.py:function-body:FROZEN_FUNCTIONS/kernel/scheduler_core.c/preflight_tail_enqueue",
        "tests/host/test_scheduler_source_layout.py:function-body:FROZEN_FUNCTIONS/kernel/scheduler_core.c/queue_dequeue",
        "tests/host/test_scheduler_source_layout.py:function-body:FROZEN_FUNCTIONS/kernel/scheduler_core.c/queue_enqueue_tail",
        "tests/host/test_scheduler_source_layout.py:function-body:FROZEN_FUNCTIONS/kernel/scheduler_core.c/queue_pick",
        "tests/host/test_scheduler_source_layout.py:function-body:FROZEN_FUNCTIONS/kernel/scheduler_core.c/scratch_load",
        "tests/host/test_scheduler_source_layout.py:function-body:FROZEN_FUNCTIONS/kernel/scheduler_core.c/thread_is_current",
    ),
}
STATE_WORK_CONTRACT = {
    "version": 1,
    "dimensions": (27, 14),
    "producer_base": 0x4f424a3031000000,
    "current_producer_offset": 256,
    "channels": ("ordinary", "aggregate", "stack"),
    "owner_masks": {"ordinary": 0, "aggregate": 24, "stack": 8},
    "cases": 171,
    "baseline_cases_sha256": "384b3cea0de2983eca5cfdd5b6e4b475158f3f49b23990fefd1eaf9769c8f8c3",
    "current_cases_sha256": "8b4f37aa05e892bef45e7d2c9a536ee5f9992ddc4ee4b0c15225faffe4eea955",
    "process_capacity": 64,
    "thread_capacity": 128,
    "hart_capacity": 8,
    "direct_probes": (1, 2, 3),
    "context_bytes": 264,
    "geometry_descriptor_bytes": 48,
    "geometry_cursor_bytes": 72,
    "ipc_candidates_bytes": 5120,
    "return_workspace_bytes": 4224,
    "return_plan_bytes": 1344,
    "preservation": (
        "complete-registry-and-output-failure-atomicity",
        "exact-caller-generations-and-selected-owner-assignment",
        "original-argument-before-stale-batch-precedence",
        "queue-prefix-successor-and-live-post-removal-endpoints",
        "unselected-corruption-remains-a-complete-diagnostic-failure",
        "affine-gaps-zero-base-primary-aliases-and-arbitrary-valid-exceptions",
        "twenty-six-retained-direct-audit-boundaries",
        "minimal-user-idle-accounting-and-original-return-projection",
    ),
}
RECIPE_ADMISSION = {
    "base_cmake_sha256": "4f8e01b1b22b9f94f8b90072b8007a5727f63b48ec3998e987745a3402de5837",
    "legacy_cmake_sha256": "9a6d56f3bb35f3ab25049f67c975cbbedf46abddcdebe6eb7960547b664a87a4",
    "native_targets": (
        "object_hart_baseline_ordinary_test", "object_hart_baseline_aggregate_test",
        "object_hart_baseline_stack_test", "object_hart_current_aggregate_test",
        "object_hart_current_stack_test", "object_hart_current_ordinary_test",
    ),
    "native_case": "object-hart-validation",
    "native_tiers": ("test-unit",),
    "native_label": "slow",
    "native_standard": "c17",
    "native_sanitizers": ("address", "undefined"),
    "source_pool": "mem03_source_proof",
    "generator": "tools/generate_object_hart_validation_fixture.py",
    "generator_output": "build/host-debug/object-hart-validation",
    "driver": "tools/check_object_hart_validation.py",
    "fixture_header": "tests/host/object_hart_validation_test.h",
    "fixture_sources": ("tests/host/object_hart_validation_test.c", "tests/work/work_counts.c"),
    "baseline_headers": "fixed-runtime-reference",
    "current_headers": "actual-current-source",
    "ordinary_observations": "absent",
}
AUDIT_SOURCES = (
    "kernel/boot/bootstrap_control_core.c", "kernel/boot/bootstrap_preparation_rollback.c",
    "kernel/endpoint/endpoint_object_validation.c", "kernel/execution/user_execution.c",
    "kernel/grant/grant_runtime.c", "kernel/ipc/ipc_runtime.c", "kernel/memory/bootstrap_memory.c",
    "kernel/memory/user_address_space_handoff.c", "kernel/objects/kernel_object_runtime.c",
    "kernel/objects/kernel_objects.c", "kernel/pm/pm_control_execution.c",
    "kernel/pm/pm_control_exit.c", "kernel/pm/pm_control_runtime_core.c",
    "kernel/pm/pm_control_validation.c", "kernel/scheduler/scheduler_core_accounting.c",
    "kernel/scheduler/scheduler_core_ipc.c", "kernel/scheduler/scheduler_core_ipc_transition.c",
    "kernel/scheduler/scheduler_core_policy.c", "kernel/scheduler/scheduler_core_return_plan.c",
)
MIGRATED_AUDITS = (
    "micros_thread_scheduler_admit_preflight", "micros_thread_scheduler_hold",
    "micros_thread_scheduler_remove", "micros_thread_runtime_flags_set",
    "micros_thread_runtime_flags_unset", "micros_thread_install_policy", "micros_hart_pick_ready",
    "micros_scheduler_accounting_initialize", "micros_scheduler_account_enter_thread",
    "micros_scheduler_account_enter_idle", "micros_scheduler_preflight_current_ipc",
    "micros_scheduler_rollback_current_ipc", "micros_scheduler_commit_current_ipc",
    "micros_scheduler_commit_ipc_transitions", "micros_scheduler_prepare_ipc_wake", "preflight_return_plan",
)
OBSERVATION_PREFIX = '#include "tests/host/object_hart_validation_test.h"\n'
STACK_FUNCTIONS = frozenset((
    "kernel_object_validation_thread_context_matches",
    "kernel_object_validation_hart_stacks_avoid_peers",
    "kernel_object_ranges_overlap",
))
MUTABLE_FUNCTIONS = {
    "kernel/objects/kernel_object_hart.c": ("micros_hart_resolve_context",),
    "kernel/objects/kernel_object_context.c": ("micros_thread_capture_execution_context",),
    "kernel/objects/kernel_object_validation.c": (
        "kernel_object_validation_thread_context_matches",
        "kernel_object_validation_thread_slots_match",
        "kernel_object_validation_hart_stack_state_matches",
        "kernel_object_validation_hart_slots_match",
        "micros_kernel_objects_validate_base",
    ),
    "kernel/scheduler/scheduler_core.c": ("scheduler_core_thread_is_current",),
    "kernel/scheduler/scheduler_core_queue.c": (
        "scheduler_core_queue_dequeue", "scheduler_core_queue_pick",
        "scheduler_core_preflight_tail_enqueue",
    ),
    "kernel/scheduler/scheduler_core_policy.c": (
        "micros_thread_scheduler_admit_preflight", "micros_thread_scheduler_admit",
        "micros_thread_scheduler_hold", "micros_thread_scheduler_remove",
        "micros_thread_runtime_flags_set", "micros_thread_runtime_flags_unset",
        "micros_thread_install_policy", "micros_hart_pick_ready",
    ),
    "kernel/scheduler/scheduler_core_accounting.c": (
        "micros_scheduler_accounting_initialize", "micros_scheduler_account_enter_thread",
        "micros_scheduler_account_enter_idle",
    ),
    "kernel/scheduler/scheduler_core_ipc.c": (
        "micros_scheduler_preflight_current_ipc", "micros_scheduler_rollback_current_ipc",
        "micros_scheduler_commit_current_ipc",
    ),
    "kernel/scheduler/scheduler_core_ipc_transition.c": (
        "preflight_transition_candidates", "micros_scheduler_commit_ipc_transitions",
        "micros_scheduler_prepare_ipc_wake",
    ),
    "kernel/scheduler/scheduler_core_return_plan.c": (
        "scratch_load", "preflight_return_plan", "micros_hart_plan_user_return",
    ),
    "kernel/scheduler/scheduler_core_validate.c": ("validate_harts",),
}
REMOVED_FUNCTIONS = {
    "kernel/objects/kernel_object_validation.c": ("kernel_object_validation_hart_stacks_avoid_peers",),
    "kernel/scheduler/scheduler_core_queue.c": ("scheduler_core_queue_enqueue_tail",),
}
CURRENT_HELPERS = {
    "kernel/scheduler/scheduler_core.c": {
        "scheduler_core_preflight_registry": (
            "enum micros_kernel_object_error scheduler_core_preflight_registry("
            "const struct micros_kernel_objects *objects)",
            ("kernel_object_require_initialized",), "",
        ),
        "selected_thread_context_is_valid": (
            "static bool selected_thread_context_is_valid(const struct micros_thread *thread)",
            ("kernel_object_stack_range_is_valid", "kernel_object_user_context_is_zero"), "",
        ),
        "selected_thread_policy_is_valid": (
            "static bool selected_thread_policy_is_valid(const struct micros_kernel_objects *objects, "
            "const struct micros_thread *thread, struct micros_thread_handle handle)",
            (
                "micros_hart_resolve", "scheduler_core_hart_handle_is_null",
                "scheduler_core_thread_handle_is_null", "scheduler_core_thread_handles_equal",
            ), "",
        ),
        "scheduler_core_preflight_thread": (
            "enum micros_kernel_object_error scheduler_core_preflight_thread("
            "const struct micros_kernel_objects *objects, struct micros_thread_handle handle, "
            "const struct micros_thread **result)",
            (
                "scheduler_core_preflight_registry", "micros_thread_resolve", "micros_process_resolve",
                "selected_thread_context_is_valid", "selected_thread_policy_is_valid",
            ), "",
        ),
        "selected_hart_stacks_are_valid": (
            "static bool selected_hart_stacks_are_valid(const struct micros_hart *hart)",
            (
                "kernel_object_trap_anchor_stacks_are_valid", "kernel_object_stack_range_is_valid",
                "kernel_object_ranges_overlap", "kernel_object_trap_anchor_is_zero",
            ), "",
        ),
        "selected_hart_accounting_is_valid": (
            "static bool selected_hart_accounting_is_valid(const struct micros_hart *hart, "
            "const struct micros_thread *current)",
            ("scheduler_core_thread_handles_equal", "scheduler_core_thread_handle_is_null"), "",
        ),
        "scheduler_core_preflight_hart": (
            "enum micros_kernel_object_error scheduler_core_preflight_hart("
            "const struct micros_kernel_objects *objects, struct micros_hart_handle handle, "
            "const struct micros_hart **result)",
            (
                "scheduler_core_preflight_registry", "micros_hart_resolve", "selected_hart_stacks_are_valid",
                "scheduler_core_thread_handle_is_null", "scheduler_core_preflight_thread",
                "scheduler_core_hart_handles_equal", "scheduler_core_thread_handles_equal",
                "selected_hart_accounting_is_valid",
            ),
            """
if (!scheduler_core_thread_handle_is_null(hart->current_thread)) {
    if (current->runtime_flags == 0
        && !scheduler_core_thread_handles_equal(
            hart->ready_head[current->scheduler_priority], hart->current_thread)) {}
}
""",
        ),
        "scheduler_core_preflight_thread_operation": (
            "enum micros_kernel_object_error scheduler_core_preflight_thread_operation("
            "const struct micros_kernel_objects *objects, struct micros_thread_handle handle)",
            ("scheduler_core_preflight_thread", "scheduler_core_preflight_hart"), "",
        ),
        "scheduler_core_preflight_queue_record": (
            "enum micros_kernel_object_error scheduler_core_preflight_queue_record("
            "const struct micros_kernel_objects *objects, const struct micros_hart *hart, "
            "uint8_t priority, struct micros_thread_handle handle, const struct micros_thread **result)",
            ("scheduler_core_preflight_thread", "scheduler_core_hart_handles_equal"), "",
        ),
        "scheduler_core_preflight_queue_endpoints": (
            "enum micros_kernel_object_error scheduler_core_preflight_queue_endpoints("
            "const struct micros_kernel_objects *objects, const struct micros_hart *hart, uint8_t priority)",
            (
                "scheduler_core_thread_handle_is_null", "scheduler_core_thread_handles_equal",
                "scheduler_core_null_thread_handle", "scheduler_core_preflight_queue_record",
            ),
            """
head = hart->ready_head[priority];
tail = hart->ready_tail[priority];
""",
        ),
    },
    "kernel/scheduler/scheduler_core_queue.c": {
        "preflight_dequeue_successor": (
            "static enum micros_kernel_object_error preflight_dequeue_successor("
            "const struct micros_kernel_objects *objects, const struct micros_hart *hart, "
            "struct micros_thread_handle handle, const struct micros_thread *thread, size_t steps)",
            (
                "scheduler_core_thread_handles_equal", "scheduler_core_thread_handle_is_null",
                "scheduler_core_preflight_queue_record",
            ),
            """
if (scheduler_core_thread_handles_equal(hart->ready_tail[thread->scheduler_priority], handle)) {}
cursor = hart->ready_head[thread->scheduler_priority];
for (index = 1; index < steps; ++index) {
    cursor = objects->threads[cursor.slot].ready_next;
}
""",
        ),
    },
    "kernel/scheduler/scheduler_core_ipc_transition.c": {
        "preflight_transition_stamps": (
            "static enum micros_kernel_object_error preflight_transition_stamps("
            "const struct micros_scheduler_ipc_transition *requests, size_t request_count, "
            "struct scheduler_ipc_transition_candidate *transitions)",
            (
                "ipc_transition_clear_flags_are_valid", "ipc_transition_set_flags_are_valid",
                "preflight_transition_shapes", "scheduler_core_thread_handles_equal",
            ),
            """
for (index = 0; index < request_count; ++index) {
    if (!ipc_transition_clear_flags_are_valid(requests[index].clear_flags)
        || !ipc_transition_set_flags_are_valid(requests[index].set_flags)
        || (requests[index].clear_flags & requests[index].set_flags) != 0) {}
    if (requests[index].handle.slot >= MICROS_THREAD_CAPACITY) {}
    transitions[requests[index].handle.slot].handle.slot = UINT16_MAX;
}
for (index = 0; index < request_count; ++index) {
    handle = requests[index].handle;
    stamp = &transitions[handle.slot].handle;
}
""",
        ),
    },
    "kernel/scheduler/scheduler_core_return_plan.c": {
        "preflight_return_threads": (
            "static enum micros_kernel_object_error preflight_return_threads("
            "const struct micros_kernel_objects *objects, const struct micros_hart *hart, "
            "struct micros_hart_handle hart_handle, const struct micros_scheduler_return_plan *candidate)",
            (
                "scheduler_core_hart_handles_equal", "scheduler_core_preflight_thread",
                "scheduler_core_bitmap_test",
            ),
            """
for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
    const struct micros_thread *thread = &objects->threads[index];
}
""",
        ),
    },
    "kernel/objects/kernel_object_validation.c": {
        "kernel_object_validation_stack_stride": (
            "static uintptr_t kernel_object_validation_stack_stride(size_t cohort)", (), "",
        ),
        "kernel_object_validation_stack_base": (
            "static bool kernel_object_validation_stack_base(size_t cohort, size_t slot, "
            "uintptr_t bottom, uintptr_t top, uintptr_t *base)",
            ("kernel_object_validation_stack_stride",),
            """
if (top - bottom != stride || slot > UINTPTR_MAX / stride || bottom < slot * stride) {}
""",
        ),
        "kernel_object_validation_stack_accumulate": (
            "static void kernel_object_validation_stack_accumulate(struct kernel_object_stack_geometry *geometry, "
            "size_t cohort, size_t slot, uintptr_t bottom, uintptr_t top)",
            ("kernel_object_validation_stack_base",),
            """
if (kernel_object_validation_stack_base(cohort, slot, bottom, top, &base)) {
    if (!geometry->seeded[cohort]) {
    } else if (base != geometry->bases[cohort]) {}
}
""",
        ),
        "kernel_object_validation_stack_is_regular": (
            "static bool kernel_object_validation_stack_is_regular(const struct kernel_object_stack_geometry *geometry, "
            "size_t cohort, size_t slot, uintptr_t bottom, uintptr_t top)",
            ("kernel_object_validation_stack_base",),
            """
return geometry->seeded[cohort]
    && kernel_object_validation_stack_base(cohort, slot, bottom, top, &base)
    && base == geometry->bases[cohort];
""",
        ),
        "kernel_object_validation_stack_range_at": (
            "static bool kernel_object_validation_stack_range_at(const struct micros_kernel_objects *objects, "
            "size_t cohort, size_t slot, uintptr_t *bottom, uintptr_t *top)", (),
            """
if (cohort == 0) {
    const struct micros_thread *thread = &objects->threads[slot];
} else {
    const struct micros_hart *hart = &objects->harts[slot];
}
""",
        ),
        "kernel_object_validation_stack_next_regular": (
            "static void kernel_object_validation_stack_next_regular(const struct micros_kernel_objects *objects, "
            "const struct kernel_object_stack_geometry *geometry, size_t cohort, struct kernel_object_stack_cursor *cursor)",
            ("kernel_object_validation_stack_range_at", "kernel_object_validation_stack_is_regular"),
            """
for (; cursor->slot < limit; ++cursor->slot) {}
""",
        ),
        "kernel_object_validation_stack_regulars_avoid": (
            "static bool kernel_object_validation_stack_regulars_avoid(const struct micros_kernel_objects *objects, "
            "const struct kernel_object_stack_geometry *geometry)",
            ("kernel_object_validation_stack_next_regular",),
            """
for (cohort = 0; cohort < 3; ++cohort) {}
for (;;) {
    for (cohort = 0; cohort < 3; ++cohort) {
        if (cursors[cohort].top != 0
            && (selected == 3 || cursors[cohort].bottom < cursors[selected].bottom)) {}
    }
    if (cursors[selected].bottom < previous_top) {}
}
""",
        ),
        "kernel_object_validation_stack_exceptions_avoid": (
            "static bool kernel_object_validation_stack_exceptions_avoid(const struct micros_kernel_objects *objects, "
            "const struct kernel_object_stack_geometry *geometry)",
            (
                "kernel_object_validation_stack_range_at", "kernel_object_validation_stack_is_regular",
                "kernel_object_ranges_overlap",
            ),
            """
for (index = 0; index < MICROS_THREAD_CAPACITY + 2 * MICROS_HART_CAPACITY; ++index) {
    for (other = 0; other < MICROS_THREAD_CAPACITY + 2 * MICROS_HART_CAPACITY; ++other) {}
}
""",
        ),
    },
}
CURRENT_COMPARISONS = {
    "kernel_object_validation_stack_base": (
        "top - bottom != stride", "slot > UINTPTR_MAX / stride", "bottom < slot * stride",
    ),
    "kernel_object_validation_stack_accumulate": ("base != geometry->bases[cohort]",),
    "kernel_object_validation_stack_is_regular": ("base == geometry->bases[cohort]",),
    "kernel_object_validation_stack_regulars_avoid": (
        "cursors[cohort].bottom < cursors[selected].bottom", "cursors[selected].bottom < previous_top",
    ),
}
CURRENT_SIGNATURES = {
    "kernel_object_validation_thread_context_matches":
        "static bool kernel_object_validation_thread_context_matches("
        "const struct micros_thread *thread, size_t index, "
        "struct kernel_object_stack_geometry *geometry)",
    "kernel_object_validation_thread_slots_match":
        "static bool kernel_object_validation_thread_slots_match(const struct micros_kernel_objects *objects, "
        "size_t process_thread_counts[MICROS_PROCESS_CAPACITY], size_t *observed_thread_count, "
        "struct kernel_object_stack_geometry *geometry)",
    "kernel_object_validation_hart_stack_state_matches":
        "static bool kernel_object_validation_hart_stack_state_matches(const struct micros_hart *hart, "
        "size_t index, struct kernel_object_stack_geometry *geometry)",
    "kernel_object_validation_hart_slots_match":
        "static bool kernel_object_validation_hart_slots_match(const struct micros_kernel_objects *objects, "
        "bool current_threads[MICROS_THREAD_CAPACITY], size_t *observed_hart_count, "
        "struct kernel_object_stack_geometry *geometry)",
    "scheduler_core_queue_pick":
        "enum micros_kernel_object_error scheduler_core_queue_pick(const struct micros_kernel_objects *objects, "
        "const struct micros_hart *hart, struct micros_thread_handle *result)",
    "scratch_load":
        "static enum micros_kernel_object_error scratch_load(const struct micros_kernel_objects *objects, "
        "const struct micros_hart *hart, struct micros_scheduler_return_plan *candidate)",
}
MUTABLE_SHAPES = {
    "micros_hart_resolve_context": "candidate = &objects->harts[index];",
    "micros_thread_capture_execution_context": "",
    "scheduler_core_thread_is_current": "",
    "kernel_object_validation_thread_context_matches": "",
    "scheduler_core_queue_dequeue": """
thread = &objects->threads[thread_handle.slot];
cursor = hart->ready_head[priority];
while (!scheduler_core_thread_handle_is_null(cursor)) {
    if (scheduler_core_thread_handles_equal(cursor, thread_handle)) {
        if (scheduler_core_thread_handle_is_null(previous)) {
            hart->ready_head[priority] = candidate->ready_next;
        } else {
            objects->threads[previous.slot].ready_next = candidate->ready_next;
        }
        if (scheduler_core_thread_handles_equal(hart->ready_tail[priority], thread_handle)) {
            hart->ready_tail[priority] = previous;
        }
        if (scheduler_core_thread_handle_is_null(hart->ready_head[priority])) {
            hart->ready_tail[priority] = scheduler_core_null_thread_handle();
        }
    }
    cursor = candidate->ready_next;
}
""",
    "scheduler_core_preflight_tail_enqueue": """
if (!scheduler_core_thread_handle_is_null(hart->current_thread)) {
    current = &objects->threads[hart->current_thread.slot];
}
""",
    "scheduler_core_queue_pick": """
for (priority = 0; priority < MICROS_SCHEDULER_PRIORITY_COUNT; ++priority) {
    if (!scheduler_core_thread_handle_is_null(hart->ready_head[priority])) {
        *result = hart->ready_head[priority];
    }
}
""",
    "micros_thread_scheduler_admit_preflight": "",
    "preflight_return_plan": "",
    "scratch_load": """
for (priority = 0; priority < MICROS_SCHEDULER_PRIORITY_COUNT; ++priority) {
    struct micros_thread_handle cursor = hart->ready_head[priority];
    while (!scheduler_core_thread_handle_is_null(cursor)) {
        plan_scratch.slots[priority][count++] = cursor.slot;
        cursor = thread->ready_next;
    }
    if (!scheduler_core_thread_handles_equal(last, hart->ready_tail[priority])) {}
    plan_scratch.counts[priority] = count;
}
""",
}
CALL_CHANGES = {
    "micros_hart_resolve_context": ((), ()),
    "micros_thread_capture_execution_context": (
        ("kernel_object_resolve_process_mutable", "kernel_object_resolve_hart_mutable", "kernel_object_stack_range_is_valid"),
        (),
    ),
    "scheduler_core_thread_is_current": (("micros_thread_resolve", "micros_hart_resolve"), ()),
    "kernel_object_validation_thread_context_matches": (
        ("kernel_object_validation_stack_accumulate",), ("kernel_object_ranges_overlap",),
    ),
    "kernel_object_validation_thread_slots_match": ((), ()),
    "kernel_object_validation_hart_stack_state_matches": (
        ("kernel_object_validation_stack_accumulate",), ("kernel_object_validation_hart_stacks_avoid_peers",),
    ),
    "kernel_object_validation_hart_slots_match": ((), ()),
    "micros_kernel_objects_validate_base": (
        ("kernel_object_validation_stack_regulars_avoid", "kernel_object_validation_stack_exceptions_avoid"), (),
    ),
    "scheduler_core_queue_dequeue": (
        ("scheduler_core_preflight_thread", "scheduler_core_preflight_queue_endpoints",
         "scheduler_core_preflight_queue_record", "preflight_dequeue_successor"), (),
    ),
    "scheduler_core_queue_pick": (
        ("scheduler_core_preflight_queue_endpoints",), ("scheduler_core_null_thread_handle",),
    ),
    "scheduler_core_preflight_tail_enqueue": (("scheduler_core_preflight_queue_endpoints",), ()),
    "micros_thread_scheduler_admit_preflight": (
        ("scheduler_core_preflight_thread", "scheduler_core_preflight_hart"),
        ("micros_scheduler_core_validate", "micros_thread_resolve", "micros_hart_resolve"),
    ),
    "micros_thread_scheduler_admit": (
        ("scheduler_core_queue_enqueue_tail_prevalidated",), ("scheduler_core_queue_enqueue_tail",),
    ),
    "micros_thread_scheduler_hold": (("scheduler_core_preflight_thread_operation",), ("micros_scheduler_core_validate",)),
    "micros_thread_scheduler_remove": (("scheduler_core_preflight_thread_operation",), ("micros_scheduler_core_validate",)),
    "micros_thread_runtime_flags_set": (("scheduler_core_preflight_thread_operation",), ("micros_scheduler_core_validate",)),
    "micros_thread_runtime_flags_unset": (
        ("scheduler_core_preflight_thread_operation", "scheduler_core_queue_enqueue_tail_prevalidated"),
        ("micros_scheduler_core_validate", "scheduler_core_queue_enqueue_tail"),
    ),
    "micros_thread_install_policy": (
        ("scheduler_core_preflight_thread_operation", "scheduler_core_queue_enqueue_tail_prevalidated"),
        ("micros_scheduler_core_validate", "scheduler_core_queue_enqueue_tail"),
    ),
    "micros_hart_pick_ready": (
        ("scheduler_core_preflight_hart",),
        ("micros_scheduler_core_validate", "micros_hart_resolve", "scheduler_core_thread_handle_is_null"),
    ),
    "micros_scheduler_accounting_initialize": (("scheduler_core_preflight_hart",), ("micros_scheduler_core_validate",)),
    "micros_scheduler_account_enter_thread": (("scheduler_core_preflight_hart",), ("micros_scheduler_core_validate",)),
    "micros_scheduler_account_enter_idle": (("scheduler_core_preflight_hart",), ("micros_scheduler_core_validate",)),
    "micros_scheduler_preflight_current_ipc": (
        ("scheduler_core_preflight_hart",), ("micros_scheduler_core_validate", "micros_hart_resolve"),
    ),
    "micros_scheduler_rollback_current_ipc": (
        ("scheduler_core_preflight_hart", "scheduler_core_preflight_thread", "scheduler_core_preflight_queue_endpoints"),
        ("micros_scheduler_core_validate",),
    ),
    "micros_scheduler_commit_current_ipc": (
        ("scheduler_core_preflight_hart", "scheduler_core_preflight_thread"), ("micros_scheduler_core_validate",),
    ),
    "preflight_transition_candidates": (("scheduler_core_preflight_thread_operation",), ()),
    "micros_scheduler_commit_ipc_transitions": (
        ("scheduler_core_preflight_registry", "preflight_transition_stamps"),
        ("micros_scheduler_core_validate", "preflight_transition_shapes"),
    ),
    "micros_scheduler_prepare_ipc_wake": (("scheduler_core_preflight_thread_operation",), ("micros_scheduler_core_validate",)),
    "preflight_return_plan": (
        ("scheduler_core_preflight_hart",),
        ("micros_scheduler_core_validate", "micros_hart_resolve", "scheduler_core_thread_handle_is_null"),
    ),
    "scratch_load": (
        ("scheduler_core_preflight_queue_endpoints", "scheduler_core_preflight_queue_record",
         "scheduler_core_bitmap_test", "scheduler_core_bitmap_set", "scheduler_core_thread_handles_equal",
         "scheduler_core_null_thread_handle"), (),
    ),
    "micros_hart_plan_user_return": (("preflight_return_threads",), ()),
    "validate_harts": ((), ("scheduler_core_hart_has_assigned_thread",)),
}
SKELETON_ADDITIONS = {
    "kernel/scheduler/scheduler_core.c": '#include "kernel/objects/kernel_object_private.h"\n',
    "kernel/scheduler/scheduler_core_ipc_transition.c": """
_Static_assert(MICROS_THREAD_CAPACITY < UINT16_MAX, "IPC stamp sentinel must not name a thread slot");
""",
    "kernel/objects/kernel_object_validation.c": """
struct kernel_object_stack_geometry {
    uintptr_t bases[3];
    size_t owners;
    size_t exceptions;
    bool seeded[3];
};

struct kernel_object_stack_cursor {
    size_t slot;
    uintptr_t bottom;
    uintptr_t top;
};

_Static_assert(sizeof(struct kernel_object_stack_geometry) <= 48, "Stack geometry must remain constant-sized");
_Static_assert(sizeof(struct kernel_object_stack_cursor) * 3 <= 72, "Stack cursors must remain constant-sized");
""",
}


def fixed_source(root, path):
    from tests.host.object_hart_validation_evidence import fixed_native_inputs

    return fixed_native_inputs(root)[path].decode()


def _current_shape(path, name, original):
    if name in MUTABLE_SHAPES:
        return MUTABLE_SHAPES[name]
    if name == "kernel_object_validation_thread_slots_match":
        before = "kernel_object_validation_thread_context_matches(objects, thread, index)"
        after = "kernel_object_validation_thread_context_matches(thread, index, geometry)"
        if original.count(before) != 1:
            raise ValueError("OBJ01 geometry caller lost its fixed context-admission boundary")
        return original.replace(before, after, 1)
    if name == "preflight_transition_candidates":
        anchor = "        error = scheduler_core_resolve_thread_mutable("
        if original.count(anchor) != 1:
            raise ValueError("OBJ01 batch contract lost its exact candidate-resolution boundary")
        return original.replace(
            anchor,
            "        error = scheduler_core_preflight_thread_operation(objects, transitions[index].handle);\n" + anchor,
            1,
        )
    return original


@lru_cache(maxsize=4)
def current_function_contracts(root):
    from tests.host.selected_address_space_source import _function_calls

    contracts = {}
    names = {name for functions in MUTABLE_FUNCTIONS.values() for name in functions}
    if set(CALL_CHANGES) != names or not set(CURRENT_SIGNATURES) <= names or not set(MUTABLE_SHAPES) <= names:
        raise ValueError("OBJ01 mutable function contracts do not form one complete finite declaration")
    for path, mutable in MUTABLE_FUNCTIONS.items():
        original = function_records(fixed_source(root, path))
        removed = set(REMOVED_FUNCTIONS.get(path, ()))
        helpers = CURRENT_HELPERS.get(path, {})
        if (
            not set(mutable) | removed <= set(original)
            or set(mutable) & removed or set(helpers) & set(original)
        ):
            raise ValueError("OBJ01 function partition differs from the fixed definitions")
        for name in mutable:
            record = original[name]
            old_calls = _function_calls(record.body)
            added, dropped = map(set, CALL_CHANGES[name])
            if not dropped <= old_calls or added & old_calls:
                raise ValueError("OBJ01 callee contract differs from its fixed predecessor: " + name)
            contracts[path + "/" + name] = (
                compact(CURRENT_SIGNATURES.get(name, record.signature)), record.guards,
                tuple(sorted(old_calls - dropped | added)),
                primitive_shape(path, name, _current_shape(path, name, record.body)),
            )
        for name, (signature, calls, shape) in helpers.items():
            if len(set(calls)) != len(calls):
                raise ValueError("OBJ01 helper declares a duplicate callee")
            contracts[path + "/" + name] = (
                compact(signature), (), tuple(sorted(calls)), primitive_shape(path, name, shape),
            )
    return contracts


def declared_header(root):
    def declaration(signature):
        prefix, arguments = signature.split("(", 1)
        return prefix + "(\n    " + arguments[:-1].replace(", ", ",\n    ") + "\n);"

    path = "kernel/scheduler/scheduler_core_private.h"
    text = fixed_source(root, path)
    for name in ("scheduler_core_queue_enqueue_tail", "scheduler_core_queue_pick"):
        pattern = r"(?m)^(?:enum micros_kernel_object_error|struct micros_thread_handle)\s+" + name + r"\s*\([^;{}]*\);"
        matches = list(re.finditer(pattern, text))
        if len(matches) != 1:
            raise ValueError("OBJ01 private header lost one exact old queue declaration")
        replacement = "" if name in REMOVED_FUNCTIONS["kernel/scheduler/scheduler_core_queue.c"] else declaration(CURRENT_SIGNATURES[name])
        text = text[:matches[0].start()] + replacement + text[matches[0].end():]
    additions = tuple(
        declaration(signature) for signature, _, _ in CURRENT_HELPERS["kernel/scheduler/scheduler_core.c"].values()
        if not signature.startswith("static ")
    )
    if len(additions) != 6 or text.count("#endif") != 1:
        raise ValueError("OBJ01 private selected-preflight declaration set is incomplete")
    return text.replace("#endif", "\n\n".join(additions) + "\n\n#endif", 1)


def expected_skeleton(root, path):
    from tests.host.selected_address_space_source import _source_skeleton

    original = fixed_source(root, path)
    if path == "kernel/scheduler/scheduler_core.c":
        original = original.replace(
            '#include "kernel/scheduler/scheduler_core_private.h"\n',
            '#include "kernel/scheduler/scheduler_core_private.h"\n' + SKELETON_ADDITIONS[path],
            1,
        )
    elif path in SKELETON_ADDITIONS:
        record = next(iter(function_records(original).values()))
        first = original.index(record.signature + "{" + record.body + "}")
        original = original[:first] + SKELETON_ADDITIONS[path] + original[first:]
    return _source_skeleton(original)


def validate_retained_source(root, path, text, stage):
    from tests.host.common_scheduler_return_source import CURRENT_BODIES, legacy_native_admission

    retired = legacy_native_admission(root, "OBJ01", path, text, stage)
    from tests.host.object_hart_validation_evidence import NATIVE_HEADERS, NATIVE_SOURCES
    from tests.host.selected_address_space_source import _function_calls, _source_skeleton

    if stage not in ("baseline", "current") or type(text) is not str or path not in (*NATIVE_SOURCES, *NATIVE_HEADERS):
        raise ValueError("OBJ01 source admission has an unknown stage or subject")
    original = fixed_source(root, path)
    if stage == "baseline" or path not in (*MUTABLE_FUNCTIONS, "kernel/scheduler/scheduler_core_private.h"):
        if "OBJ01/current-input/" + path in retired:
            return
        if text != original:
            raise ValueError("OBJ01 fixed native input changed: " + path)
        return
    if path.endswith(".h"):
        if text != declared_header(root):
            raise ValueError("OBJ01 selected-preflight header differs from its independent declaration")
        return
    before, after = function_records(original), function_records(text)
    expected_names = set(before).difference(REMOVED_FUNCTIONS.get(path, ())) | set(CURRENT_HELPERS.get(path, ()))
    if "OBJ01/definitions/" + path not in retired and set(after) != expected_names:
        raise ValueError("OBJ01 current source has a missing or undeclared function: " + path)
    contracts = current_function_contracts(str(Path(root).resolve()))
    for name, record in after.items():
        key = path + "/" + name
        if "OBJ01/function/" + path + "/" + name in retired or retired and name in CURRENT_BODIES.get(path, {}):
            continue
        if key not in contracts:
            previous = before[name]
            if (record.signature, record.guards, record.body) != (previous.signature, previous.guards, previous.body):
                raise ValueError("OBJ01 retained function changed: " + key)
            validate_observation_contract(root, path, name, record.body, stage)
            continue
        signature, guards, calls, shape = contracts[key]
        if (
            compact(record.signature) != signature or record.guards != guards
            or tuple(sorted(_function_calls(record.body))) != calls
            or re.search(r"^\s*#", record.body, re.M)
        ):
            raise ValueError("OBJ01 function escaped its signature, guards or callees: " + key)
        validate_observation_contract(root, path, name, record.body, stage)
    if "OBJ01/skeleton/" + path not in retired and _source_skeleton(text) != expected_skeleton(root, path):
        raise ValueError("OBJ01 changed an unadmitted include, type or storage owner: " + path)


@lru_cache(maxsize=256)
def observation_contracts(root, path, stage):
    if stage not in ("baseline", "current"):
        raise ValueError("OBJ01 observations require an exact source stage")
    original = function_records(fixed_source(root, path))
    contracts = {
        name: primitive_shape(path, name, record.body)
        for name, record in original.items()
        if stage == "baseline" or name not in REMOVED_FUNCTIONS.get(path, ())
    }
    if stage == "current" and path in MUTABLE_FUNCTIONS:
        for key, declaration in current_function_contracts(root).items():
            if key.rsplit("/", 1)[0] == path:
                contracts[key.rsplit("/", 1)[1]] = declaration[3]
    return contracts


def validate_observation_contract(root, path, name, body, stage):
    from tests.host.common_scheduler_return_source import legacy_observation_contract

    if legacy_observation_contract(root, path, name, body, stage):
        return
    contracts = observation_contracts(str(Path(root).resolve()), path, stage)
    if name not in contracts or primitive_shape(path, name, body) != contracts[name]:
        raise ValueError("OBJ01 function has changed or undeclared primitive coverage: " + path + "/" + name)


def native_assertion_records(root):
    from tests.host.evidence_authority_types import digest
    from tests.host.object_hart_validation_evidence import NATIVE_SOURCES, fixed_native_inputs

    root = str(Path(root).resolve())
    fixed = fixed_native_inputs(root)
    current = current_function_contracts(root)
    rows = []

    def add(kind, path, policy, name=""):
        scope = "OBJ01/" + kind + "/" + path + ("/" + name if name else "")
        rows.append((scope, path, (SOURCE_PATH,), policy))

    for path, raw in sorted(fixed.items()):
        add("baseline-input", path, {"comparison": "exact-file", "sha256": hashlib.sha256(raw).hexdigest()})
        if path not in (*MUTABLE_FUNCTIONS, "kernel/scheduler/scheduler_core_private.h"):
            add("current-input", path, {"comparison": "exact-file", "sha256": hashlib.sha256(raw).hexdigest()})
        elif path.endswith(".h"):
            add("header", path, {"comparison": "declared-header", "sha256": hashlib.sha256(declared_header(root).encode()).hexdigest()})
        else:
            original = function_records(raw.decode())
            names = set(original).difference(REMOVED_FUNCTIONS.get(path, ())) | set(CURRENT_HELPERS.get(path, ()))
            add("definitions", path, {"comparison": "definition-set", "expected": tuple(sorted(names))})
            add("skeleton", path, {"comparison": "source-skeleton", "expected": expected_skeleton(root, path)})
            for name in sorted(names):
                key = path + "/" + name
                if key in current:
                    policy = {"comparison": "current-signature-guards-callees", "expected": current[key][:3]}
                else:
                    record = original[name]
                    policy = {
                        "comparison": "retained-signature-guards-body",
                        "sha256": digest((record.signature, record.guards, record.body)),
                    }
                add("function", path, policy, name)
        if path in NATIVE_SOURCES:
            for stage in ("baseline", "current"):
                for name, declaration in observation_contracts(root, path, stage).items():
                    add(stage + "-observation", path, {"comparison": "primitive-coverage", "expected": declaration}, name)
    return tuple(sorted(rows))


def candidate_paths():
    paths = (*SOURCE_ROLES["production_paths"], *SOURCE_ROLES["fixed_adapters"], *SOURCE_ROLES["unchanged_candidates"])
    if len(paths) != 38 or len(set(paths)) != 38:
        raise ValueError("OBJ01 candidate domain differs from its thirty-eight exact paths")
    return tuple(sorted(paths))


def read_declarations(root):
    root = Path(root).resolve()
    result = {}
    for path, names in (
        (AUTHORITY_PATH, ("DESIGN_PINS", "ORACLE_RULES")),
        (SOURCE_PATH, (
            "SOURCE_ROLES", "SOURCE_PROJECTION", "STATE_WORK_CONTRACT", "RECIPE_ADMISSION",
            "MUTABLE_FUNCTIONS", "REMOVED_FUNCTIONS", "CURRENT_HELPERS", "CURRENT_COMPARISONS",
            "CURRENT_SIGNATURES", "MUTABLE_SHAPES", "CALL_CHANGES", "SKELETON_ADDITIONS",
            "AUDIT_SOURCES", "MIGRATED_AUDITS",
        )),
    ):
        tree = ast.parse(regular_source(root, path).read_text())
        result.update((name, literal_value(single_assignment(tree, name).value)) for name in names)
    return result


def native_binding_facts(root, family, relative):
    from collections import Counter

    root = Path(root).resolve()
    if family != NATIVE_FAMILY or relative != BINDINGS_PATH:
        raise ValueError("OBJ01 native declaration has an unknown owner")
    tree = ast.parse(regular_source(root, relative).read_text())
    declarations = {"BINDING_REFERENCE", "NATIVE_SOURCE_BINDINGS"}
    for node in tree.body:
        if isinstance(node, ast.Expr) and isinstance(node.value, ast.Constant) and type(node.value.value) is str:
            continue
        if (
            not isinstance(node, ast.Assign) or len(node.targets) != 1
            or not isinstance(node.targets[0], ast.Name) or node.targets[0].id not in declarations
        ):
            raise ValueError("OBJ01 native declaration is not closed literal data")
    reference = literal_value(single_assignment(tree, "BINDING_REFERENCE").value)
    rows = literal_value(single_assignment(tree, "NATIVE_SOURCE_BINDINGS").value)
    pins = read_declarations(root)["DESIGN_PINS"]
    if reference != (
        ("policy", pins["design_commit"], pins["design_tree"]),
        ("OBJ01", pins["runtime_reference"], pins["runtime_tree"]),
    ):
        raise ValueError("OBJ01 native predicates borrowed another design or runtime reference")
    if (
        type(rows) is not tuple or len(rows) != 468
        or digest(rows) != SOURCE_PROJECTION["native_bindings_sha256"]
        or rows != native_assertion_records(root)
    ):
        raise ValueError("OBJ01 native predicates differ from their complete independent declaration")
    counts = Counter(row[0].split("/")[1] for row in rows)
    if counts != {
        "baseline-input": 26, "baseline-observation": 136, "current-input": 14,
        "current-observation": 155, "definitions": 11, "function": 114, "header": 1, "skeleton": 11,
    }:
        raise ValueError("OBJ01 native predicates omitted a source/observation policy")
    consumers = tuple(f"B{index:02d}" for index in range(1, 13)) + ("B14",)
    facts = []
    for scope, subject, enforcers, policy in rows:
        payloads = tuple(sorted({relative, subject, *enforcers}))
        for path in payloads:
            regular_source(root, path)
        facts.append(Fact(
            relative + ":native-source-admission:" + scope, family, relative,
            "native-source-admission", scope, AuthorityClass.TRANSPORT_IDENTITY,
            "B03", consumers, payloads, digest(policy),
        ))
    return tuple(facts)


def declaration_facts(root, owner):
    if owner not in (SOURCE_PATH, AUTHORITY_PATH):
        raise ValueError("OBJ01 has no declaration in this source owner")
    values = read_declarations(root)
    pins, roles = values["DESIGN_PINS"], values["SOURCE_ROLES"]
    references = tuple(pins[name] for name in ("design_document", "adr_document", "runtime_adr_document", "research_document"))
    consumers = tuple(f"B{index:02d}" for index in range(1, 13)) + ("B14",)
    candidates = (*roles["production_paths"], *roles["fixed_adapters"], *roles["unchanged_candidates"])

    def fact(kind, scope, classification, payloads, value):
        return Fact(
            owner + ":" + kind + ":" + scope, FAMILY, owner, kind, scope, classification,
            "B03", consumers, tuple(sorted({owner, *references, *payloads})), digest(value),
        )

    if owner == AUTHORITY_PATH:
        return (
            fact("design-anchor", "OBJ01", AuthorityClass.TRANSPORT_IDENTITY, (), pins),
            *(fact("baseline-oracle", row[0], AuthorityClass.BASELINE_ORACLE, roles["immutable_additions"], row)
              for row in values["ORACLE_RULES"]),
        )
    projection = {name: value for name, value in values.items() if name not in (
        "DESIGN_PINS", "ORACLE_RULES", "SOURCE_ROLES", "RECIPE_ADMISSION", "STATE_WORK_CONTRACT",
    )}
    return (
        fact("source-membership", "OBJ01", AuthorityClass.PERMANENT_INVARIANT,
             (*candidates, *roles["immutable_additions"], *roles["immutable_adapters"]), roles),
        fact("artifact-contract", "OBJ01-recipes", AuthorityClass.ARTIFACT_CONTRACT,
             ("CMakeLists.txt", "CMakePresets.json", *roles["immutable_additions"],
              "include/micros/work_observation.h", "tests/work/work_counts.h", "tests/work/work_counts.c"),
             values["RECIPE_ADMISSION"]),
        fact("state-work-contract", "OBJ01", AuthorityClass.PERMANENT_INVARIANT,
             (*roles["production_paths"], *roles["immutable_additions"]), values["STATE_WORK_CONTRACT"]),
        fact("source-projection", "OBJ01", AuthorityClass.TRANSPORT_IDENTITY,
             (*candidates, *roles["immutable_additions"], "tests/host/c_source_records.py",
              "tests/host/work_count_lexical.py", "tools/stack_bound_fatal_effects.py"), projection),
    )


@lru_cache(maxsize=32)
def fixed_audit_source(root, path):
    if path not in (*AUDIT_SOURCES, "kernel/scheduler/scheduler_core_validate.c"):
        raise ValueError("OBJ01 fixed audit source leaves its closed namespace")
    return subprocess.check_output(
        ("git", "show", SOURCE_PROJECTION["source_reference"] + ":" + path),
        cwd=root, text=True, timeout=30,
    )


def audit_rows(sources):
    from tests.host.c_source_records import cpp_guards
    from tests.host.test_frame_ownership_source_layout import c_lexical_text

    pattern = r"\b(micros_kernel_objects_validate|micros_kernel_object_runtime_validate|micros_scheduler_core_validate)\s*\("
    rows = []
    for path, text in sources.items():
        for name, record in function_records(text).items():
            for match in re.finditer(pattern, c_lexical_text(record.body)):
                rows.append((path, name, match[1], (*record.guards, *cpp_guards(record.body, match.start()))))
    return tuple(sorted(rows))


def measure_audit_boundaries(root, stage, *, current_sources=None):
    from collections.abc import Mapping
    from tests.host.evidence_authority_scope import source_bytes
    from tests.host.selected_address_space_source import _function_calls

    if stage not in ("baseline", "current"):
        raise ValueError("OBJ01 caller census has an unknown source stage")
    root = str(Path(root).resolve())
    current_paths = {*AUDIT_SOURCES, "kernel/scheduler/scheduler_core_validate.c"}
    if current_sources is not None and (
        stage != "current" or not isinstance(current_sources, Mapping)
        or set(current_sources) != current_paths
        or any(type(raw) is not bytes for raw in current_sources.values())
    ):
        raise ValueError("OBJ01 current audit snapshot is incomplete or untyped")
    fixed = {path: fixed_audit_source(root, path) for path in AUDIT_SOURCES}
    expected = audit_rows(fixed)
    if len(expected) != 42 or digest(expected) != SOURCE_PROJECTION["audit_boundaries_sha256"]:
        raise ValueError("OBJ01 fixed complete-audit census differs from its independent declaration")
    selected = tuple(row for row in expected if row[1] in MIGRATED_AUDITS)
    retained = tuple(row for row in expected if row not in selected)
    if len(selected) != 16 or len(retained) != 26 or {row[1] for row in selected} != set(MIGRATED_AUDITS):
        raise ValueError("OBJ01 complete-audit partition changed")
    observed = expected if stage == "baseline" else audit_rows({
        path: (
            source_bytes(Path(root), Path(root) / path)
            if current_sources is None else current_sources[path]
        ).decode() for path in AUDIT_SOURCES
    })
    migrated = tuple(row for row in observed if row in selected)
    if tuple(row for row in observed if row not in selected) != retained:
        raise ValueError("OBJ01 changed an outside complete-audit boundary")
    path = "kernel/scheduler/scheduler_core_validate.c"
    text = fixed_audit_source(root, path) if stage == "baseline" else (
        source_bytes(Path(root), Path(root) / path) if current_sources is None else current_sources[path]
    ).decode()
    root_body = function_records(text)["micros_scheduler_core_validate"].body
    if "micros_kernel_objects_validate_base" not in _function_calls(root_body):
        raise ValueError("OBJ01 removed the complete scheduler-to-object-base edge")
    return {"stage": stage, "retained": retained, "migrated": migrated, "base_edge": 1}


def body_sites(path, name, body):
    from tests.host.selected_address_space_source import _matching
    from tests.host.selected_ownership_source import _original_regions
    from tests.host.selected_release_handoff_source import _array_declaration

    positions = positional_tokens(body)
    values = tuple(item.value for item in positions)
    stacks, closings = compound_layout(values)
    sites = []
    for row in _body_sites(path, name, body):
        identifier, mode, _, owner, kind, begin, end, primitive, regions, units = row
        stack_work = name in STACK_FUNCTIONS or (
            name.startswith("kernel_object_validation_stack_")
            and (kind == "RECORD_VISIT" or name in (
                "kernel_object_validation_stack_next_regular",
                "kernel_object_validation_stack_exceptions_avoid",
            ))
        )
        channel = "stack" if stack_work else "aggregate"
        sites.append((
            identifier.replace("mem05-", "obj01-", 1), mode, channel, owner,
            kind, begin, end, primitive, regions, units,
        ))
    if name == "kernel_object_ranges_overlap":
        identifier = "obj01-overlap-" + hashlib.sha256((path + "/" + name).encode()).hexdigest()[:20]
        sites.append((
            identifier, "insert", "stack", "KERNEL_OBJECTS", "COMPARISON", 0, 0,
            "", (), 1,
        ))
    owner = "KERNEL_OBJECTS" if path.startswith("kernel/objects/") else "KERNEL_SCHEDULER"

    def add(mode, channel, kind, start, end):
        begin = positions[start].start
        finish = positions[end - 1].end
        regions = _original_regions(values, stacks, closings, stacks[start])
        identifier = "obj01-extra-" + hashlib.sha256(
            repr((path, name, mode, kind, start, end)).encode()
        ).hexdigest()[:20]
        sites.append((identifier, mode, channel, owner, kind, begin, finish, body[begin:finish], regions, 1))

    for index, value in enumerate(values):
        if value in ("ready_head", "ready_tail", "requests", "transitions") and index + 1 < len(values) and values[index + 1] == "[":
            member = index > 0 and values[index - 1] in (".", "->")
            if not member and _array_declaration(values, stacks, index):
                continue
            closing = _matching(values, index + 1, "[", "]")
            add("wrap", "aggregate", "INDEX_PROBE", index + 2, closing)
        if value == "cursor" and values[index:index + 2] == ("cursor", "="):
            semicolon = values.index(";", index)
            if "ready_next" in values[index:semicolon]:
                add("wrap", "aggregate", "LINK_TRAVERSE", index + 2, semicolon)
    for expression in CURRENT_COMPARISONS.get(name, ()):
        fragment = tuple(item.value for item in positional_tokens(expression))
        matches = [index for index in range(len(values)) if values[index:index + len(fragment)] == fragment]
        if len(matches) != 1:
            raise ValueError("OBJ01 geometric predicate is missing or duplicated: " + name)
        add("compare", "stack", "COMPARISON", matches[0], matches[0] + len(fragment))
    if len({site[0] for site in sites}) != len(sites):
        raise ValueError("OBJ01 primitive inventory contains duplicate identities")
    return tuple(sites)


def _primitive_statement(values, stacks, occurrence):
    from tests.host.selected_ownership_source import _original_statement

    while stacks[occurrence]:
        opening = stacks[occurrence][-1]
        prefix = values[max(0, opening - 4):opening]
        handles = ("micros_process_handle", "micros_thread_handle", "micros_hart_handle")
        literal = len(prefix) == 4 and prefix[:2] == ("(", "struct") and prefix[2] in handles and prefix[-1] == ")"
        initialized = (
            len(prefix) == 4 and prefix[0] == "struct" and prefix[1] in handles
            and re.fullmatch(r"[A-Za-z_]\w*", prefix[2]) is not None and prefix[3] == "="
        )
        if not literal and not initialized:
            break
        occurrence = opening
    return _original_statement(values, stacks, occurrence)


@lru_cache(maxsize=512)
def primitive_shape(path, name, body):
    positions = positional_tokens(body)
    values = tuple(item.value for item in positions)
    stacks, _ = compound_layout(values)
    result = []
    for site in sorted(body_sites(path, name, body), key=lambda row: (row[5], row[6], row[4])):
        _, mode, channel, owner, kind, begin, _, declared, regions, units = site
        if mode == "wrap" and kind != "LINK_TRAVERSE":
            occurrence = next(index for index, token in enumerate(positions) if token.start == begin) - 2
            first, last = _primitive_statement(values, stacks, occurrence)
            primitive = " ".join(values[first:last])
        elif declared:
            primitive = compact(declared)
        elif kind == "INDEX_PROBE":
            primitive = regions[-1][0]
        elif begin == 0:
            primitive = "function-entry"
        else:
            following = next((index for index, token in enumerate(positions) if token.start >= begin), len(values))
            first, last = _primitive_statement(values, stacks, following - 2)
            primitive = " ".join(values[first:last])
        result.append((mode, channel, owner, kind, primitive, regions, units))
    return tuple(result)


def observe_source(root, path, text, channel, stage):
    from tests.host.common_scheduler_return_source import legacy_observe_source

    observed = legacy_observe_source(root, "OBJ01", path, text, stage, channel)
    if observed is not None:
        return observed
    from tests.host.object_hart_validation_evidence import NATIVE_SOURCES

    if channel not in CHANNELS or path not in NATIVE_SOURCES or type(text) is not str:
        raise ValueError("OBJ01 observation has an unknown channel or subject")
    validate_retained_source(root, path, text, stage)
    if channel == "ordinary":
        return text
    if "MICROS_MEM05_OBSERVE_VALUE" in text or "MICROS_OBJ01_OBSERVE_VALUE" in text:
        raise ValueError("OBJ01 source already contains an observation wrapper")
    edits = []
    for name, span in function_spans(text).items():
        body = text[span.body_start:span.body_end]
        validate_observation_contract(root, path, name, body, stage)
        sites = body_sites(path, name, body)
        if channel != "aggregate":
            sites = tuple(row for row in sites if row[2] == channel)
        observed = _render_body(body, sites).replace(
            "MICROS_MEM05_OBSERVE_VALUE", "MICROS_OBJ01_OBSERVE_VALUE",
        )
        edits.append((span.body_start, span.body_end, observed))
    for first, last, observed in sorted(edits, reverse=True):
        text = text[:first] + observed + text[last:]
    return OBSERVATION_PREFIX + text


def observe_fixed_source(root, path, text, channel):
    return observe_source(root, path, text, channel, "baseline")


def project_observations(root, path, observed, channel, stage, *, original=None):
    from tests.host.evidence_authority_scope import source_bytes

    if original is None:
        original = fixed_source(root, path) if stage == "baseline" else source_bytes(Path(root), Path(root) / path).decode()
    if observed != observe_source(root, path, original, channel, stage):
        raise ValueError("OBJ01 passive source has a changed, moved or omitted primitive")
    return original


def project_fixed_observations(root, path, observed, channel):
    return project_observations(root, path, observed, channel, "baseline")


def _current_authority(root):
    from tests.host.evidence_authority_scope import identity_required

    return not identity_required(root, SOURCE_PROJECTION["retirement_keys"][0])


def native_assertion_admission(root, family, path, text, stage):
    from tests.host.common_scheduler_return_source import legacy_native_admission as common_admission

    admitted = common_admission(root, family, path, text, stage)
    if admitted:
        return admitted
    from tests.host.work_count_types import Stage

    owners = {
        "MEM01": "tests/host/memory_native_source_bindings.py",
        "MEM02": "tests/host/memory_native_source_bindings.py",
        "MEM03": "tests/host/selected_ownership_native_source_bindings.py",
        "MEM04": "tests/host/selected_address_space_native_source_bindings.py",
        "MEM05": "tests/host/selected_release_handoff_native_source_bindings.py",
    }
    if stage not in (Stage.RED, Stage.GREEN) or family not in owners:
        raise ValueError("OBJ01 legacy admission has an unknown family or source stage")
    scope = family + "/native-input/" + path
    key = owners[family] + ":native-source-admission:" + scope
    if stage is Stage.RED or key not in SOURCE_PROJECTION["retirement_keys"] or not _current_authority(root):
        return frozenset()
    validate_retained_source(root, path, text, "current")
    return frozenset((scope,))


def legacy_replaced_functions(root, family, path, text, stage):
    from tests.host.common_scheduler_return_source import legacy_replaced_functions as common_admission

    admitted = common_admission(root, family, path, text, stage)
    if admitted:
        return admitted
    from tests.host import selected_ownership_source as ownership
    from tests.host.work_count_types import Stage

    if family not in ("MEM01", "MEM02", "MEM03", "MEM04", "MEM05") or stage not in (Stage.RED, Stage.GREEN):
        raise ValueError("OBJ01 legacy observations have an unknown family or stage")
    if stage is Stage.RED or path not in MUTABLE_FUNCTIONS or not _current_authority(root):
        return frozenset()
    validate_retained_source(root, path, text, "current")
    result = set(CURRENT_HELPERS.get(path, ())) | set(REMOVED_FUNCTIONS.get(path, ()))
    if family == "MEM03" and path in ownership.OBSERVED_OBJECT_PATHS:
        prefix = (
            "tests/host/selected_ownership_observation_bindings.py:native-source-admission:"
            "MEM03/observation/" + path + "/"
        )
        result.update(key.removeprefix(prefix) for key in SOURCE_PROJECTION["retirement_keys"] if key.startswith(prefix))
    else:
        result.update(MUTABLE_FUNCTIONS[path])
    return frozenset(result)


LEGACY_VALUE_PREFIX = (
    '#include "micros/work_observation.h"\n'
    "#define MICROS_OBJ01_OBSERVE_VALUE(site, owner, kind, value) "
    "(micros_test_work_record((owner), (kind), 1), (value))\n"
)


def legacy_observe_source(root, family, path, text, stage, channel="aggregate"):
    from tests.host.common_scheduler_return_source import legacy_observe_source as common_admission

    observed = common_admission(root, family, path, text, stage, channel)
    if observed is not None:
        return observed
    from collections import Counter
    from tests.host import selected_release_handoff_source as previous
    from tests.host.work_count_types import Stage

    if family not in ("MEM01", "MEM02", "MEM03", "MEM04", "MEM05") or stage not in (Stage.RED, Stage.GREEN):
        raise ValueError("OBJ01 legacy observation has an unknown family or stage")
    backend = previous if family == "MEM05" else previous._legacy_backend(family)
    channels = (
        backend.STATE_WORK_CONTRACT["channels"] if family == "MEM03"
        else backend.CHANNELS if family in ("MEM04", "MEM05") else ("aggregate",)
    )
    if channel not in (*channels, "ordinary"):
        raise ValueError("OBJ01 legacy observation has an unknown channel")
    if stage is Stage.RED or family == "MEM01" or path not in MUTABLE_FUNCTIONS or not _current_authority(root):
        return None
    validate_retained_source(root, path, text, "current")
    if channel == "ordinary":
        return text
    replaced = legacy_replaced_functions(root, family, path, text, stage)
    records = function_records(text)
    retained = {name: [] for name in records}
    if family == "MEM05":
        original_sites = previous.baseline_observations(str(Path(root).resolve()), path)
        for name in records:
            if name not in replaced:
                retained[name].extend(
                    site for site in original_sites[name] if channel == "aggregate" or site[2] == channel
                )
    elif family != "MEM02":
        retained = previous._legacy_retained_sites(root, family, path, text, channel)
    original = function_records(fixed_source(root, path))
    for name, record in records.items():
        sites = sorted(body_sites(path, name, record.body), key=lambda row: (row[5], row[6], row[4]))
        if name in replaced:
            additions = sites
        elif name in MUTABLE_FUNCTIONS[path]:
            baseline_body = original[name].body
            if name == "kernel_object_validation_thread_slots_match":
                baseline_body = _current_shape(path, name, baseline_body)
            before = Counter(primitive_shape(path, name, baseline_body))
            additions = []
            for site, shape in zip(sites, primitive_shape(path, name, record.body), strict=True):
                if before[shape]:
                    before[shape] -= 1
                else:
                    additions.append(site)
            if any(before.values()):
                raise ValueError("OBJ01 changed an unretired legacy primitive instead of supplementing it")
        else:
            additions = ()
        if channel == "aggregate" or family in ("MEM04", "MEM05") and channel == "context":
            retained[name].extend(additions)
    spans = function_spans(text)
    for name in sorted(spans, key=lambda key: spans[key].body_start, reverse=True):
        span = spans[name]
        body = _render_body(text[span.body_start:span.body_end], retained[name]).replace(
            "MICROS_MEM05_OBSERVE_VALUE", "MICROS_OBJ01_OBSERVE_VALUE",
        )
        text = text[:span.body_start] + body + text[span.body_end:]
    prefix = backend.NATIVE_PREFIX if family == "MEM04" else backend.OBSERVATION_PREFIX
    return prefix + LEGACY_VALUE_PREFIX + text


def project_legacy_observations(root, family, path, text, stage, channel="aggregate"):
    from tests.host.common_scheduler_return_source import project_legacy_observations as common_admission

    observed = common_admission(root, family, path, text, stage, channel)
    if observed is not None:
        return observed
    from tests.host.evidence_authority_scope import source_bytes
    from tests.host.work_count_types import Stage

    if stage is Stage.RED or family == "MEM01" or path not in MUTABLE_FUNCTIONS or not _current_authority(root):
        return None
    original = source_bytes(Path(root), Path(root) / path).decode()
    if text != legacy_observe_source(root, family, path, original, stage, channel):
        raise ValueError("OBJ01 legacy observation changed or omitted retained/current primitive work")
    return original


def project_include_transports(root, logical, raw, transports, required):
    from tests.host.evidence_authority_scope import source_bytes
    from tests.host.kernel_layout_source import KERNEL_FILE_FACTS
    from tests.host.stack_source_projection import project_stack_source

    if type(required) is not bool:
        raise AssertionError("OBJ01 include transport requires checked source authority")
    path = KERNEL_FILE_FACTS[logical][0]
    if required or path not in (*MUTABLE_FUNCTIONS, "kernel/scheduler/scheduler_core_private.h") or not _current_authority(root):
        return transports
    actual = source_bytes(Path(root), Path(root) / path).decode()
    validate_retained_source(root, path, actual, "current")
    projected = project_stack_source(path, actual) if "MICROS_TEST_STACK_" in actual else actual
    if raw.decode() != projected:
        raise ValueError("OBJ01 include transport does not contain its actual qualified source")
    if path == "kernel/scheduler/scheduler_core.c":
        return (*transports, (
            2, "kernel/kernel_object_private.h",
            '#include "kernel/kernel_object_private.h"\n',
            '#include "kernel/objects/kernel_object_private.h"\n',
        ))
    return transports


def panic_hart_source_admitted(root, path, text):
    if path != "kernel/objects/kernel_object_hart.c" or not _current_authority(root):
        return False
    validate_retained_source(root, path, text, "current")
    return True


def validate_current_object_family(root):
    from tests.host.evidence_authority_scope import source_bytes

    if not _current_authority(root):
        raise ValueError("OBJ01 current object qualification lacks complete replacement authority")
    for path in SOURCE_ROLES["production_paths"]:
        if path.startswith("kernel/objects/"):
            validate_retained_source(root, path, source_bytes(Path(root), Path(root) / path).decode(), "current")


def object_phase_owners(root):
    if not _current_authority(root):
        return {}
    validate_current_object_family(root)
    return {
        name: "kernel/" + Path(path).name
        for path, declarations in CURRENT_HELPERS.items() if path.startswith("kernel/objects/")
        for name in declarations
    }


def object_validation_signatures(root, previous):
    if not _current_authority(root):
        return previous
    validate_current_object_family(root)
    removed = REMOVED_FUNCTIONS["kernel/objects/kernel_object_validation.c"]
    return {
        name: CURRENT_SIGNATURES[name].split("(", 1)[1][:-1] if name in CURRENT_SIGNATURES else parameters
        for name, parameters in previous.items() if name not in removed
    }


def object_inverse_fixture(root, current):
    from tests.host.kernel_layout_source import KERNEL_FILE_FACTS, _restored_include_bytes
    from tests.host.stack_source_projection import project_stack_source

    if not _current_authority(root):
        return current
    validate_current_object_family(root)
    fixed = {}
    for path in SOURCE_ROLES["production_paths"]:
        if not path.startswith("kernel/objects/") or not path.endswith(".c"):
            continue
        logical = "kernel/" + Path(path).name
        text = fixed_source(root, path)
        if "MICROS_TEST_STACK_" in text:
            text = project_stack_source(path, text)
        _, _, expected, transports = KERNEL_FILE_FACTS[logical]
        fixed[logical] = _restored_include_bytes(logical, text.encode(), expected, transports).decode()
    return fixed


def historical_identity_required(root, key):
    from tests.host.common_scheduler_return_source import historical_identity_required as common_required

    common = common_required(root, key)
    if common is not None:
        return common
    from tests.host.evidence_authority_scope import identity_required, source_bytes

    if identity_required(root, key):
        return True
    if key not in SOURCE_PROJECTION["retirement_keys"]:
        raise ValueError("OBJ01 cannot qualify an unrelated retired source identity")
    if key == "tests/host/test_kernel_object_source_layout.py:data-entry:STABLE_HASHES/kernel/scheduler_core_validate.c":
        path = "kernel/scheduler/scheduler_core_validate.c"
        validate_retained_source(root, path, source_bytes(Path(root), Path(root) / path).decode(), "current")
    elif "kernel_object" in key:
        validate_current_object_family(root)
    else:
        for path in SOURCE_ROLES["production_paths"]:
            if path.startswith("kernel/scheduler/"):
                validate_retained_source(root, path, source_bytes(Path(root), Path(root) / path).decode(), "current")
    return False


def object_failure_is_propagated(body, call, success):
    from tests.host.selected_address_space_source import _matching

    if success not in ("true", "MICROS_KERNEL_OBJECT_OK"):
        return False
    values = tuple(token.value for token in positional_tokens(body))
    invariant = ("return", "MICROS_KERNEL_OBJECT_ERROR_INVARIANT", ";")
    for index in range(len(values) - 2):
        if values[index:index + 2] != ("if", "("):
            continue
        closing = _matching(values, index + 1, "(", ")")
        if values[closing + 1:closing + 2] != ("{",):
            continue
        end = _matching(values, closing + 1, "{", "}")
        returned = values[closing + 2:end]
        condition = values[index + 2:closing]
        terms, start, depth = [], 0, 0
        for offset, token in enumerate(condition):
            if token in ("(", "[", "{"):
                depth += 1
            elif token in (")", "]", "}"):
                depth -= 1
            elif token == "||" and depth == 0:
                terms.append(condition[start:offset])
                start = offset + 1
        terms.append(condition[start:])
        for term_index, term in enumerate(terms):
            while term and term[0] == "(" and _matching(term, 0, "(", ")") == len(term) - 1:
                term = term[1:-1]
            if success == "true":
                if (
                    returned == invariant and term[:3] == ("!", call, "(")
                    and _matching(term, 2, "(", ")") == len(term) - 1
                ):
                    return True
                continue
            if (
                term_index != 0 or len(term) != 3 or term[1:] != ("!=", success)
                or returned not in (invariant, ("return", term[0], ";"))
            ):
                continue
            for position in range(2, index):
                if values[position - 2:position + 2] != (term[0], "=", call, "("):
                    continue
                if position > 2 and values[position - 3] not in (
                    ";", "{", "}", "micros_kernel_object_error",
                ):
                    continue
                call_end = _matching(values, position + 1, "(", ")")
                if call_end + 2 == index and values[call_end + 1] == ";":
                    return True
    return False


def _object_gate_edits():
    edits = [
        (
            "def original_locations(family, texts=None):\n",
            "def original_locations(family, texts=None):\n"
            "    from tests.host.object_hart_validation_source import object_phase_owners\n\n",
        ),
        (
            "    stack_owners.update({name: CORE_SOURCES[8] for name in BASE_VALIDATION_SIGNATURES})\n",
            "    stack_owners.update({name: CORE_SOURCES[8] for name in BASE_VALIDATION_SIGNATURES})\n"
            "    if family == 0:\n        stack_owners.update(object_phase_owners(ROOT))\n",
        ),
        (
            "                    self.assertEqual(expected, original_body_digest(family, name, record), REVIEWED_BASE)\n",
            '                    key = "tests/host/test_kernel_object_source_layout.py:data-entry:BASELINE_BODIES/" + str(family) + "/" + name\n'
            "                    if historical_identity_required(ROOT, key):\n"
            "                        self.assertEqual(expected, original_body_digest(family, name, record), REVIEWED_BASE)\n",
        ),
        (
            "        final.update({name: CORE_SOURCES[8] for name in BASE_VALIDATION_SIGNATURES})\n",
            "        final.update({name: CORE_SOURCES[8] for name in BASE_VALIDATION_SIGNATURES})\n"
            "        final.update(object_phase_owners(ROOT))\n",
        ),
        (
            "        for name, parameters in BASE_VALIDATION_SIGNATURES.items():\n",
            "        for name, parameters in object_validation_signatures(ROOT, BASE_VALIDATION_SIGNATURES).items():\n",
        ),
        (
            "                if not diagnostic_identity_required(\n"
            '                    ROOT, "tests/host/test_kernel_object_source_layout.py:data-entry:STABLE_HASHES/" + path,\n',
            '                key = "tests/host/test_kernel_object_source_layout.py:data-entry:STABLE_HASHES/" + path\n'
            '                if path == "kernel/scheduler_core_validate.c" and not historical_identity_required(ROOT, key):\n'
            "                    continue\n"
            "                if not diagnostic_identity_required(\n"
            '                    ROOT, "tests/host/test_kernel_object_source_layout.py:data-entry:STABLE_HASHES/" + path,\n',
        ),
    ]
    for method, helper in (
        ("test_complete_original_bodies_and_all_parameter_types_remain_exact", "historical_identity_required"),
        ("test_portable_private_helpers_are_necessary_and_dependencies_stay_acyclic", "object_phase_owners"),
        ("test_base_validation_exposes_typed_passes_and_original_caller_witnesses", "object_validation_signatures"),
        ("test_public_runtime_architecture_driver_and_neighbor_artifacts_are_byte_exact", "historical_identity_required"),
    ):
        header = "    def " + method + "(self):\n"
        edits.append((header, header + "        from tests.host.object_hart_validation_source import " + helper + "\n\n"))
    for method, current in (
        ("test_portable_complete_byte_inverse_is_independent_and_rejects_changed_code",
         "        current = {path: source(path) for path in CORE_SOURCES if kernel_layout_source.is_file_path(ROOT / path)}\n"),
        ("test_actual_base_inverse_rejects_changed_types_predicates_witnesses_and_peer_order",
         "        current = {path: source(path) for path in CORE_SOURCES}\n"),
    ):
        header = "    def " + method + "(self):\n"
        edits.append((
            header + current,
            header + "        from tests.host.object_hart_validation_source import object_inverse_fixture\n\n"
            + current + "        current = object_inverse_fixture(ROOT, current)\n",
        ))
    return tuple(edits)


def _scheduler_gate_edits():
    edits = [
        (
            "    def test_all_original_owners_and_pending_bodies_are_frozen(self) -> None:\n"
            "        from tests.host.evidence_authority_scope import identity_required\n",
            "    def test_all_original_owners_and_pending_bodies_are_frozen(self) -> None:\n"
            "        from tests.host.evidence_authority_scope import identity_required\n"
            "        from tests.host.object_hart_validation_source import historical_identity_required\n",
        ),
        (
            "                        expected = original if (original, name) in pending else final\n",
            "                        expected = original if (original, name) in pending else final\n"
            '                        key = "tests/host/test_scheduler_source_layout.py:function-body:FROZEN_FUNCTIONS/" + original + "/" + name\n'
            '                        if family == 0 and name == "queue_enqueue_tail" and not historical_identity_required(REPOSITORY_ROOT, key):\n'
            "                            self.assertEqual([], matches)\n"
            "                            self.assertNotRegex(\n"
            '                                self.texts[PRIVATE_HEADERS[0]], r"\\bscheduler_core_queue_enqueue_tail\\s*\\(",\n'
            "                            )\n"
            "                            continue\n",
        ),
        (
            "        for path, expected in STABLE_ARTIFACT_HASHES.items():\n"
            "            with self.subTest(path=path):\n",
            "        for path, expected in STABLE_ARTIFACT_HASHES.items():\n"
            "            with self.subTest(path=path):\n"
            '                key = "tests/host/test_scheduler_source_layout.py:data-entry:STABLE_ARTIFACT_HASHES/" + path\n'
            '                if path == "kernel/kernel_objects.c" and not historical_identity_required(REPOSITORY_ROOT, key):\n'
            "                    continue\n",
        ),
        (
            "        for (original, name), expected in FROZEN_FACTS.items():\n",
            "        for (original, name), expected in FROZEN_FACTS.items():\n"
            '            key = "tests/host/test_scheduler_source_layout.py:data-table:FROZEN_FACTS/" + original + "/" + name\n'
            "            if not historical_identity_required(REPOSITORY_ROOT, key):\n                continue\n",
        ),
        (
            "                for role in PHASE_ROLES[name]:\n",
            "                roles = PHASE_ROLES[name]\n"
            '                key = "tests/host/test_scheduler_source_layout.py:data-table:FROZEN_FACTS/" + original + "/" + name\n'
            "                current_object_phase = (\n"
            '                    family == 0 and name in ("micros_scheduler_commit_ipc_transitions", "micros_scheduler_core_validate", "micros_hart_plan_user_return")\n'
            "                    and not historical_identity_required(REPOSITORY_ROOT, key)\n"
            "                )\n"
            '                if current_object_phase and name == "micros_scheduler_commit_ipc_transitions":\n'
            '                    roles = (r"stamp", r"registry", r"preflight|candidate", r"commit|apply")\n'
            "                for role in roles:\n",
        ),
        (
            "                            require_error_propagation(normalized_body(phase, family, records), called, success)\n",
            "                            body = normalized_body(phase, family, records)\n"
            "                            if (\n"
            "                                current_object_phase\n"
            '                                and re.fullmatch(r"(?:static )?enum micros_kernel_object_error", signature_shape(phase.signature, phase.name)[0])\n'
            "                                and object_failure_is_propagated(body, called, success)\n"
            "                            ):\n"
            "                                continue\n"
            "                            require_error_propagation(body, called, success)\n",
        ),
        (
            '        require_all_record_preflight(self.phases(0, "micros_scheduler_commit_ipc_transitions"))\n',
            '        prefix = "tests/host/test_scheduler_source_layout.py:data-table:FROZEN_FACTS/kernel/scheduler_core.c/"\n'
            '        if historical_identity_required(REPOSITORY_ROOT, prefix + "micros_scheduler_commit_ipc_transitions"):\n'
            '            require_all_record_preflight(self.phases(0, "micros_scheduler_commit_ipc_transitions"))\n'
            "        else:\n"
            "            require_order(\n"
            '                self.original(0, "micros_scheduler_commit_ipc_transitions"),\n'
            '                "preflight_transition_stamps", "scheduler_core_preflight_registry",\n'
            '                "preflight_transition_candidates", "commit_transition_candidates",\n'
            "            )\n",
        ),
        (
            '            "micros_scheduler_core_validate", "scratch_load",\n',
            '            "micros_scheduler_core_validate" if historical_identity_required(\n'
            '                REPOSITORY_ROOT, prefix + "micros_hart_plan_user_return",\n'
            '            ) else "preflight_return_plan",\n'
            '            "scratch_load",\n',
        ),
    ]
    for method in (
        "test_stable_artifacts_are_literal_reviewed_base_hashes",
        "test_functions_stay_bounded_and_discharge_real_named_work",
        "test_all_phase_source_facts_are_preserved_not_summarized",
        "test_minimal_accounting_and_nonrecursive_validation",
    ):
        header = "    def " + method + "(self) -> None:\n"
        helper = "historical_identity_required"
        if method == "test_functions_stay_bounded_and_discharge_real_named_work":
            helper += ", object_failure_is_propagated"
        edits.append((header, header + "        from tests.host.object_hart_validation_source import " + helper + "\n\n"))
    return tuple(edits)


def _legacy_changes(path):
    if path == "tests/host/test_kernel_object_source_layout.py":
        return _object_gate_edits()
    if path == "tests/host/test_scheduler_source_layout.py":
        return _scheduler_gate_edits()
    if path == "tests/host/test_endpoint_source_layout.py":
        header = "    def test_public_runtime_driver_and_neighbor_artifacts_remain_byte_exact(self):\n"
        anchor = "                from tests.host.test_qemu_runner_source_layout import restored_runner_source\n\n"
        return (
            (header, header + "        from tests.host.object_hart_validation_source import historical_identity_required\n\n"),
            (
                anchor,
                anchor + '                key = "tests/host/test_endpoint_source_layout.py:data-entry:STABLE_HASHES/" + path\n'
                '                if path == "kernel/kernel_objects.c" and not historical_identity_required(ROOT, key):\n'
                "                    continue\n",
            ),
        )
    edits = []
    if path == "tests/host/selected_ownership_source.py":
        edits.extend((
            (
                '\n    if "tests.host.selected_address_space_source" in text:\n'
                "        text = project_next(Path(__file__).resolve().parents[2], path, text)\n",
                "\n    if (\n"
                '        "tests.host.selected_address_space_source" in text\n'
                '        or "tests.host.object_hart_validation_source" in text\n'
                "    ):\n"
                "        text = project_next(Path(__file__).resolve().parents[2], path, text)\n",
            ),
            (
                "    from tests.host.selected_release_handoff_source import native_recipe as selected_release_handoff_recipe\n",
                "    from tests.host.selected_release_handoff_source import native_recipe as selected_release_handoff_recipe\n"
                "    from tests.host.object_hart_validation_source import native_recipe as object_hart_recipe\n",
            ),
            (
                "    return text.replace(anchor, native_recipe() + selected_address_space_recipe() + selected_release_handoff_recipe() + anchor, 1)\n",
                "    return text.replace(anchor, native_recipe() + selected_address_space_recipe() + selected_release_handoff_recipe() + object_hart_recipe() + anchor, 1)\n",
            ),
        ))
    elif path == "tests/host/selected_release_handoff_source.py":
        for name, imported, call in (
            ("native_assertion_admission", "native_assertion_admission", "root, family, path, text, stage"),
            ("legacy_replaced_functions", "legacy_replaced_functions", "root, family, path, text, stage"),
            ("legacy_observe_source", "legacy_observe_source", "root, family, path, text, stage, channel"),
            ("project_legacy_observations", "project_legacy_observations", "root, family, path, text, stage, channel"),
        ):
            arguments = "root, family, path, text, stage" + (
                ', channel="aggregate"' if name in ("legacy_observe_source", "project_legacy_observations") else ""
            )
            header = "def " + name + "(" + arguments + "):\n"
            optional = name in ("legacy_observe_source", "project_legacy_observations")
            edits.append((header, header + (
                "    from tests.host.object_hart_validation_source import " + imported + " as next_admission\n\n"
                "    admitted = next_admission(" + call + ")\n"
                "    if admitted" + (" is not None" if optional else "") + ":\n"
                "        return admitted\n"
            )))
        for name, arguments, imported, call in (
            ("validate_retained_source", "root, path, text, stage",
             "native_assertion_admission", 'root, "MEM05", path, text, stage'),
            ("observe_source", "root, path, text, channel, stage",
             "legacy_observe_source", 'root, "MEM05", path, text, stage, channel'),
        ):
            header = "def " + name + "(" + arguments + "):\n"
            observed = name == "observe_source"
            edits.append((header, header + (
                "    from tests.host.object_hart_validation_source import " + imported + " as next_admission\n\n"
                "    admitted = next_admission(" + call + ")\n"
                "    if admitted" + (" is not None" if observed else "") + ":\n"
                "        return" + (" admitted" if observed else "") + "\n"
            )))
        for name, arguments, variable, call in (
            ("project_include_transports", "root, logical, raw, transports, required", "transports",
             "root, logical, raw, transports, required"),
            ("project_legacy_adapter", "root, path, text", "text", "root, path, text"),
            ("project_cmake", "text", "text", "text"),
        ):
            header = "def " + name + "(" + arguments + "):\n"
            edits.append((header, header + (
                "    from tests.host.object_hart_validation_source import " + name + " as project_next\n\n"
                "    " + variable + " = project_next(" + call + ")\n"
            )))
    elif path == "tools/generate_selected_release_handoff_fixture.py":
        before = (
            '            if identity["mode"] != mode or path not in source.SOURCE_ROLES["production_paths"] and raw != original:\n'
            '                raise ValueError("MEM05 changed an unadmitted native input or file mode: " + path)\n'
        )
        after = (
            '            if identity["mode"] != mode:\n'
            '                raise ValueError("MEM05 changed an unadmitted native input or file mode: " + path)\n'
            '            if path not in source.SOURCE_ROLES["production_paths"] and raw != original:\n'
            '                from tests.host.object_hart_validation_source import native_assertion_admission\n\n'
            '                if "MEM05/native-input/" + path not in native_assertion_admission(\n'
            '                    root, "MEM05", path, raw.decode(), stage,\n'
            '                ):\n'
            '                    raise ValueError("MEM05 changed an unadmitted native input or file mode: " + path)\n'
        )
        edits.append((before, after))
    elif path == "tools/generate_kernel_mapping_fixture.py":
        before = '        if name == "kernel/memory/bootstrap_memory.c":\n'
        after = (
            '        if observed:\n'
            '            from tests.host.object_hart_validation_source import legacy_observe_source, project_legacy_observations\n\n'
            '            current = legacy_observe_source(root, "MEM02", name, text, stage)\n'
            '            if current is not None:\n'
            '                if project_legacy_observations(root, "MEM02", name, current, stage) != text:\n'
            '                    raise ValueError("MEM02 current object observations changed their qualified source")\n'
            '                text = current\n'
        ) + before
        edits.append((before, after))
    elif path == "tools/stack_bound_fatal.py":
        edits.extend((
            (
                "def trace_preservation(root, artifacts, protected):\n",
                "def trace_preservation(root, artifacts, protected):\n"
                "    from tests.host.object_hart_validation_source import panic_hart_source_admitted\n\n",
            ),
            (
                '        if source != trace_reference_source(unit):\n'
                '            raise StackEvidenceError("trace selected-owner provider changed its complete fixed predecessor source")\n',
                '        if source != trace_reference_source(unit):\n'
                '            try:\n'
                '                admitted = panic_hart_source_admitted(root, unit, source)\n'
                '            except ValueError as error:\n'
                '                raise StackEvidenceError(str(error)) from error\n'
                '            if not admitted:\n'
                '                raise StackEvidenceError("trace selected-owner provider changed its complete fixed predecessor source")\n',
            ),
            (
                "def trace_fault_artifacts(root, directory, catalog, artifacts):\n",
                "def trace_fault_artifacts(root, directory, catalog, artifacts):\n"
                "    from tests.host.object_hart_validation_source import panic_hart_source_admitted\n\n",
            ),
            (
                '            prior = project_trace_runtime(unit, source)\n',
                '            prior = project_trace_runtime(unit, source)\n'
                '            source_matches = prior == trace_reference_source(unit) or panic_hart_source_admitted(root, unit, source)\n',
            ),
            (
                '            prior != trace_reference_source(unit) or origin.source != unit or origin.line != definition_line(source, name)\n',
                '            not source_matches or origin.source != unit or origin.line != definition_line(source, name)\n',
            ),
        ))
    return tuple(edits)


@lru_cache(maxsize=32)
def baseline_adapter_source(root, path):
    if path not in (*SOURCE_ROLES["fixed_adapters"], *SOURCE_ROLES["immutable_adapters"], *SOURCE_ROLES["unchanged_candidates"]):
        raise ValueError("OBJ01 has no independently declared adapter owner")
    return subprocess.check_output(
        ("git", "show", SOURCE_PROJECTION["adapter_reference"] + ":" + path),
        cwd=root, text=True, timeout=30,
    )


def extend_legacy_adapter(root, path, text):
    if text != baseline_adapter_source(str(Path(root).resolve()), path):
        raise ValueError("OBJ01 adapter lacks its complete fixed predecessor")
    for before, after in _legacy_changes(path):
        if text.count(before) != 1 or after in text:
            raise ValueError("OBJ01 adapter lost its exact original clause: " + path)
        text = text.replace(before, after, 1)
    return text


def project_legacy_adapter(root, path, text):
    from tests.host.common_scheduler_return_source import project_legacy_adapter as project_common

    text = project_common(root, path, text)
    if path not in SOURCE_ROLES["fixed_adapters"] or "tests.host.object_hart_validation_source" not in text:
        return text
    original = baseline_adapter_source(str(Path(root).resolve()), path)
    if text != extend_legacy_adapter(root, path, original):
        raise ValueError("OBJ01 adapter changed beyond its complete frozen admission")
    return original


def native_products():
    return (
        ("baseline", "ordinary"), ("baseline", "aggregate"), ("baseline", "stack"),
        ("current", "aggregate"), ("current", "stack"), ("current", "ordinary"),
    )


def _native_target_recipe(index, stage, channel, target):
    observed = channel != "ordinary"
    root = "${OBJ01_NATIVE_ROOT}/" + stage + "/" + channel
    result = (
        "    add_executable(" + target + " ${OBJ01_SOURCES_" + str(index) + "}\n"
        "        tests/host/object_hart_validation_test.c\n"
        + ("        tests/work/work_counts.c\n" if observed else "")
        + "    )\n"
        "    target_include_directories(" + target + " PRIVATE\n"
        '        "' + root + '" "' + root + '/include"\n'
        '        "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/include")\n'
        "    target_compile_features(" + target + " PRIVATE c_std_17)\n"
        "    target_compile_options(" + target
        + " PRIVATE -Wall -Wextra -Werror -Wpedantic -Wmissing-prototypes -Wstrict-prototypes -fno-builtin)\n"
        "    target_compile_definitions(" + target + " PRIVATE MICROS_OBJ01_NATIVE_STAGE=" + str(int(stage == "current")) + ")\n"
        '    if(MICROS_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")\n'
        "        target_compile_options(" + target + " PRIVATE -fno-omit-frame-pointer -fsanitize=address,undefined)\n"
        "        target_link_options(" + target + " PRIVATE -fsanitize=address,undefined)\n"
        "    endif()\n"
    )
    if observed:
        result += (
            "    target_compile_definitions(" + target + " PRIVATE MICROS_TEST_WORK_COUNTS=1 "
            "MICROS_OBJ01_NATIVE_CHANNEL=" + str(("aggregate", "stack").index(channel)) + ")\n"
        )
    elif stage == "current":
        result += (
            "    target_compile_definitions(" + target + " PRIVATE\n"
            "        MICROS_OBJECT_HART_VALIDATION_LAUNCHER=1\n"
            '        "MICROS_OBJECT_HART_VALIDATION_PYTHON=\\"${MICROS_PYTHON}\\""\n'
            '        "MICROS_OBJECT_HART_VALIDATION_DRIVER=\\"${CMAKE_CURRENT_SOURCE_DIR}/tools/check_object_hart_validation.py\\""\n'
            '        "MICROS_OBJECT_HART_VALIDATION_ROOT=\\"${CMAKE_CURRENT_SOURCE_DIR}\\""\n'
            "    )\n"
        )
    return result


def native_recipe():
    from tests.host.object_hart_validation_evidence import NATIVE_HEADERS, NATIVE_SOURCES

    result = "".join(
        "    add_custom_command(\n"
        "        OUTPUT ${" + owner + "_NATIVE_OUTPUTS} APPEND\n"
        "        DEPENDS tests/host/object_hart_validation_source.py\n"
        "            tests/host/object_hart_validation_authority.py\n"
        "            tests/host/object_hart_native_source_bindings.py\n"
        "            tests/host/evidence_authority_scope.py\n"
        "            tools/validation_context.py\n"
        "            tools/validation_inputs.py\n"
        "    )\n"
        for owner in ("MEM01", "MEM02", "MEM03", "MEM04", "MEM05")
    )
    result += (
        '    set(OBJ01_NATIVE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/object-hart-validation")\n'
        "    set(OBJ01_NATIVE_INPUTS\n"
        + "".join("        " + path + "\n" for path in NATIVE_SOURCES)
        + "    )\n"
        "    set(OBJ01_NATIVE_HEADERS\n"
        + "".join("        " + path + "\n" for path in NATIVE_HEADERS)
        + "    )\n"
        "    set(OBJ01_NATIVE_OUTPUTS)\n"
    )
    for index, (stage, channel) in enumerate(native_products()):
        root = "${OBJ01_NATIVE_ROOT}/" + stage + "/" + channel
        result += (
            "    set(OBJ01_SOURCES_" + str(index) + ")\n"
            "    foreach(obj01_source IN LISTS OBJ01_NATIVE_INPUTS)\n"
            "        list(APPEND OBJ01_SOURCES_" + str(index) + ' "' + root + '/${obj01_source}")\n'
            "    endforeach()\n"
            "    list(APPEND OBJ01_NATIVE_OUTPUTS ${OBJ01_SOURCES_" + str(index) + "})\n"
            "    foreach(obj01_header IN LISTS OBJ01_NATIVE_HEADERS)\n"
            '        list(APPEND OBJ01_NATIVE_OUTPUTS "' + root + '/${obj01_header}")\n'
            "    endforeach()\n"
        )
    result += (
        "    add_custom_command(\n"
        "        OUTPUT ${OBJ01_NATIVE_OUTPUTS}\n"
        "        JOB_POOL mem03_source_proof\n"
        '        COMMAND "${MICROS_PYTHON}" ${MICROS_PYTHON_ARGS}\n'
        '            "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_object_hart_validation_fixture.py"\n'
        "        DEPENDS ${OBJ01_NATIVE_INPUTS} ${OBJ01_NATIVE_HEADERS}\n"
        + "".join("            " + path + "\n" for path in SOURCE_ROLES["immutable_additions"])
        + "            tests/host/evidence_authority_scope.py\n"
        "            tests/host/selected_release_handoff_source.py\n"
        "            tests/host/selected_ownership_source.py\n"
        "            tests/host/selected_address_space_source.py\n"
        "            tests/host/c_source_records.py\n"
        "            tests/host/work_count_lexical.py\n"
        "            tools/validation_context.py\n"
        "            tools/validation_inputs.py\n"
        '        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"\n'
        "        VERBATIM\n"
        "    )\n"
    )
    for index, ((stage, channel), target) in enumerate(zip(native_products(), RECIPE_ADMISSION["native_targets"], strict=True)):
        result += _native_target_recipe(index, stage, channel, target)
    return result + (
        "    add_test(NAME object-hart-validation COMMAND object_hart_current_ordinary_test\n"
        + "".join('        "$<TARGET_FILE:' + target + '>"\n' for target in RECIPE_ADMISSION["native_targets"][:-1])
        + "    )\n"
        "    set_tests_properties(object-hart-validation PROPERTIES LABELS slow)\n"
        "    add_dependencies(test-unit " + " ".join(RECIPE_ADMISSION["native_targets"]) + ")\n\n"
    )


def extend_cmake(text):
    anchor = '    include("${CMAKE_CURRENT_SOURCE_DIR}/tests/stack/StackEvidence.cmake")\n'
    if hashlib.sha256(text.encode()).hexdigest() != RECIPE_ADMISSION["base_cmake_sha256"] or text.count(anchor) != 1:
        raise ValueError("OBJ01 native recipe lacks its complete fixed predecessor")
    if "    set_property(GLOBAL APPEND PROPERTY JOB_POOLS mem03_source_proof=1)\n" not in text:
        raise ValueError("OBJ01 requires the existing depth-one source-proof pool")
    return text.replace(anchor, native_recipe() + anchor, 1)


def project_cmake(text):
    from tests.host.common_scheduler_return_source import project_cmake as project_common

    text = project_common(text)
    if not any(marker in text for marker in ("OBJ01_NATIVE", "object_hart_", "object-hart-validation")):
        return text
    addition = native_recipe()
    anchor = '    include("${CMAKE_CURRENT_SOURCE_DIR}/tests/stack/StackEvidence.cmake")\n'
    if text.count(addition + anchor) != 1:
        raise ValueError("OBJ01 native recipe is changed, partial or relocated")
    original = text.replace(addition, "", 1)
    if hashlib.sha256(original.encode()).hexdigest() not in (
        RECIPE_ADMISSION["base_cmake_sha256"], RECIPE_ADMISSION["legacy_cmake_sha256"],
    ):
        raise ValueError("OBJ01 native recipe changed unrelated predecessor bytes")
    return original


def validate_scaffold(root):
    from tests.host.object_hart_validation_evidence import expected_cases, NATIVE_HEADERS, NATIVE_SOURCES
    from tools.stack_artifact_files import read_regular

    root = Path(root).resolve()
    values = read_declarations(root)
    if any(values[name] != globals()[name] for name in values if name not in ("DESIGN_PINS", "ORACLE_RULES")):
        raise ValueError("OBJ01 loaded declarations differ from their current literal source")
    if (
        len(NATIVE_SOURCES) != 18 or len(NATIVE_HEADERS) != 8
        or digest(current_function_contracts(str(root))) != SOURCE_PROJECTION["current_function_contracts_sha256"]
        or len(current_function_contracts(str(root))) != 53
    ):
        raise ValueError("OBJ01 native function or source closure differs from its independent declaration")
    for stage in ("baseline", "current"):
        cases = expected_cases(stage)
        if len(cases) != STATE_WORK_CONTRACT["cases"] or digest(cases) != STATE_WORK_CONTRACT[stage + "_cases_sha256"]:
            raise ValueError("OBJ01 independent semantic corpus changed")
    native_binding_facts(root, NATIVE_FAMILY, BINDINGS_PATH)
    original = baseline_adapter_source(str(root), "CMakeLists.txt")
    if read_regular(root / "CMakeLists.txt")[0].decode() != extend_cmake(original):
        raise ValueError("OBJ01 exact full-only native recipe is absent or altered")
    expected_adapters = SOURCE_PROJECTION.get("adapter_sha256")
    if type(expected_adapters) is not dict or set(expected_adapters) != set((
        *SOURCE_ROLES["fixed_adapters"], *SOURCE_ROLES["unchanged_candidates"],
    )):
        raise ValueError("OBJ01 exact pre-Red candidate adapters have not been independently frozen")
    for path in SOURCE_ROLES["fixed_adapters"]:
        actual = read_regular(root / path)[0]
        if (
            actual.decode() != extend_legacy_adapter(root, path, baseline_adapter_source(str(root), path))
            or hashlib.sha256(actual).hexdigest() != expected_adapters[path]
        ):
            raise ValueError("OBJ01 frozen candidate adapter changed: " + path)
    for path in SOURCE_ROLES["unchanged_candidates"]:
        actual = read_regular(root / path)[0]
        if (
            actual.decode() != baseline_adapter_source(str(root), path)
            or hashlib.sha256(actual).hexdigest() != expected_adapters[path]
        ):
            raise ValueError("OBJ01 changed an original literal binding: " + path)
    machine = SOURCE_PROJECTION.get("stack_admission_sha256")
    if type(machine) is not dict or set(machine) != {"tools/stack_bound_fatal_effects.py", "tests/host/test_stack_bound_evidence.py"}:
        raise ValueError("OBJ01 exact machine qualification inputs have not been frozen")
    for path, expected in machine.items():
        if hashlib.sha256(read_regular(root / path)[0]).hexdigest() != expected:
            raise ValueError("OBJ01 frozen stack qualification changed: " + path)
    for path in SOURCE_ROLES["immutable_additions"]:
        read_regular(root / path)
    validate_retained_source(root, "kernel/scheduler/scheduler_core_private.h", declared_header(root), "current")

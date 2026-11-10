"""Independently declared OBJ02 source inputs and passive observations."""

import hashlib
from functools import lru_cache
from pathlib import Path
import re
import subprocess

from tests.host.object_hart_validation_evidence import NATIVE_HEADERS, NATIVE_SOURCES
from tests.host.c_source_records import compact, function_records
from tests.host.work_count_lexical import compound_layout, function_spans, positional_tokens


SOURCE_PATH = "tests/host/common_scheduler_return_source.py"
AUTHORITY_PATH = "tests/host/common_scheduler_return_authority.py"
BINDINGS_PATH = "tests/host/common_scheduler_return_native_source_bindings.py"
FAMILY = "common-scheduler-return-baseline"
NATIVE_FAMILY = "common-scheduler-return-native-admission"
SOURCE_ROLES = {
    "base_membership_sha256": "1b7023162ed02ad1a6557ee9ec3c4d44a6f3591a0afa5631dc7d61d3f1ca76bb",
    "production_paths": (
        "include/micros/scheduler_core.h",
        "kernel/scheduler/scheduler.c",
        "kernel/scheduler/scheduler_context.c",
        "kernel/scheduler/scheduler_core_return.c",
        "kernel/scheduler/scheduler_core_return_plan.c",
        "kernel/scheduler/scheduler_idle.c",
        "kernel/scheduler/scheduler_return.c",
        "kernel/scheduler/scheduler_runtime_internal.h",
    ),
    "fixed_adapters": (
        "kernel/tests/scheduler/scheduler_test_finish.c",
        "tests/host/kernel_layout_source.py",
        "tests/host/kernel_mapping_source.py",
        "tests/host/object_hart_validation_source.py",
        "tests/host/selected_address_space_source.py",
        "tests/host/selected_ownership_source.py",
        "tests/host/selected_release_handoff_source.py",
        "tests/host/test_kernel_object_source_layout.py",
        "tests/host/test_scheduler_source_layout.py",
        "tools/generate_kernel_mapping_fixture.py",
        "tools/generate_selected_ownership_fixture.py",
        "tools/generate_selected_release_handoff_fixture.py",
    ),
    "unchanged_candidates": (
        "kernel/scheduler/scheduler_completion.c",
        "kernel/scheduler/scheduler_interrupt.c",
        "kernel/tests/scheduler/scheduler_test_support.c",
        "tests/host/memory_native_source_bindings.py",
        "tests/host/object_hart_native_source_bindings.py",
        "tests/host/selected_address_space_native_source_bindings.py",
        "tests/host/selected_ownership_native_source_bindings.py",
        "tests/host/selected_release_handoff_native_source_bindings.py",
    ),
    "immutable_additions": (
        "tests/host/common_scheduler_return_test.c",
        "tests/host/common_scheduler_return_test.h",
        "tests/host/common_scheduler_return_source.py",
        "tests/host/common_scheduler_return_evidence.py",
        "tests/host/common_scheduler_return_authority.py",
        "tests/host/common_scheduler_return_native_source_bindings.py",
        "tests/host/test_common_scheduler_return.py",
        "tests/qemu/common_scheduler_return_work.c",
        "tests/qemu/common_scheduler_return_work.h",
        "tools/generate_common_scheduler_return_fixture.py",
        "tools/check_common_scheduler_return.py",
    ),
    "immutable_adapters": (
        "CMakeLists.txt",
        "tests/host/evidence_authority_inventory.py",
        "tests/host/evidence_authority_scope.py",
        "tests/host/test_evidence_authority.py",
        "tests/host/test_evidence_authority_scope.py",
        "tests/host/test_python_host_runtime.py",
        "tests/host/object_hart_validation_evidence.py",
        "tests/host/object_hart_validation_test.c",
        "tests/host/test_object_hart_validation.py",
        "tests/host/test_selected_ownership.py",
        "tests/host/test_qemu_transcript.py",
        "tests/host/test_selected_release_handoff.py",
        "tools/generate_object_hart_validation_fixture.py",
    ),
}
SOURCE_PROJECTION = {'predecessor_facts_sha256': '026081f993bb3d79d88337ceff3cc083a04668f7ddf075b58ea12e6b17d16ce1',
 'affected_facts_sha256': '7e80ec298823df18ec39c2805e7cc2a9035edae211778c4db4761b370e836fc2',
 'retirement_keys': ('tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/scheduler.c/2',
                     'tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/scheduler_core_return.c/2',
                     'tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/scheduler_runtime_internal.h/2',
                     'tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/scheduler_test_finish.c/2',
                     'tests/host/memory_native_source_bindings.py:native-source-admission:MEM02/native-input/include/micros/scheduler_core.h',
                     'tests/host/memory_native_source_bindings.py:native-source-admission:MEM02/native-input/kernel/scheduler/scheduler_core_return.c',
                     'tests/host/memory_native_source_bindings.py:native-source-admission:MEM02/native-input/kernel/scheduler/scheduler_runtime_internal.h',
                     'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/current-input/include/micros/scheduler_core.h',
                     'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/current-input/kernel/scheduler/scheduler_core_return.c',
                     'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/current-observation/kernel/scheduler/scheduler_core_return.c/micros_hart_commit_user_return',
                     'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/current-observation/kernel/scheduler/scheduler_core_return.c/micros_scheduler_apply_return_plan',
                     'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/current-observation/kernel/scheduler/scheduler_core_return.c/plans_equal',
                     'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/current-observation/kernel/scheduler/scheduler_core_return_plan.c/micros_hart_plan_user_return',
                     'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/current-observation/kernel/scheduler/scheduler_core_return_plan.c/preflight_return_threads',
                     'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/current-observation/kernel/scheduler/scheduler_core_return_plan.c/project_candidate_queues',
                     'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/current-observation/kernel/scheduler/scheduler_core_return_plan.c/repair_preempted_candidate',
                     'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/current-observation/kernel/scheduler/scheduler_core_return_plan.c/scratch_head',
                     'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/current-observation/kernel/scheduler/scheduler_core_return_plan.c/scratch_load',
                     'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/current-observation/kernel/scheduler/scheduler_core_return_plan.c/scratch_pick',
                     'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/current-observation/kernel/scheduler/scheduler_core_return_plan.c/scratch_remove',
                     'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/current-observation/kernel/scheduler/scheduler_core_return_plan.c/scratch_tail',
                     'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/current-observation/kernel/scheduler/scheduler_core_return_plan.c/select_and_renew_candidate',
                     'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/definitions/kernel/scheduler/scheduler_core_return_plan.c',
                     'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/function/kernel/scheduler/scheduler_core_return_plan.c/micros_hart_plan_user_return',
                     'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/function/kernel/scheduler/scheduler_core_return_plan.c/preflight_return_threads',
                     'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/function/kernel/scheduler/scheduler_core_return_plan.c/project_candidate_queues',
                     'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/function/kernel/scheduler/scheduler_core_return_plan.c/repair_preempted_candidate',
                     'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/function/kernel/scheduler/scheduler_core_return_plan.c/scratch_head',
                     'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/function/kernel/scheduler/scheduler_core_return_plan.c/scratch_load',
                     'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/function/kernel/scheduler/scheduler_core_return_plan.c/scratch_pick',
                     'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/function/kernel/scheduler/scheduler_core_return_plan.c/scratch_remove',
                     'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/function/kernel/scheduler/scheduler_core_return_plan.c/scratch_tail',
                     'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/function/kernel/scheduler/scheduler_core_return_plan.c/select_and_renew_candidate',
                     'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/skeleton/kernel/scheduler/scheduler_core_return_plan.c',
                     'tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/function/kernel/scheduler/scheduler_context.c/scheduler_runtime_load_selected_context',
                     'tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/native-input/include/micros/scheduler_core.h',
                     'tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/native-input/kernel/scheduler/scheduler_core_return.c',
                     'tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/native-input/kernel/scheduler/scheduler_runtime_internal.h',
                     'tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/native-input/include/micros/scheduler_core.h',
                     'tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/native-input/kernel/scheduler/scheduler_core_return.c',
                     'tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/native-input/kernel/scheduler/scheduler_runtime_internal.h',
                     'tests/host/selected_release_handoff_native_source_bindings.py:native-source-admission:MEM05/native-input/include/micros/scheduler_core.h',
                     'tests/host/selected_release_handoff_native_source_bindings.py:native-source-admission:MEM05/native-input/kernel/scheduler/scheduler_context.c',
                     'tests/host/selected_release_handoff_native_source_bindings.py:native-source-admission:MEM05/native-input/kernel/scheduler/scheduler_core_return.c',
                     'tests/host/selected_release_handoff_native_source_bindings.py:native-source-admission:MEM05/native-input/kernel/scheduler/scheduler_runtime_internal.h',
                     'tests/host/test_kernel_object_source_layout.py:data-entry:STABLE_HASHES/include/micros/scheduler_core.h',
                     'tests/host/test_scheduler_source_layout.py:data-entry:STABLE_ARTIFACT_HASHES/include/micros/scheduler_core.h',
                     'tests/host/test_scheduler_source_layout.py:data-table:FROZEN_FACTS/kernel/scheduler.c/micros_scheduler_idle_select',
                     'tests/host/test_scheduler_source_layout.py:data-table:FROZEN_FACTS/kernel/scheduler.c/micros_scheduler_start',
                     'tests/host/test_scheduler_source_layout.py:data-table:FROZEN_FACTS/kernel/scheduler.c/select_user_return',
                     'tests/host/test_scheduler_source_layout.py:function-body:FROZEN_FUNCTIONS/kernel/scheduler.c/load_selected_context',
                     'tests/host/test_scheduler_source_layout.py:function-body:FROZEN_FUNCTIONS/kernel/scheduler_core.c/micros_hart_commit_user_return',
                     'tests/host/test_scheduler_source_layout.py:function-body:FROZEN_FUNCTIONS/kernel/scheduler_core.c/micros_scheduler_apply_return_plan',
                     'tests/host/test_scheduler_source_layout.py:function-body:FROZEN_FUNCTIONS/kernel/scheduler_core.c/plans_equal',
                     'tests/host/test_scheduler_source_layout.py:function-body:FROZEN_FUNCTIONS/kernel/scheduler_core.c/scratch_head',
                     'tests/host/test_scheduler_source_layout.py:function-body:FROZEN_FUNCTIONS/kernel/scheduler_core.c/scratch_pick',
                     'tests/host/test_scheduler_source_layout.py:function-body:FROZEN_FUNCTIONS/kernel/scheduler_core.c/scratch_remove',
                     'tests/host/test_scheduler_source_layout.py:function-body:FROZEN_FUNCTIONS/kernel/scheduler_core.c/scratch_tail'),
 'native_bindings_sha256': '873bd1d2ef44258bffeb7f44e9752a66370f4cc046c2dbc5322f42ab529e0c97',
 'native_binding_count': 455,
 'current_function_contracts_sha256': '2776c82360f9403596de996e4925b4bec7f4295d59ed6efc1f8b338bda837159',
 'current_function_count': 13,
 'adapter_sha256': {'kernel/tests/scheduler/scheduler_test_finish.c': '9b2aa644a06ec479a124915342ed07b79b2bf8c795c105ff3db86c3429983bc0',
                    'tests/host/kernel_layout_source.py': '86a27e9152c994da2d22e4be6d28fe49e0d0d3fc435ace56fe1aabc4c9fbfdfd',
                    'tests/host/kernel_mapping_source.py': '3e986d8414b060e0d713567486db0625a4eea49c55669085cd9a857df1b50815',
                    'tests/host/object_hart_validation_source.py': '9b860a02d4dcbbae0367c2f1b0f8735fbd9efa5f4ac1416f0b91eef4194e1391',
                    'tests/host/selected_address_space_source.py': '6416717577823ae286124af677a380a27207bf6306e89e015347a4f1c399e88f',
                    'tests/host/selected_ownership_source.py': '41b8b9bd7b4de6b857ab427a70aeb1fe23d9e52ead43ae1cfd8dbf53a7513883',
                    'tests/host/selected_release_handoff_source.py': '0bf5c421c3dda48ab30ec949db0e3528f03960a64c1cd256abf64482ed53cecb',
                    'tests/host/test_kernel_object_source_layout.py': '3ffc3c6dcf4c2aca6c1e1e9c9d7811c645b58f9508cd16c2678910d381780e94',
                    'tests/host/test_scheduler_source_layout.py': '1405dd0bc7e056761757fc025c0369ea99279d811a4f66a65bcbfdb93513b0af',
                    'tools/generate_kernel_mapping_fixture.py': 'bb068daae53db5e0a36ec7918fc7167070c0d3cfb68cf95277e297280c1e60de',
                    'tools/generate_selected_ownership_fixture.py': 'ba8f39db43703409092c46d143e9afa419e0c13cf714f8f847242e54503af91a',
                    'tools/generate_selected_release_handoff_fixture.py': 'ad56ca10f311e54d6d7f2b9b293ae8ab841eb3854e5cc0b954b851877883dcfe'},
 'unchanged_sha256': {'kernel/scheduler/scheduler_completion.c': 'af443f8f1a780fe87c16b8869247b73c8a084213627c6227769207f6216c8975',
                      'kernel/scheduler/scheduler_interrupt.c': 'b52c79e84c7ff220c732fd62b7a8ccb1b2eabec137bad4553777f16d484e768f',
                      'kernel/tests/scheduler/scheduler_test_support.c': '6c7736129fbad7ce3ffd413ba1eb140183ed7cb2cbe40e6c02e2d437e9bbce08',
                      'tests/host/memory_native_source_bindings.py': '4a90c7ec808b4fa3edd4606b83b250b0e9ddba4cbde09de03df65dea6ec35202',
                      'tests/host/object_hart_native_source_bindings.py': '30152a81dbb5e7959f2e91e35fffba63b9426af40c6d075bfb28a4e47891e414',
                      'tests/host/selected_address_space_native_source_bindings.py': '5736cc190f48f94975546d209ccb4b36ca7549d09fc7e6b9880ebc2095933254',
                      'tests/host/selected_ownership_native_source_bindings.py': '6b67d60370e9ee30c480a730c400589660e11d5919ce4e728b3265d449629903',
                      'tests/host/selected_release_handoff_native_source_bindings.py': '2827df6a3236463106b325292883ae5ac83a0922a2871a26220126436d9db93f'}}
STATE_WORK_CONTRACT = {'plan_limit': 320,
 'clear_limit': 640,
 'copy_limit': 320,
 'probe_bound': '128+64*z+16*p',
 'store_bound': '48+4*z',
 'priority_bound': '(z+1)*16',
 'target_calls': (1, 2, 1),
 'baseline_target_calls': (2, 2, 1),
 'target_workflow': 'test-qemu-scheduler',
 'target_kernel': 'build/riscv64-scheduler-test/kernel/micros.elf',
 'legacy_scoped_cases': ('nonlive-assigned-record',
                         'reschedule-without-assignment',
                         'unreachable-assigned-ready'),
 'plan_side_record': ('version', 'case', 'bytes', 'before', 'after', 'has_effect', 'effect'),
 'baseline_cases_sha256': 'e73863fb89dd3555924a76c374036dd0940a3da28d2aa195df83dc8c21637f4f',
 'current_cases_sha256': '6b3c6bc1ee6f258552ea7c1ed53c633a2447511ec68ee25bc5d7e01a32d5d0c4',
 'baseline_cases': 174,
 'current_cases': 180}
RECIPE_ADMISSION = {
    "native_targets": (
        "common_scheduler_return_baseline_ordinary", "common_scheduler_return_baseline_work",
        "common_scheduler_return_current_ordinary", "common_scheduler_return_test",
    ),
    "generator": "tools/generate_common_scheduler_return_fixture.py",
    "driver": "tools/check_common_scheduler_return.py",
    "native_root": "build/host-debug/common-scheduler-return",
    "target_report_option": "--report-transcript",
}
RUNTIME_REFERENCE = "34125516b60e9e0a9e5362ff2ee60555691adb99"
RUNTIME_TREE = "1de53de9dcf5102dbef3e76d59b7f2fef28cee9e"
OBSERVATION_PREFIX = '#include "tests/host/common_scheduler_return_test.h"\n'
PLAN_PATH = "kernel/scheduler/scheduler_core_return_plan.c"
APPLY_PATH = "kernel/scheduler/scheduler_core_return.c"
TARGET_PATHS = (
    "kernel/scheduler/scheduler.c",
    "kernel/scheduler/scheduler_context.c",
    "kernel/scheduler/scheduler_return.c",
    "kernel/scheduler/scheduler_idle.c",
)
TARGET_INCLUDE = '#include "tests/qemu/common_scheduler_return_work.h"\n'
PLAN_DECLARATION = """struct micros_scheduler_return_patch {
    struct micros_thread_handle thread;
    struct micros_thread_handle next;
};

struct micros_scheduler_return_queue {
    uint8_t priority;
    uint8_t patch_count;
    uint16_t renew_count;
    struct micros_thread_handle before_head;
    struct micros_thread_handle before_tail;
    struct micros_thread_handle after_head;
    struct micros_thread_handle after_tail;
    struct micros_thread_handle renew_first;
    struct micros_scheduler_return_patch patches[3];
};

struct micros_scheduler_return_plan {
    struct micros_hart_handle hart;
    struct micros_thread_handle outgoing;
    struct micros_thread_handle selected;
    enum micros_scheduler_return_action action;
    bool repair_preempted;
    bool renew_quantum;
    bool renew_current;
    bool publish_current;
    uint8_t queue_count;
    struct micros_scheduler_return_queue queues[2];
};

_Static_assert(sizeof(struct micros_scheduler_return_plan) <= 320, "bounded return plan");
"""
CURSOR_DECLARATION = """struct scheduler_return_cursor {
    struct micros_scheduler_return_queue *queue;
    struct micros_thread_handle initial_head;
    struct micros_thread_handle initial_tail;
    struct micros_thread_handle rotated_last;
    bool tail_reached;
    bool wrapped;
};
"""
OUTGOING_DECLARATION = """struct scheduler_outgoing_context {
    struct micros_thread_handle thread;
    struct micros_user_context context;
};

"""
REMOVED_FUNCTIONS = {
    "kernel/scheduler/scheduler_core_return_plan.c": (
        "scratch_load", "scratch_remove", "scratch_head", "scratch_tail", "scratch_pick",
        "preflight_return_threads", "project_candidate_queues",
    ),
}
CURRENT_SIGNATURES = {
    "return_queue_at_priority":
        "static struct scheduler_return_cursor *return_queue_at_priority("
        "const struct micros_scheduler_return_plan *candidate, "
        "struct scheduler_return_cursor cursors[2], uint8_t priority)",
    "prepare_return_queue":
        "static enum micros_kernel_object_error prepare_return_queue("
        "const struct micros_kernel_objects *objects, const struct micros_hart *hart, "
        "uint8_t priority, struct micros_scheduler_return_plan *candidate, "
        "struct scheduler_return_cursor cursors[2], struct scheduler_return_cursor **result)",
    "pick_return_queue":
        "static enum micros_kernel_object_error pick_return_queue("
        "const struct micros_kernel_objects *objects, const struct micros_hart *hart, "
        "struct micros_scheduler_return_plan *candidate, struct scheduler_return_cursor cursors[2], "
        "struct scheduler_return_cursor **result)",
    "repair_preempted_candidate":
        "static enum micros_kernel_object_error repair_preempted_candidate("
        "const struct micros_kernel_objects *objects, const struct micros_hart *hart, "
        "struct micros_scheduler_return_plan *candidate, struct scheduler_return_cursor cursors[2])",
    "rotate_return_head":
        "static enum micros_kernel_object_error rotate_return_head("
        "const struct micros_kernel_objects *objects, const struct micros_hart *hart, "
        "const struct micros_scheduler_return_plan *candidate, struct scheduler_return_cursor *cursor, "
        "struct micros_thread_handle selected, const struct micros_thread *thread)",
    "select_and_renew_candidate":
        "static enum micros_kernel_object_error select_and_renew_candidate("
        "const struct micros_kernel_objects *objects, const struct micros_hart *hart, "
        "struct micros_scheduler_return_plan *candidate, struct scheduler_return_cursor cursors[2])",
    "set_return_patch":
        "static enum micros_kernel_object_error set_return_patch("
        "struct micros_scheduler_return_queue *queue, struct micros_thread_handle thread, "
        "struct micros_thread_handle next)",
    "finish_return_queue":
        "static enum micros_kernel_object_error finish_return_queue("
        "const struct micros_kernel_objects *objects, const struct micros_scheduler_return_plan *candidate, "
        "struct scheduler_return_cursor *cursor)",
    "queue_deltas_equal":
        "static bool queue_deltas_equal(const struct micros_scheduler_return_queue *left, "
        "const struct micros_scheduler_return_queue *right)",
    "renew_return_thread":
        "static void renew_return_thread(struct micros_thread *thread)",
}
CURRENT_BODIES = {
    "kernel/scheduler/scheduler_core_return_plan.c": {
        "return_queue_at_priority": """
    if (candidate->queue_count > 0 && candidate->queues[0].priority == priority) {
        return &cursors[0];
    }
    if (candidate->queue_count > 1 && candidate->queues[1].priority == priority) {
        return &cursors[1];
    }
    return NULL;
""",
        "prepare_return_queue": """
    struct scheduler_return_cursor *cursor = return_queue_at_priority(candidate, cursors, priority);
    struct micros_scheduler_return_queue *queue;
    enum micros_kernel_object_error error;

    if (cursor != NULL) {
        *result = cursor;
        return MICROS_KERNEL_OBJECT_OK;
    }
    if (candidate->queue_count >= 2) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    error = scheduler_core_preflight_queue_endpoints(objects, hart, priority);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    cursor = &cursors[candidate->queue_count];
    queue = &candidate->queues[candidate->queue_count];
    queue->priority = priority;
    queue->before_head = hart->ready_head[priority];
    queue->before_tail = hart->ready_tail[priority];
    queue->after_head = queue->before_head;
    queue->after_tail = queue->before_tail;
    cursor->queue = queue;
    cursor->initial_head = queue->before_head;
    cursor->initial_tail = queue->before_tail;
    cursor->rotated_last = scheduler_core_null_thread_handle();
    cursor->tail_reached = scheduler_core_thread_handle_is_null(queue->before_head);
    cursor->wrapped = false;
    ++candidate->queue_count;
    *result = cursor;
    return MICROS_KERNEL_OBJECT_OK;
""",
        "pick_return_queue": """
    size_t priority;
    enum micros_kernel_object_error error;

    for (priority = 0; priority < MICROS_SCHEDULER_PRIORITY_COUNT; ++priority) {
        struct scheduler_return_cursor *cursor =
            return_queue_at_priority(candidate, cursors, (uint8_t)priority);
        struct micros_thread_handle head;

        if (cursor != NULL) {
            head = cursor->queue->after_head;
        } else {
            error = scheduler_core_preflight_queue_endpoints(objects, hart, (uint8_t)priority);
            if (error != MICROS_KERNEL_OBJECT_OK) {
                return error;
            }
            head = hart->ready_head[priority];
        }
        if (head.generation != 0) {
            return prepare_return_queue(objects, hart, (uint8_t)priority, candidate, cursors, result);
        }
    }
    *result = NULL;
    return MICROS_KERNEL_OBJECT_OK;
""",
        "repair_preempted_candidate": """
    const struct micros_thread *current;
    struct scheduler_return_cursor *cursor;
    struct micros_scheduler_return_queue *queue;
    enum micros_kernel_object_error error;
    uint32_t resulting;

    if (scheduler_core_thread_handle_is_null(candidate->outgoing)) {
        return MICROS_KERNEL_OBJECT_OK;
    }
    current = &objects->threads[candidate->outgoing.slot];
    candidate->repair_preempted = (current->runtime_flags & MICROS_THREAD_RTS_PREEMPTED) != 0;
    resulting = current->runtime_flags & ~MICROS_THREAD_RTS_PREEMPTED;
    if (resulting != 0) {
        return MICROS_KERNEL_OBJECT_OK;
    }
    error = prepare_return_queue(objects, hart, current->scheduler_priority, candidate, cursors, &cursor);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    if (!candidate->repair_preempted) {
        return MICROS_KERNEL_OBJECT_OK;
    }
    candidate->publish_current = true;
    queue = cursor->queue;
    if (scheduler_core_thread_handle_is_null(queue->before_head)) {
        queue->after_head = candidate->outgoing;
        queue->after_tail = candidate->outgoing;
    } else if (current->remaining_counter_ticks == 0) {
        queue->after_tail = candidate->outgoing;
    } else {
        queue->after_head = candidate->outgoing;
    }
    cursor->initial_head = queue->after_head;
    cursor->initial_tail = queue->after_tail;
    return MICROS_KERNEL_OBJECT_OK;
""",
        "rotate_return_head": """
    struct micros_scheduler_return_queue *queue = cursor->queue;
    struct micros_thread_handle next;
    const struct micros_thread *successor;
    enum micros_kernel_object_error error;
    bool inserted = candidate->publish_current
        && scheduler_core_thread_handles_equal(selected, candidate->outgoing);

    if (!scheduler_core_thread_handles_equal(selected, queue->after_head)) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    if (inserted) {
        next = thread->remaining_counter_ticks == 0
            ? scheduler_core_null_thread_handle() : queue->before_head;
    } else {
        next = thread->ready_next;
        if (scheduler_core_thread_handles_equal(selected, queue->before_tail)) {
            if (!scheduler_core_thread_handle_is_null(next)) {
                return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
            }
            cursor->tail_reached = true;
            if (candidate->publish_current) {
                const struct micros_thread *outgoing = &objects->threads[candidate->outgoing.slot];

                if (outgoing->scheduler_priority == queue->priority
                    && outgoing->remaining_counter_ticks == 0) {
                    next = candidate->outgoing;
                }
            }
        } else {
            if (scheduler_core_thread_handle_is_null(next)
                || scheduler_core_thread_handles_equal(next, selected)
                || scheduler_core_thread_handles_equal(next, candidate->outgoing)) {
                return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
            }
            error = scheduler_core_preflight_queue_record(objects, hart, queue->priority, next, &successor);
            if (error != MICROS_KERNEL_OBJECT_OK) {
                return error;
            }
        }
    }
    if (scheduler_core_thread_handle_is_null(next)) {
        if (!cursor->tail_reached) {
            return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
        }
        next = cursor->initial_head;
        cursor->wrapped = true;
    }
    queue->after_head = next;
    queue->after_tail = selected;
    cursor->rotated_last = selected;
    return MICROS_KERNEL_OBJECT_OK;
""",
        "select_and_renew_candidate": """
    struct scheduler_return_cursor *cursor = NULL;
    size_t renew_count = 0;
    enum micros_kernel_object_error error;

    if (!scheduler_core_thread_handle_is_null(candidate->outgoing)
        && objects->threads[candidate->outgoing.slot].runtime_flags == 0) {
        cursor = return_queue_at_priority(
            candidate, cursors, objects->threads[candidate->outgoing.slot].scheduler_priority
        );
    } else {
        error = pick_return_queue(objects, hart, candidate, cursors, &cursor);
        if (error != MICROS_KERNEL_OBJECT_OK) {
            return error;
        }
    }
    while (cursor != NULL) {
        struct micros_scheduler_return_queue *queue = cursor->queue;
        struct micros_thread_handle selected = queue->after_head;
        const struct micros_thread *thread;
        bool current = scheduler_core_thread_handles_equal(selected, candidate->outgoing);

        if (current) {
            thread = &objects->threads[selected.slot];
        } else {
            error = scheduler_core_preflight_queue_record(objects, hart, queue->priority, selected, &thread);
            if (error != MICROS_KERNEL_OBJECT_OK) {
                return error;
            }
        }
        if (thread->remaining_counter_ticks != 0) {
            break;
        }
        if ((current && candidate->renew_current)
            || (queue->renew_count != 0 && scheduler_core_thread_handles_equal(selected, queue->renew_first))) {
            if (!cursor->wrapped) {
                return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
            }
            break;
        }
        if (++renew_count > MICROS_THREAD_CAPACITY) {
            return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
        }
        if (current) {
            candidate->renew_current = true;
        } else {
            if (queue->renew_count == 0) {
                queue->renew_first = selected;
            }
            ++queue->renew_count;
        }
        candidate->renew_quantum = true;
        if (!thread->scheduler_preemptible) {
            break;
        }
        error = rotate_return_head(objects, hart, candidate, cursor, selected, thread);
        if (error != MICROS_KERNEL_OBJECT_OK) {
            return error;
        }
        error = pick_return_queue(objects, hart, candidate, cursors, &cursor);
        if (error != MICROS_KERNEL_OBJECT_OK) {
            return error;
        }
    }
    candidate->selected = cursor == NULL ? scheduler_core_null_thread_handle() : cursor->queue->after_head;
    return MICROS_KERNEL_OBJECT_OK;
""",
        "set_return_patch": """
    size_t index;

    for (index = 0; index < queue->patch_count; ++index) {
        if (scheduler_core_thread_handles_equal(queue->patches[index].thread, thread)) {
            queue->patches[index].next = next;
            return MICROS_KERNEL_OBJECT_OK;
        }
    }
    if (queue->patch_count >= 3) {
        return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    queue->patches[queue->patch_count].thread = thread;
    queue->patches[queue->patch_count].next = next;
    ++queue->patch_count;
    return MICROS_KERNEL_OBJECT_OK;
""",
        "finish_return_queue": """
    struct micros_scheduler_return_queue *queue = cursor->queue;
    enum micros_kernel_object_error error;

    if (candidate->publish_current) {
        const struct micros_thread *current = &objects->threads[candidate->outgoing.slot];

        if (current->scheduler_priority == queue->priority) {
            if (current->remaining_counter_ticks == 0
                && !scheduler_core_thread_handle_is_null(queue->before_head)) {
                error = set_return_patch(queue, queue->before_tail, candidate->outgoing);
                if (error != MICROS_KERNEL_OBJECT_OK) {
                    return error;
                }
            }
            error = set_return_patch(
                queue, candidate->outgoing, current->remaining_counter_ticks == 0
                    ? scheduler_core_null_thread_handle() : queue->before_head
            );
            if (error != MICROS_KERNEL_OBJECT_OK) {
                return error;
            }
        }
    }
    if (!scheduler_core_thread_handle_is_null(cursor->rotated_last)) {
        error = set_return_patch(queue, cursor->initial_tail, cursor->initial_head);
        if (error != MICROS_KERNEL_OBJECT_OK) {
            return error;
        }
        error = set_return_patch(queue, cursor->rotated_last, scheduler_core_null_thread_handle());
        if (error != MICROS_KERNEL_OBJECT_OK) {
            return error;
        }
    }
    return MICROS_KERNEL_OBJECT_OK;
""",
        "micros_hart_plan_user_return": """
    const struct micros_hart *hart;
    struct micros_scheduler_return_plan candidate;
    struct scheduler_return_cursor cursors[2];
    size_t index;
    enum micros_kernel_object_error error;

    if (plan == NULL) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    clear_return_candidate(&candidate);
    error = preflight_return_plan(objects, hart_handle, &hart);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    candidate.hart = hart_handle;
    candidate.outgoing = hart->current_thread;
    error = repair_preempted_candidate(objects, hart, &candidate, cursors);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    error = select_and_renew_candidate(objects, hart, &candidate, cursors);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return error;
    }
    for (index = 0; index < candidate.queue_count; ++index) {
        error = finish_return_queue(objects, &candidate, &cursors[index]);
        if (error != MICROS_KERNEL_OBJECT_OK) {
            return error;
        }
    }
    candidate.action = scheduler_core_thread_handle_is_null(candidate.selected)
        ? MICROS_SCHEDULER_RETURN_ENTER_IDLE
        : scheduler_core_thread_handles_equal(candidate.selected, candidate.outgoing)
            ? MICROS_SCHEDULER_RETURN_KEEP_CURRENT : MICROS_SCHEDULER_RETURN_SELECT_THREAD;
    publish_return_candidate(plan, &candidate);
    return MICROS_KERNEL_OBJECT_OK;
""",
    },
    "kernel/scheduler/scheduler_core_return.c": {
        "queue_deltas_equal": """
    size_t index;

    if (left->priority != right->priority || left->patch_count != right->patch_count
        || left->renew_count != right->renew_count
        || !scheduler_core_thread_handles_equal(left->before_head, right->before_head)
        || !scheduler_core_thread_handles_equal(left->before_tail, right->before_tail)
        || !scheduler_core_thread_handles_equal(left->after_head, right->after_head)
        || !scheduler_core_thread_handles_equal(left->after_tail, right->after_tail)
        || !scheduler_core_thread_handles_equal(left->renew_first, right->renew_first)) {
        return false;
    }
    for (index = 0; index < 3; ++index) {
        if (!scheduler_core_thread_handles_equal(left->patches[index].thread, right->patches[index].thread)
            || !scheduler_core_thread_handles_equal(left->patches[index].next, right->patches[index].next)) {
            return false;
        }
    }
    return true;
""",
        "plans_equal": """
    size_t index;

    if (!scheduler_core_hart_handles_equal(left->hart, right->hart)
        || !scheduler_core_thread_handles_equal(left->outgoing, right->outgoing)
        || !scheduler_core_thread_handles_equal(left->selected, right->selected)
        || left->action != right->action || left->repair_preempted != right->repair_preempted
        || left->renew_quantum != right->renew_quantum || left->renew_current != right->renew_current
        || left->publish_current != right->publish_current
        || left->queue_count != right->queue_count || left->queue_count > 2) {
        return false;
    }
    for (index = 0; index < 2; ++index) {
        if (!queue_deltas_equal(&left->queues[index], &right->queues[index])) {
            return false;
        }
    }
    return true;
""",
        "renew_return_thread": """
    thread->runtime_flags |= MICROS_THREAD_RTS_NO_QUANTUM;
    thread->remaining_counter_ticks = thread->quantum_counter_ticks;
    thread->runtime_flags &= ~MICROS_THREAD_RTS_NO_QUANTUM;
""",
        "micros_scheduler_apply_return_plan": """
    struct micros_hart *hart = &objects->harts[plan->hart.slot];
    size_t index;

    if (plan->repair_preempted) {
        struct micros_thread *outgoing = &objects->threads[plan->outgoing.slot];

        outgoing->runtime_flags &= ~MICROS_THREAD_RTS_PREEMPTED;
    }
    if (plan->renew_current) {
        renew_return_thread(&objects->threads[plan->outgoing.slot]);
    }
    for (index = 0; index < plan->queue_count; ++index) {
        const struct micros_scheduler_return_queue *queue = &plan->queues[index];
        struct micros_thread_handle cursor = queue->renew_first;
        size_t renewed;

        for (renewed = 0; renewed < queue->renew_count; ++renewed) {
            struct micros_thread *thread = &objects->threads[cursor.slot];

            cursor = thread->ready_next;
            renew_return_thread(thread);
        }
    }
    if (plan->publish_current) {
        objects->threads[plan->outgoing.slot].ready_linked = true;
    }
    for (index = 0; index < plan->queue_count; ++index) {
        const struct micros_scheduler_return_queue *queue = &plan->queues[index];
        size_t patch;

        for (patch = 0; patch < queue->patch_count; ++patch) {
            objects->threads[queue->patches[patch].thread.slot].ready_next = queue->patches[patch].next;
        }
        hart->ready_head[queue->priority] = queue->after_head;
        hart->ready_tail[queue->priority] = queue->after_tail;
    }
    hart->current_thread = plan->selected;
    hart->reschedule_pending = false;
    if (scheduler_core_thread_handle_is_null(plan->selected)) {
        hart->trap.primary_stack_bottom = hart->idle_primary_stack_bottom;
        hart->trap.primary_stack_top = hart->idle_primary_stack_top;
    } else {
        const struct micros_thread *selected = &objects->threads[plan->selected.slot];

        hart->trap.primary_stack_bottom = selected->kernel_stack_bottom;
        hart->trap.primary_stack_top = selected->kernel_stack_top;
    }
""",
    },
}
DECLARATION_NAMES = (
    "SOURCE_ROLES", "SOURCE_PROJECTION", "STATE_WORK_CONTRACT", "RECIPE_ADMISSION",
    "RUNTIME_REFERENCE", "RUNTIME_TREE", "TARGET_PATHS", "TARGET_INCLUDE", "OBSERVATION_PREFIX",
    "PLAN_DECLARATION", "CURSOR_DECLARATION", "OUTGOING_DECLARATION",
    "REMOVED_FUNCTIONS", "CURRENT_SIGNATURES", "CURRENT_BODIES",
    "LEGACY_VALUE_PREFIX", "TARGET_RECIPE", "TARGET_REPORT_ANCHOR",
)


def _replace_once(text, before, after):
    if text.count(before) != 1:
        raise ValueError("OBJ02 transformation lost one exact boundary: " + before[:100])
    return text.replace(before, after, 1)


def _map_function(text, name, transform):
    records = function_spans(text)
    if name not in records:
        raise ValueError("OBJ02 transformation lost its function: " + name)
    span = records[name]
    return text[:span.body_start] + transform(text[span.body_start:span.body_end]) + text[span.body_end:]


def extend_target_observations(path, text):
    if path not in TARGET_PATHS or TARGET_INCLUDE in text or "MICROS_OBJ02_" in text:
        raise ValueError("OBJ02 target observer has an unknown or already annotated source")
    text = TARGET_INCLUDE + text
    if path.endswith(("scheduler_return.c", "scheduler_context.c")):
        text = _replace_once(text, "micros_user_execution_validate_context(", "MICROS_OBJ02_VALIDATE_CONTEXT(")
    if path.endswith("/scheduler.c"):
        def annotate_start(body):
            body = _replace_once(
                body, "    error = micros_hart_plan_user_return(",
                "    MICROS_OBJ02_RETURN_BEGIN(MICROS_OBJ02_RETURN_START, hart->current_thread);\n"
                "    error = micros_hart_plan_user_return(",
            )
            body = _replace_once(
                body, "        riscv_irq_restore(saved_status);\n        return error == MICROS_KERNEL_OBJECT_OK\n",
                "        MICROS_OBJ02_RETURN_CANCEL();\n"
                "        riscv_irq_restore(saved_status);\n        return error == MICROS_KERNEL_OBJECT_OK\n",
            )
            anchor = "    scheduler_error = scheduler_runtime_preflight_kernel_interval(hart, &accounting);"
            if body.count(anchor) != 1:
                raise ValueError("OBJ02 start observer lost its first accounting sample")
            before, after = body.split(anchor)
            guard = "        riscv_irq_restore(saved_status);\n        return scheduler_error;"
            if before.count(guard) != 2:
                raise ValueError("OBJ02 start observer lost a recoverable context failure")
            before = before.replace(guard, "        MICROS_OBJ02_RETURN_CANCEL();\n" + guard)
            return before + "    MICROS_OBJ02_RETURN_END(plan.selected);\n" + anchor + after
        text = _map_function(text, "micros_scheduler_start", annotate_start)
    elif path.endswith("/scheduler_return.c"):
        def annotate_return(body):
            body = _replace_once(
                body, "    scheduler_error = capture_outgoing_custody(",
                "    MICROS_OBJ02_RETURN_BEGIN(\n"
                "        store_outgoing ? MICROS_OBJ02_RETURN_USER : MICROS_OBJ02_RETURN_CAPTURED,\n"
                "        hart->current_thread\n    );\n    scheduler_error = capture_outgoing_custody(",
            )
            body = _replace_once(
                body, "        return scheduler_error;\n    }\n    micros_diagnostic_notify_pending",
                "        MICROS_OBJ02_RETURN_CANCEL();\n        return scheduler_error;\n"
                "    }\n    micros_diagnostic_notify_pending",
            )
            body = _replace_once(
                body, "        return scheduler_runtime_map_object_error(error);",
                "        MICROS_OBJ02_RETURN_CANCEL();\n        return scheduler_runtime_map_object_error(error);",
            )
            for indent in ("        ", "    "):
                anchor = "\n" + indent + "scheduler_error = prepare_timer_accounting(runtime, hart, &accounting);"
                body = _replace_once(body, anchor, "\n" + indent + "MICROS_OBJ02_RETURN_END(plan.selected);" + anchor)
            return body
        text = _map_function(text, "select_user_return", annotate_return)
    elif path.endswith("/scheduler_idle.c"):
        def annotate_idle(body):
            body = _replace_once(
                body, "    micros_diagnostic_notify_pending(hart, NULL);",
                "    MICROS_OBJ02_RETURN_BEGIN(MICROS_OBJ02_RETURN_IDLE, hart->current_thread);\n"
                "    micros_diagnostic_notify_pending(hart, NULL);",
            )
            for indent in ("        ", "    "):
                anchor = "\n" + indent + "prepare_idle_timer_accounting(runtime, hart, &accounting);"
                body = _replace_once(body, anchor, "\n" + indent + "MICROS_OBJ02_RETURN_END(plan.selected);" + anchor)
            return body
        text = _map_function(text, "micros_scheduler_idle_select", annotate_idle)
    return text


def project_target_observations(path, text):
    if path not in TARGET_PATHS or text.count(TARGET_INCLUDE) != 1 or not text.startswith(TARGET_INCLUDE):
        raise ValueError("OBJ02 target source lacks its exact passive include")
    original = text.removeprefix(TARGET_INCLUDE)
    original = original.replace("MICROS_OBJ02_VALIDATE_CONTEXT(", "micros_user_execution_validate_context(")
    original = re.sub(r"(?m)^[ \t]*MICROS_OBJ02_RETURN_(?:BEGIN|END|CANCEL)\([^;]*\);\n", "", original)
    if "MICROS_OBJ02_" in original or extend_target_observations(path, original) != text:
        raise ValueError("OBJ02 target source moved, omitted or changed a passive hook")
    return original


@lru_cache(maxsize=256)
def fixed_source(root, path):
    root = Path(root).resolve()
    if path.startswith("/") or ".." in Path(path).parts or "\\" in path:
        raise ValueError("OBJ02 fixed source leaves its repository namespace")
    if subprocess.check_output(
        ("git", "rev-parse", RUNTIME_REFERENCE + "^{tree}"), cwd=root, text=True, timeout=30,
    ).strip() != RUNTIME_TREE:
        raise ValueError("OBJ02 fixed runtime commit/tree changed")
    entries = subprocess.check_output(
        ("git", "ls-tree", "-z", RUNTIME_REFERENCE, "--", path), cwd=root, timeout=30,
    ).split(b"\0")
    if len(entries) != 2 or entries[1] or not entries[0].startswith(b"100644 blob "):
        raise ValueError("OBJ02 fixed source is not one ordinary file: " + path)
    metadata, separator, name = entries[0].partition(b"\t")
    if not separator or name.decode() != path:
        raise ValueError("OBJ02 fixed source borrowed another path")
    raw = subprocess.check_output(
        ("git", "show", RUNTIME_REFERENCE + ":" + path), cwd=root, timeout=30,
    )
    if hashlib.sha1(b"blob " + str(len(raw)).encode() + b"\0" + raw).hexdigest() != metadata.split()[2].decode():
        raise ValueError("OBJ02 fixed source differs from its Git blob")
    return raw.decode()


def fixed_native_inputs(root):
    return {path: fixed_source(str(Path(root).resolve()), path).encode()
            for path in (*NATIVE_SOURCES, *NATIVE_HEADERS)}


def declared_header(root, path):
    original = fixed_source(str(Path(root).resolve()), path)
    if path == "include/micros/scheduler_core.h":
        match = re.search(r"struct micros_scheduler_return_plan \{.*?\n\};\n", original, re.S)
        if match is None:
            raise ValueError("OBJ02 public plan declaration is absent")
        return original[:match.start()] + PLAN_DECLARATION + original[match.end():]
    if path == "kernel/scheduler/scheduler_runtime_internal.h":
        return _replace_once(original, "struct scheduler_accounting_commit {",
                             OUTGOING_DECLARATION + "struct scheduler_accounting_commit {")
    raise ValueError("OBJ02 has no mutable header at " + path)


def declared_target_source(root, path):
    original = fixed_source(str(Path(root).resolve()), path)
    if path not in TARGET_PATHS:
        raise ValueError("OBJ02 target contract has an unknown owner")
    if path.endswith("/scheduler_return.c"):
        text = _replace_once(
            original, "    bool store_outgoing\n)\n{\n    struct micros_user_context outgoing;",
            "    bool store_outgoing,\n    struct scheduler_outgoing_context *outgoing\n)\n{",
        )

        def capture(body):
            body = _replace_once(
                body, "    if (store_outgoing) {",
                "    outgoing->thread = (struct micros_thread_handle){0, 0};\n    if (store_outgoing) {",
            )
            if body.count("&outgoing") != 3:
                raise ValueError("OBJ02 outgoing proof lost its exact copy/validate/store image")
            body = body.replace("&outgoing", "&outgoing->context")
            return _replace_once(
                body, "        }\n    } else if (",
                "        }\n        outgoing->thread = current;\n    } else if (",
            )
        text = _map_function(text, "capture_outgoing_custody", capture)

        def selector(body):
            body = _replace_once(
                body, "    struct scheduler_completion_commit completion;",
                "    struct scheduler_completion_commit completion;\n    struct scheduler_outgoing_context outgoing;",
            )
            body = _replace_once(
                body, "objects, hart_handle, hart, frame, store_outgoing",
                "objects, hart_handle, hart, frame, store_outgoing, &outgoing",
            )
            return _replace_once(
                body,
                "    scheduler_error = scheduler_runtime_load_selected_context(runtime, plan.selected);\n"
                "    if (scheduler_error != MICROS_SCHEDULER_OK) {\n"
                "        panic_invalid_context(hart, frame, false);\n    }\n",
                "    if (outgoing.thread.generation != 0\n"
                "        && plan.selected.slot == outgoing.thread.slot\n"
                "        && plan.selected.generation == outgoing.thread.generation) {\n"
                "        scheduler_runtime_copy_user_context(&runtime->selected_context, &outgoing.context);\n"
                "    } else {\n"
                "        scheduler_error = scheduler_runtime_load_selected_context(runtime, plan.selected);\n"
                "        if (scheduler_error != MICROS_SCHEDULER_OK) {\n"
                "            panic_invalid_context(hart, frame, false);\n        }\n    }\n",
            )
        return _map_function(text, "select_user_return", selector)
    return original


@lru_cache(maxsize=4)
def current_function_contracts(root):
    from tests.host.selected_address_space_source import _function_calls

    contracts = {}
    for path, bodies in CURRENT_BODIES.items():
        previous = function_records(fixed_source(root, path))
        for name, body in bodies.items():
            signature = CURRENT_SIGNATURES.get(name)
            if signature is None:
                signature = previous[name].signature
            contracts[path + "/" + name] = (
                compact(signature), (), tuple(sorted(_function_calls(body))), primitive_shape(path, name, body),
            )
    return contracts


def expected_skeleton(root, path):
    from tests.host.selected_address_space_source import _source_skeleton

    original = fixed_source(root, path)
    if path == PLAN_PATH:
        end = original.find("static enum micros_kernel_object_error scratch_load(")
        if end == -1:
            raise ValueError("OBJ02 old static workspace boundary changed")
        original = '#include "kernel/scheduler/scheduler_core_private.h"\n\n' + CURSOR_DECLARATION + "\n" + original[end:]
    return _source_skeleton(original)


def validate_retained_source(root, path, text, stage):
    from tests.host.selected_address_space_source import _function_calls, _source_skeleton

    root = str(Path(root).resolve())
    paths = (*NATIVE_SOURCES, *NATIVE_HEADERS, *TARGET_PATHS, "kernel/scheduler/scheduler_runtime_internal.h")
    if stage not in ("baseline", "current") or path not in paths or type(text) is not str:
        raise ValueError("OBJ02 source input has an unknown stage, representation or path")
    if path in TARGET_PATHS and TARGET_INCLUDE in text:
        text = project_target_observations(path, text)
    original = fixed_source(root, path)
    mutable_headers = ("include/micros/scheduler_core.h", "kernel/scheduler/scheduler_runtime_internal.h")
    if stage == "baseline" or path not in (*CURRENT_BODIES, *TARGET_PATHS, *mutable_headers):
        if text != original:
            raise ValueError("OBJ02 changed a fixed source input: " + path)
        return
    if path in mutable_headers:
        if text != declared_header(root, path):
            raise ValueError("OBJ02 header differs from its independent declaration")
        return
    if path in TARGET_PATHS:
        declared = declared_target_source(root, path)
        before, after = function_records(declared), function_records(text)
        if set(before) != set(after):
            raise ValueError("OBJ02 target changed its complete definition set: " + path)
        for name, record in after.items():
            expected = before[name]
            if (record.signature, record.guards, record.body) != (expected.signature, expected.guards, expected.body):
                raise ValueError("OBJ02 target escaped its protected-context contract: " + path + "/" + name)
        if _source_skeleton(text) != _source_skeleton(declared):
            raise ValueError("OBJ02 target changed a storage owner or dependency")
        return
    before, after = function_records(original), function_records(text)
    expected_names = set(before).difference(REMOVED_FUNCTIONS.get(path, ())) | set(CURRENT_BODIES[path])
    if set(after) != expected_names:
        raise ValueError("OBJ02 current source has a missing or unadmitted definition: " + path)
    contracts = current_function_contracts(root)
    for name, record in after.items():
        key = path + "/" + name
        if key not in contracts:
            previous = before[name]
            if (record.signature, record.guards, record.body) != (previous.signature, previous.guards, previous.body):
                raise ValueError("OBJ02 changed an unrelated retained function: " + key)
        else:
            signature, guards, calls, shape = contracts[key]
            if (
                compact(record.signature) != signature or record.guards != guards
                or tuple(sorted(_function_calls(record.body))) != calls
                or primitive_shape(path, name, record.body) != shape
                or re.search(r"^\s*#", record.body, re.M)
            ):
                raise ValueError("OBJ02 function escaped its independent signature/callees/primitive contract: " + key)
    if _source_skeleton(text) != expected_skeleton(root, path):
        raise ValueError("OBJ02 source changed an undeclared include, type or storage owner")


def workspace_accessor(stage):
    if stage not in ("baseline", "current"):
        raise ValueError("OBJ02 workspace accessor has an unknown stage")
    expression = "sizeof(plan_scratch)" if stage == "baseline" else "0"
    return "\nsize_t micros_obj02_return_workspace_size(void)\n{\n    return " + expression + ";\n}\n"


def body_sites(path, name, body):
    from tests.host.object_hart_validation_source import body_sites as predecessor_sites
    from tests.host.selected_address_space_source import _matching
    from tests.host.selected_ownership_source import _original_regions

    sites = [
        ("obj02-" + row[0], row[1], "work", *row[3:])
        for row in predecessor_sites(path, name, body)
        if row[4] != "COMPARISON" and not (
            path == PLAN_PATH and name in ("clear_return_candidate", "publish_return_candidate")
            and row[4] == "INDEX_PROBE"
        )
    ]
    positions = positional_tokens(body)
    values = tuple(item.value for item in positions)
    stacks, closings = compound_layout(values)

    def store(kind, first, last, units=1):
        begin = positions[last - 1].end
        primitive = body[positions[first].start:begin]
        regions = _original_regions(values, stacks, closings, stacks[first])
        identifier = "obj02-store-" + hashlib.sha256(
            repr((path, name, kind, first, last, units)).encode()
        ).hexdigest()[:20]
        sites.append((identifier, "insert", "work", "KERNEL_SCHEDULER",
                      kind, begin, begin, primitive, regions, units))

    if path == PLAN_PATH and name in ("scratch_pick", "pick_return_queue"):
        expression = "plan_scratch.counts[priority] != 0" if name == "scratch_pick" else "head.generation != 0"
        fragment = tuple(token.value for token in positional_tokens(expression))
        matches = [index for index in range(len(values)) if values[index:index + len(fragment)] == fragment]
        if len(matches) != 1:
            raise ValueError("OBJ02 priority probe lost its exact emptiness comparison")
        first = matches[0]
        begin, end = positions[first].start, positions[first + len(fragment) - 1].end
        regions = _original_regions(values, stacks, closings, stacks[first])
        sites.append((
            "obj02-priority-" + name, "compare", "work", "KERNEL_SCHEDULER",
            "COMPARISON", begin, end, body[begin:end], regions, 1,
        ))
    for index, value in enumerate(values):
        if path == PLAN_PATH and name in ("clear_return_candidate", "publish_return_candidate"):
            field = "bytes" if name == "clear_return_candidate" else "destination"
            if value != field or values[index + 1:index + 2] != ("[",):
                continue
            closing = _matching(values, index + 1, "[", "]")
            if values[closing + 1:closing + 2] == ("=",):
                store("CLEARED_BYTE" if field == "bytes" else "COPIED_BYTE",
                      index, values.index(";", closing) + 1)
        if path == PLAN_PATH and name in ("scratch_remove", "scratch_head") and value == "slots":
            first = index - 2
            if values[first:index] != ("plan_scratch", "."):
                continue
            closing = _matching(values, index + 1, "[", "]")
            closing = _matching(values, closing + 1, "[", "]")
            if values[closing + 1:closing + 2] == ("=",):
                store("QUEUE_RECORD_MOVE", first, values.index(";", closing) + 1)
        if path == APPLY_PATH and name != "plans_equal" and value in (
            "runtime_flags", "remaining_counter_ticks", "ready_linked", "ready_next",
            "ready_head", "ready_tail", "current_thread", "reschedule_pending",
            "primary_stack_bottom", "primary_stack_top",
        ):
            after = index + 1
            if values[after:after + 1] == ("[",):
                after = _matching(values, after, "[", "]") + 1
            if values[after:after + 1] in (("=",), ("|=",), ("&=",)):
                store("COPY_UNIT", index, values.index(";", after) + 1)
    if len({site[0] for site in sites}) != len(sites):
        raise ValueError("OBJ02 primitive observations have duplicate identities")
    return tuple(sites)


def primitive_shape(path, name, body):
    from tests.host.object_hart_validation_source import _primitive_statement

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


def observe_source(root, path, text, stage):
    from tests.host.selected_release_handoff_source import _render_body

    validate_retained_source(root, path, text, stage)
    if path not in NATIVE_SOURCES or "MICROS_OBJ02_OBSERVE_VALUE" in text:
        raise ValueError("OBJ02 observation has an unknown or already observed subject")
    edits = []
    contracts = observation_contracts(str(Path(root).resolve()), path, stage)
    for name, span in function_spans(text).items():
        body = text[span.body_start:span.body_end]
        if name not in contracts or primitive_shape(path, name, body) != contracts[name]:
            raise ValueError("OBJ02 observation changed its independent primitive inventory")
        rendered = _render_body(body, body_sites(path, name, body))
        rendered = rendered.replace("MICROS_MEM05_OBSERVE_VALUE", "MICROS_OBJ02_OBSERVE_VALUE")
        edits.append((span.body_start, span.body_end, rendered))
    for first, last, rendered in sorted(edits, reverse=True):
        text = text[:first] + rendered + text[last:]
    return OBSERVATION_PREFIX + text


def project_observations(root, path, observed, stage, *, original):
    if observed != observe_source(root, path, original, stage):
        raise ValueError("OBJ02 source has a moved, changed or missing work observation")
    return original


@lru_cache(maxsize=128)
def observation_contracts(root, path, stage):
    if stage not in ("baseline", "current") or path not in NATIVE_SOURCES:
        raise ValueError("OBJ02 observation contract has an unknown stage or owner")
    original = function_records(fixed_source(root, path))
    removed = REMOVED_FUNCTIONS.get(path, ()) if stage == "current" else ()
    result = {name: primitive_shape(path, name, record.body)
              for name, record in original.items() if name not in removed}
    if stage == "current":
        result.update((name, primitive_shape(path, name, body))
                      for name, body in CURRENT_BODIES.get(path, {}).items())
    return result


def candidate_paths():
    paths = (*SOURCE_ROLES["production_paths"], *SOURCE_ROLES["fixed_adapters"], *SOURCE_ROLES["unchanged_candidates"])
    if len(paths) != 28 or len(set(paths)) != 28:
        raise ValueError("OBJ02 direct candidate closure differs from its twenty-eight paths")
    return tuple(sorted(paths))


def read_declarations(root):
    import ast
    from tests.host.evidence_authority_source import literal_value, regular_source, single_assignment

    result = {}
    for path, names in (
        (AUTHORITY_PATH, ("DESIGN_PINS", "ORACLE_RULES")),
        (SOURCE_PATH, DECLARATION_NAMES),
    ):
        tree = ast.parse(regular_source(Path(root), path).read_text())
        result.update((name, literal_value(single_assignment(tree, name).value)) for name in names)
    return result


def native_assertion_records(root):
    from tests.host.evidence_authority_types import digest
    from tests.host.selected_address_space_source import _source_skeleton

    root = str(Path(root).resolve())
    contracts = current_function_contracts(root)
    rows = []

    def add(kind, path, policy, name=""):
        scope = "OBJ02/" + kind + "/" + path + ("/" + name if name else "")
        rows.append((scope, path, (SOURCE_PATH,), policy))

    for path in sorted((*NATIVE_SOURCES, *NATIVE_HEADERS, *TARGET_PATHS, "kernel/scheduler/scheduler_runtime_internal.h")):
        original = fixed_source(root, path)
        add("baseline-input", path, {"comparison": "exact-file", "sha256": hashlib.sha256(original.encode()).hexdigest()})
        if path in ("include/micros/scheduler_core.h", "kernel/scheduler/scheduler_runtime_internal.h"):
            add("header", path, {"comparison": "declared-header", "sha256": hashlib.sha256(declared_header(root, path).encode()).hexdigest()})
        elif path in CURRENT_BODIES or path in TARGET_PATHS:
            target = path in TARGET_PATHS
            declared = declared_target_source(root, path) if target else original
            before = function_records(declared)
            names = set(before) if target else set(before).difference(REMOVED_FUNCTIONS.get(path, ())) | set(CURRENT_BODIES[path])
            add("definitions", path, {"comparison": "definition-set", "expected": tuple(sorted(names))})
            add("skeleton", path, {"comparison": "source-skeleton",
                                  "expected": _source_skeleton(declared) if target else expected_skeleton(root, path)})
            for name in sorted(names):
                key = path + "/" + name
                if key in contracts:
                    policy = {"comparison": "current-signature-guards-callees", "expected": contracts[key][:3]}
                else:
                    record = before[name]
                    policy = {"comparison": "retained-or-declared-signature-guards-body",
                              "sha256": digest((record.signature, record.guards, record.body))}
                add("function", path, policy, name)
        else:
            add("current-input", path, {"comparison": "exact-file", "sha256": hashlib.sha256(original.encode()).hexdigest()})
        if path in NATIVE_SOURCES:
            for stage in ("baseline", "current"):
                for name, declaration in observation_contracts(root, path, stage).items():
                    add(stage + "-observation", path, {"comparison": "primitive-coverage", "expected": declaration}, name)
        if path in TARGET_PATHS:
            for stage, source in (("baseline", original), ("current", declared_target_source(root, path))):
                expected = extend_target_observations(path, source)
                add(stage + "-target-observation", path, {
                    "comparison": "exact-passive-forward-and-inverse",
                    "sha256": hashlib.sha256(expected.encode()).hexdigest(),
                })
    for path in SOURCE_ROLES["fixed_adapters"]:
        expected = extend_legacy_adapter(root, path, fixed_source(root, path))
        add("adapter", path, {"comparison": "exact-fixed-forward-and-inverse",
                             "sha256": hashlib.sha256(expected.encode()).hexdigest()})
    for path in SOURCE_ROLES["unchanged_candidates"]:
        add("unchanged-candidate", path, {"comparison": "exact-file",
                                        "sha256": hashlib.sha256(fixed_source(root, path).encode()).hexdigest()})
    return tuple(sorted(rows))


def native_binding_facts(root, family, relative):
    import ast
    from tests.host.evidence_authority_source import literal_value, regular_source, single_assignment
    from tests.host.evidence_authority_types import AuthorityClass, Fact, digest

    root = Path(root).resolve()
    if family != NATIVE_FAMILY or relative != BINDINGS_PATH:
        raise ValueError("OBJ02 source predicates have an unknown literal owner")
    tree = ast.parse(regular_source(root, relative).read_text())
    for node in tree.body:
        if isinstance(node, ast.Expr) and isinstance(node.value, ast.Constant) and type(node.value.value) is str:
            continue
        if (
            not isinstance(node, ast.Assign) or len(node.targets) != 1
            or not isinstance(node.targets[0], ast.Name)
            or node.targets[0].id not in ("BINDING_REFERENCE", "NATIVE_SOURCE_BINDINGS")
        ):
            raise ValueError("OBJ02 source predicates are not closed literal declarations")
    reference = literal_value(single_assignment(tree, "BINDING_REFERENCE").value)
    rows = literal_value(single_assignment(tree, "NATIVE_SOURCE_BINDINGS").value)
    pins = read_declarations(root)["DESIGN_PINS"]
    if reference != (
        ("policy", pins["design_commit"], pins["design_tree"]),
        ("OBJ02", pins["runtime_reference"], pins["runtime_tree"]),
    ):
        raise ValueError("OBJ02 source predicates borrowed another runtime or design")
    if (
        type(rows) is not tuple or rows != native_assertion_records(root)
        or digest(rows) != SOURCE_PROJECTION.get("native_bindings_sha256")
        or len(rows) != SOURCE_PROJECTION.get("native_binding_count")
    ):
        raise ValueError("OBJ02 source predicates differ from the independently frozen complete declaration")
    facts = []
    consumers = tuple(f"B{index:02d}" for index in range(1, 13)) + ("B14",)
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
    from tests.host.evidence_authority_types import AuthorityClass, Fact, digest

    if owner not in (SOURCE_PATH, AUTHORITY_PATH):
        raise ValueError("OBJ02 declaration has an unknown owner")
    values = read_declarations(root)
    pins, roles = values["DESIGN_PINS"], values["SOURCE_ROLES"]
    references = tuple(pins[name] for name in ("design_document", "adr_document", "runtime_adr_document", "research_document"))
    consumers = tuple(f"B{index:02d}" for index in range(1, 13)) + ("B14",)

    def fact(kind, scope, classification, payloads, value):
        return Fact(owner + ":" + kind + ":" + scope, FAMILY, owner, kind, scope, classification,
                    "B03", consumers, tuple(sorted({owner, *references, *payloads})), digest(value))

    if owner == AUTHORITY_PATH:
        return (
            fact("design-anchor", "OBJ02", AuthorityClass.TRANSPORT_IDENTITY, (), pins),
            *(fact("baseline-oracle", row[0], AuthorityClass.BASELINE_ORACLE, roles["immutable_additions"], row)
              for row in values["ORACLE_RULES"]),
        )
    projection = {name: value for name, value in values.items() if name not in (
        "DESIGN_PINS", "ORACLE_RULES", "SOURCE_ROLES", "RECIPE_ADMISSION", "STATE_WORK_CONTRACT",
    )}
    return (
        fact("source-membership", "OBJ02", AuthorityClass.PERMANENT_INVARIANT,
             (*candidate_paths(), *roles["immutable_additions"], *roles["immutable_adapters"]), roles),
        fact("artifact-contract", "OBJ02-recipes", AuthorityClass.ARTIFACT_CONTRACT,
             ("CMakeLists.txt", "CMakePresets.json", *roles["immutable_additions"],
              "include/micros/work_observation.h", "tests/work/work_counts.h", "tests/work/work_counts.c"),
             values["RECIPE_ADMISSION"]),
        fact("state-work-contract", "OBJ02", AuthorityClass.PERMANENT_INVARIANT,
             (*roles["production_paths"], *roles["immutable_additions"]), values["STATE_WORK_CONTRACT"]),
        fact("source-projection", "OBJ02", AuthorityClass.TRANSPORT_IDENTITY,
             (*candidate_paths(), *roles["immutable_additions"], "tests/host/c_source_records.py",
              "tests/host/work_count_lexical.py"), projection),
    )


def _current_authority(root):
    from tests.host.evidence_authority_scope import identity_required

    keys = SOURCE_PROJECTION["retirement_keys"]
    return len(keys) == 58 and not identity_required(root, keys[0])


def historical_identity_required(root, key):
    from tests.host.evidence_authority_scope import identity_required, source_bytes

    if key not in SOURCE_PROJECTION["retirement_keys"]:
        return None
    if identity_required(root, key):
        return True
    for path in SOURCE_ROLES["production_paths"]:
        validate_retained_source(root, path, source_bytes(Path(root), Path(root) / path).decode(), "current")
    return False


def legacy_native_admission(root, family, path, text, stage):
    from tests.host.work_count_types import Stage

    if stage not in ("baseline", "current", Stage.RED, Stage.GREEN) or family not in (
        "MEM01", "MEM02", "MEM03", "MEM04", "MEM05", "OBJ01",
    ):
        raise ValueError("OBJ02 inherited source admission has an unknown family or stage")
    if stage in ("baseline", Stage.RED) or path not in SOURCE_ROLES["production_paths"] or not _current_authority(root):
        return frozenset()
    validate_retained_source(root, path, text, "current")
    prefix = family + "/"
    return frozenset(
        scope for key in SOURCE_PROJECTION["retirement_keys"]
        if ":native-source-admission:" in key
        for scope in (key.split(":native-source-admission:", 1)[1],)
        if scope.startswith(prefix) and (scope.endswith("/" + path) or "/" + path + "/" in scope)
    )


def legacy_source_projection(root, family, path, text, stage):
    from tests.host.work_count_types import Stage

    if stage in ("current", Stage.GREEN) and path in TARGET_PATHS and _current_authority(root):
        validate_retained_source(root, path, text, "current")
        return project_target_observations(path, text) if TARGET_INCLUDE in text else text
    return text


def legacy_replaced_functions(root, family, path, text, stage):
    from tests.host.work_count_types import Stage

    if stage in ("baseline", Stage.RED) or path not in CURRENT_BODIES or not _current_authority(root):
        return frozenset()
    validate_retained_source(root, path, text, "current")
    return frozenset((*CURRENT_BODIES[path], *REMOVED_FUNCTIONS.get(path, ())))


LEGACY_VALUE_PREFIX = (
    '#include "micros/work_observation.h"\n'
    "#define MICROS_OBJ02_OBSERVE_VALUE(site, owner, kind, value) "
    "(micros_test_work_record((owner), (kind), 1), (value))\n"
)


def legacy_observe_source(root, family, path, text, stage, channel="aggregate"):
    from tests.host import object_hart_validation_source as objects
    from tests.host import selected_release_handoff_source as release
    from tests.host.selected_release_handoff_source import _render_body
    from tests.host.work_count_types import Stage

    if family not in ("MEM01", "MEM02", "MEM03", "MEM04", "MEM05", "OBJ01"):
        raise ValueError("OBJ02 legacy observations have an unknown family")
    if stage in ("baseline", Stage.RED) or path not in CURRENT_BODIES or not _current_authority(root):
        return None
    validate_retained_source(root, path, text, "current")
    backend = objects if family == "OBJ01" else release if family == "MEM05" else release._legacy_backend(family)
    channels = (
        backend.STATE_WORK_CONTRACT["channels"] if family == "MEM03"
        else backend.CHANNELS if family in ("MEM04", "MEM05", "OBJ01") else ("aggregate",)
    )
    if channel not in (*channels, "ordinary"):
        raise ValueError("OBJ02 legacy observations have an unknown channel")
    if channel == "ordinary":
        return text
    spans = function_spans(text)
    for name in sorted(spans, key=lambda value: spans[value].body_start, reverse=True):
        span = spans[name]
        body = text[span.body_start:span.body_end]
        changed = name in CURRENT_BODIES[path]
        sites = body_sites(path, name, body) if changed else objects.body_sites(path, name, body)
        if channel != "aggregate" and not (family in ("MEM04", "MEM05") and channel == "context"):
            sites = ()
        rendered = _render_body(body, sites).replace("MICROS_MEM05_OBSERVE_VALUE", "MICROS_OBJ02_OBSERVE_VALUE")
        text = text[:span.body_start] + rendered + text[span.body_end:]
    prefix = backend.NATIVE_PREFIX if family == "MEM04" else backend.OBSERVATION_PREFIX
    return prefix + LEGACY_VALUE_PREFIX + text


def project_legacy_observations(root, family, path, text, stage, channel="aggregate"):
    from tests.host.evidence_authority_scope import source_bytes
    from tests.host.work_count_types import Stage

    if stage in ("baseline", Stage.RED) or path not in CURRENT_BODIES or not _current_authority(root):
        return None
    original = source_bytes(Path(root), Path(root) / path).decode()
    if text != legacy_observe_source(root, family, path, original, stage, channel):
        raise ValueError("OBJ02 legacy observation changed or omitted actual work")
    return original


def project_directory_source(root, logical, raw):
    from tests.host.evidence_authority_scope import source_bytes
    from tests.host.kernel_layout_source import KERNEL_FILE_FACTS
    from tests.host.stack_source_projection import project_stack_source

    path = KERNEL_FILE_FACTS[logical][0]
    report = "kernel/tests/scheduler/scheduler_test_finish.c"
    if path not in (*TARGET_PATHS, report) or not _current_authority(root):
        return raw
    actual = source_bytes(Path(root), Path(root) / path).decode()
    checked = project_stack_source(path, actual) if "MICROS_TEST_STACK_" in actual else actual
    if checked.encode() != raw:
        raise ValueError("OBJ02 directory projection does not name its actual qualified source")
    if path == report:
        projected = project_legacy_adapter(root, path, actual)
        return (project_stack_source(path, projected) if "MICROS_TEST_STACK_" in projected else projected).encode()
    validate_retained_source(root, path, actual, "current")
    return project_target_observations(path, actual).encode()


def current_delta_active(root):
    from tests.host.evidence_authority_scope import source_bytes

    if not _current_authority(root):
        return False
    for path in ("include/micros/scheduler_core.h", PLAN_PATH, APPLY_PATH):
        validate_retained_source(root, path, source_bytes(Path(root), Path(root) / path).decode(), "current")
    return True


def _legacy_changes(path):
    edits = []
    if path == "kernel/tests/scheduler/scheduler_test_finish.c":
        edits.extend((
            ('#include "micros/stack_observation.h"\n',
             '#include "micros/stack_observation.h"\n' + TARGET_INCLUDE),
            ("    report_scheduler_success();",
             "    if (!micros_obj02_return_report()) {\n        goto failure;\n    }\n    report_scheduler_success();"),
        ))
    elif path == "tests/host/kernel_layout_source.py":
        anchor = "    required = diagnostic_identity_required(\n"
        edits.append((anchor,
                      "    from tests.host.common_scheduler_return_source import project_directory_source\n\n"
                      "    raw = project_directory_source(root, logical, raw)\n" + anchor))
    elif path == "tests/host/object_hart_validation_source.py":
        header = "def historical_identity_required(root, key):\n"
        edits.append((header, header +
                      "    from tests.host.common_scheduler_return_source import historical_identity_required as common_required\n\n"
                      "    common = common_required(root, key)\n    if common is not None:\n        return common\n"))
        for name, arguments, function, call, variable in (
            ("native_assertion_admission", "root, family, path, text, stage", "legacy_native_admission",
             "root, family, path, text, stage", "admitted"),
            ("legacy_replaced_functions", "root, family, path, text, stage", "legacy_replaced_functions",
             "root, family, path, text, stage", "admitted"),
            ("legacy_observe_source", 'root, family, path, text, stage, channel="aggregate"', "legacy_observe_source",
             "root, family, path, text, stage, channel", "observed"),
            ("project_legacy_observations", 'root, family, path, text, stage, channel="aggregate"', "project_legacy_observations",
             "root, family, path, text, stage, channel", "observed"),
        ):
            header = "def " + name + "(" + arguments + "):\n"
            optional = variable == "observed"
            edits.append((header, header +
                          "    from tests.host.common_scheduler_return_source import " + function + " as common_admission\n\n"
                          "    " + variable + " = common_admission(" + call + ")\n"
                          "    if " + variable + (" is not None" if optional else "") + ":\n"
                          "        return " + variable + "\n"))
        header = "def validate_retained_source(root, path, text, stage):\n"
        edits.append((header, header +
                      "    from tests.host.common_scheduler_return_source import CURRENT_BODIES, legacy_native_admission\n\n"
                      '    retired = legacy_native_admission(root, "OBJ01", path, text, stage)\n'))
        anchor = '    if stage == "baseline" or path not in (*MUTABLE_FUNCTIONS, "kernel/scheduler/scheduler_core_private.h"):\n'
        edits.append((anchor, anchor +
                      '        if "OBJ01/current-input/" + path in retired:\n            return\n'))
        edits.append((
            "    if set(after) != expected_names:\n",
            '    if "OBJ01/definitions/" + path not in retired and set(after) != expected_names:\n',
        ))
        anchor = "    for name, record in after.items():\n        key = path + \"/\" + name\n"
        edits.append((anchor, anchor +
                      '        if "OBJ01/function/" + path + "/" + name in retired or retired and name in CURRENT_BODIES.get(path, {}):\n'
                      "            continue\n"))
        edits.append((
            "    if _source_skeleton(text) != expected_skeleton(root, path):\n",
            '    if "OBJ01/skeleton/" + path not in retired and _source_skeleton(text) != expected_skeleton(root, path):\n',
        ))
        header = "def validate_observation_contract(root, path, name, body, stage):\n"
        edits.append((header, header +
                      "    from tests.host.common_scheduler_return_source import legacy_observation_contract\n\n"
                      "    if legacy_observation_contract(root, path, name, body, stage):\n        return\n"))
        header = "def observe_source(root, path, text, channel, stage):\n"
        edits.append((header, header +
                      "    from tests.host.common_scheduler_return_source import legacy_observe_source\n\n"
                      '    observed = legacy_observe_source(root, "OBJ01", path, text, stage, channel)\n'
                      "    if observed is not None:\n        return observed\n"))
        for name, args, call in (
            ("project_legacy_adapter", "root, path, text", "root, path, text"),
            ("project_cmake", "text", "text"),
        ):
            header = "def " + name + "(" + args + "):\n"
            edits.append((header, header +
                          "    from tests.host.common_scheduler_return_source import " + name + " as project_common\n\n"
                          "    text = project_common(" + call + ")\n"))
    elif path == "tests/host/selected_address_space_source.py":
        header = "def validate_retained_source(root, path, text, stage):\n"
        edits.append((header, header +
                      "    from tests.host.common_scheduler_return_source import legacy_source_projection\n\n"
                      '    text = legacy_source_projection(root, "MEM04", path, text, stage)\n'))
    elif path == "tests/host/test_kernel_object_source_layout.py":
        header = "    def test_public_runtime_architecture_driver_and_neighbor_artifacts_are_byte_exact(self):\n"
        edits.append((header, header +
                      "        from tests.host.common_scheduler_return_source import historical_identity_required as common_required\n\n"))
        anchor = '                key = "tests/host/test_kernel_object_source_layout.py:data-entry:STABLE_HASHES/" + path\n'
        edits.append((anchor, anchor + "                if common_required(ROOT, key) is False:\n                    continue\n"))
    elif path == "tests/host/test_scheduler_source_layout.py":
        edits.extend(_scheduler_gate_changes())
    return tuple(edits)


def legacy_observation_contract(root, path, name, body, stage):
    if stage != "current" or path not in CURRENT_BODIES or not _current_authority(root):
        return False
    if name not in CURRENT_BODIES[path]:
        return False
    expected = observation_contracts(str(Path(root).resolve()), path, "current")
    if name not in expected or primitive_shape(path, name, body) != expected[name]:
        raise ValueError("OBJ02 inherited observation changed its independent primitive contract")
    return True


def _scheduler_gate_changes():
    edits = [
        (
            "    def test_all_original_owners_and_pending_bodies_are_frozen(self) -> None:\n",
            "    def test_all_original_owners_and_pending_bodies_are_frozen(self) -> None:\n"
            "        from tests.host.common_scheduler_return_source import current_delta_active\n\n",
        ),
        (
            '                        if family == 0 and name == "queue_enqueue_tail" and not historical_identity_required(REPOSITORY_ROOT, key):\n',
            '                        if family == 0 and name == "scratch_load" and current_delta_active(REPOSITORY_ROOT):\n'
            "                            self.assertEqual([], matches)\n                            continue\n"
            '                        if family == 0 and name in ("scratch_remove", "scratch_head", "scratch_tail", "scratch_pick") and not historical_identity_required(REPOSITORY_ROOT, key):\n'
            "                            self.assertEqual([], matches)\n                            continue\n"
            '                        if family == 0 and name == "queue_enqueue_tail" and not historical_identity_required(REPOSITORY_ROOT, key):\n',
        ),
        (
            '                if path == "kernel/kernel_objects.c" and not historical_identity_required(REPOSITORY_ROOT, key):\n',
            '                if path in ("kernel/kernel_objects.c", "include/micros/scheduler_core.h") and not historical_identity_required(REPOSITORY_ROOT, key):\n',
        ),
        (
            '            if family == 0:\n                workspace = [(path, text) for path, text in declarations if re.search(r"\\bplan_scratch\\b", text)]\n',
            '            if family == 0:\n'
            "                from tests.host.common_scheduler_return_source import current_delta_active\n\n"
            "                if current_delta_active(REPOSITORY_ROOT):\n"
            "                    self.assertEqual([], declarations)\n"
            '                    self.assertNotRegex("\\n".join(self.texts.get(path, "") for path in paths), r"\\b(?:plan_scratch|scheduler_plan_scratch)\\b")\n'
            "                    continue\n"
            '                workspace = [(path, text) for path, text in declarations if re.search(r"\\bplan_scratch\\b", text)]\n',
        ),
        (
            "                for role in roles:\n",
            "                from tests.host.common_scheduler_return_source import current_delta_active\n\n"
            '                if name == "micros_hart_plan_user_return" and current_delta_active(REPOSITORY_ROOT):\n'
            '                    roles = (r"preflight", r"repair", r"select|renew", r"finish|patch", r"publish")\n'
            "                for role in roles:\n",
        ),
    ]
    root = Path(__file__).resolve().parents[2]
    original = fixed_source(str(root), "tests/host/test_scheduler_source_layout.py")
    first = original.index('        planner = self.phases(0, "micros_hart_plan_user_return")\n')
    last = original.index(
        '        require_order(\n            self.original(0, "micros_hart_commit_user_return"),', first,
    )
    old = original[first:last]
    new = (
        "        from tests.host.common_scheduler_return_source import current_delta_active\n\n"
        "        if current_delta_active(REPOSITORY_ROOT):\n"
        "            require_order(\n"
        '                self.original(0, "micros_hart_plan_user_return"),\n'
        '                "clear_return_candidate", "preflight_return_plan", "repair_preempted_candidate",\n'
        '                "select_and_renew_candidate", "finish_return_queue", "publish_return_candidate",\n'
        "            )\n"
        "        else:\n"
        + "".join("    " + line if line.strip() else line for line in old.splitlines(keepends=True))
    )
    edits.append((old, new))
    return tuple(edits)


def extend_legacy_adapter(root, path, text):
    if path not in SOURCE_ROLES["fixed_adapters"] or text != fixed_source(str(Path(root).resolve()), path):
        raise ValueError("OBJ02 adapter lacks its complete fixed predecessor")
    for before, after in _legacy_changes(path):
        text = _replace_once(text, before, after)
    return text


def project_legacy_adapter(root, path, text):
    if path not in SOURCE_ROLES["fixed_adapters"]:
        return text
    marker = TARGET_INCLUDE if path.endswith(".c") else "tests.host.common_scheduler_return_source"
    if marker not in text:
        return text
    original = fixed_source(str(Path(root).resolve()), path)
    if text != extend_legacy_adapter(root, path, original):
        raise ValueError("OBJ02 adapter changed beyond its exact fixed transformation: " + path)
    return original


def native_products():
    return (("baseline", "ordinary"), ("baseline", "work"), ("current", "ordinary"), ("current", "work"))


def native_recipe():
    result = "".join(
        "    add_custom_command(\n"
        "        OUTPUT ${" + owner + "_NATIVE_OUTPUTS} APPEND\n"
        "        DEPENDS tests/host/common_scheduler_return_source.py\n"
        "            tests/host/common_scheduler_return_authority.py\n"
        "            tests/host/common_scheduler_return_native_source_bindings.py\n"
        "            tests/host/evidence_authority_scope.py\n"
        "    )\n"
        for owner in ("MEM01", "MEM02", "MEM03", "MEM04", "MEM05", "OBJ01")
    )
    result += (
        '    set(OBJ02_NATIVE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/common-scheduler-return")\n'
        "    set(OBJ02_NATIVE_INPUTS\n" + "".join("        " + path + "\n" for path in NATIVE_SOURCES) + "    )\n"
        "    set(OBJ02_NATIVE_HEADERS\n" + "".join("        " + path + "\n" for path in NATIVE_HEADERS) + "    )\n"
        "    set(OBJ02_NATIVE_OUTPUTS)\n"
    )
    for index, (stage, channel) in enumerate(native_products()):
        root = "${OBJ02_NATIVE_ROOT}/" + stage + "/" + channel
        result += (
            "    set(OBJ02_SOURCES_" + str(index) + ")\n"
            "    foreach(obj02_source IN LISTS OBJ02_NATIVE_INPUTS)\n"
            "        list(APPEND OBJ02_SOURCES_" + str(index) + ' "' + root + '/${obj02_source}")\n'
            "    endforeach()\n"
            "    list(APPEND OBJ02_NATIVE_OUTPUTS ${OBJ02_SOURCES_" + str(index) + "})\n"
            "    foreach(obj02_header IN LISTS OBJ02_NATIVE_HEADERS)\n"
            '        list(APPEND OBJ02_NATIVE_OUTPUTS "' + root + '/${obj02_header}")\n'
            "    endforeach()\n"
        )
    result += (
        "    add_custom_command(\n"
        "        OUTPUT ${OBJ02_NATIVE_OUTPUTS}\n"
        "        JOB_POOL mem03_source_proof\n"
        '        COMMAND "${MICROS_PYTHON}" ${MICROS_PYTHON_ARGS}\n'
        '            "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_common_scheduler_return_fixture.py"\n'
        "        DEPENDS ${OBJ02_NATIVE_INPUTS} ${OBJ02_NATIVE_HEADERS}\n"
        + "".join("            " + path + "\n" for path in SOURCE_ROLES["immutable_additions"])
        + "            tests/host/object_hart_validation_source.py\n"
        "            tests/host/evidence_authority_scope.py\n"
        "            tests/host/c_source_records.py\n"
        "            tests/host/work_count_lexical.py\n"
        "            tools/validation_context.py\n"
        "            tools/validation_inputs.py\n"
        '        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"\n'
        "        VERBATIM\n"
        "    )\n"
    )
    for index, ((stage, channel), target) in enumerate(zip(native_products(), RECIPE_ADMISSION["native_targets"], strict=True)):
        root = "${OBJ02_NATIVE_ROOT}/" + stage + "/" + channel
        result += (
            "    add_executable(" + target + " ${OBJ02_SOURCES_" + str(index) + "}\n"
            "        tests/host/common_scheduler_return_test.c\n"
            + ("        tests/work/work_counts.c\n" if channel == "work" else "")
            + "    )\n"
            "    target_include_directories(" + target + " PRIVATE\n"
            '        "' + root + '" "' + root + '/include"\n'
            '        "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/include")\n'
            "    target_compile_features(" + target + " PRIVATE c_std_17)\n"
            "    target_compile_options(" + target
            + " PRIVATE -Wall -Wextra -Werror -Wpedantic -Wmissing-prototypes -Wstrict-prototypes -fno-builtin)\n"
            "    target_compile_definitions(" + target + " PRIVATE MICROS_OBJ02_NATIVE_STAGE=" + str(int(stage == "current")) + ")\n"
            '    if(MICROS_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")\n'
            "        target_compile_options(" + target + " PRIVATE -fno-omit-frame-pointer -fsanitize=address,undefined)\n"
            "        target_link_options(" + target + " PRIVATE -fsanitize=address,undefined)\n"
            "    endif()\n"
        )
        if channel == "work":
            result += "    target_compile_definitions(" + target + " PRIVATE MICROS_TEST_WORK_COUNTS=1)\n"
        if index == 3:
            result += (
                "    target_compile_definitions(" + target + " PRIVATE\n"
                "        MICROS_COMMON_SCHEDULER_RETURN_LAUNCHER=1\n"
                '        "MICROS_COMMON_SCHEDULER_RETURN_PYTHON=\\"${MICROS_PYTHON}\\""\n'
                '        "MICROS_COMMON_SCHEDULER_RETURN_DRIVER=\\"${CMAKE_CURRENT_SOURCE_DIR}/tools/check_common_scheduler_return.py\\""\n'
                '        "MICROS_COMMON_SCHEDULER_RETURN_ROOT=\\"${CMAKE_CURRENT_SOURCE_DIR}\\""\n'
                "    )\n"
            )
    return result + (
        "    add_test(NAME common-scheduler-return COMMAND common_scheduler_return_test\n"
        + "".join('        "$<TARGET_FILE:' + target + '>"\n' for target in RECIPE_ADMISSION["native_targets"][:-1])
        + "    )\n"
        "    set_tests_properties(common-scheduler-return PROPERTIES LABELS slow)\n"
        "    add_dependencies(test-unit " + " ".join(RECIPE_ADMISSION["native_targets"]) + ")\n\n"
    )


TARGET_RECIPE = """    target_sources(micros PRIVATE
        tests/qemu/common_scheduler_return_work.c
        tests/work/work_counts.c
    )
    set_source_files_properties(
        tests/qemu/common_scheduler_return_work.c tests/work/work_counts.c
        PROPERTIES COMPILE_DEFINITIONS MICROS_TEST_WORK_COUNTS=1
    )
"""
TARGET_REPORT_ANCHOR = """        test-qemu-scheduler
        COMMAND
            "${MICROS_PYTHON}"
            ${MICROS_PYTHON_ARGS}
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu_smoke.py"
"""


def extend_target_recipe(text):
    anchor = "            ${MICROS_SCHEDULER_QEMU_TEST_SOURCES}\n    )\n"
    if TARGET_RECIPE in text:
        raise ValueError("OBJ02 target observer is already registered")
    text = _replace_once(text, anchor, anchor + TARGET_RECIPE)
    return _replace_once(text, TARGET_REPORT_ANCHOR, TARGET_REPORT_ANCHOR + "            --report-transcript\n")


def extend_cmake(text):
    root = Path(__file__).resolve().parents[2]
    original = fixed_source(str(root), "CMakeLists.txt")
    if text != original:
        raise ValueError("OBJ02 recipe lacks its complete fixed predecessor")
    anchor = '    include("${CMAKE_CURRENT_SOURCE_DIR}/tests/stack/StackEvidence.cmake")\n'
    return extend_target_recipe(_replace_once(text, anchor, native_recipe() + anchor))


def project_cmake(text):
    if "OBJ02_NATIVE" not in text and "tests/qemu/common_scheduler_return_work.c" not in text:
        return text
    anchor = '    include("${CMAKE_CURRENT_SOURCE_DIR}/tests/stack/StackEvidence.cmake")\n'
    text = _replace_once(text, native_recipe() + anchor, anchor)
    target_anchor = "            ${MICROS_SCHEDULER_QEMU_TEST_SOURCES}\n    )\n"
    text = _replace_once(text, target_anchor + TARGET_RECIPE, target_anchor)
    return _replace_once(text, TARGET_REPORT_ANCHOR + "            --report-transcript\n", TARGET_REPORT_ANCHOR)


def validate_scaffold(root):
    from tests.host.common_scheduler_return_evidence import native_case_specs
    from tests.host.evidence_authority_types import digest
    from tools.stack_artifact_files import read_regular

    root = Path(root).resolve()
    values = read_declarations(root)
    if any(values[name] != globals()[name] for name in DECLARATION_NAMES):
        raise ValueError("OBJ02 loaded declarations differ from their current literal source")
    contracts = current_function_contracts(str(root))
    if (
        len(NATIVE_SOURCES) != 18 or len(NATIVE_HEADERS) != 8
        or len(contracts) != SOURCE_PROJECTION["current_function_count"]
        or digest(contracts) != SOURCE_PROJECTION["current_function_contracts_sha256"]
    ):
        raise ValueError("OBJ02 source closure or independent current function contracts changed")
    for stage in ("baseline", "current"):
        cases = native_case_specs(stage)
        if len(cases) != STATE_WORK_CONTRACT[stage + "_cases"] or digest(cases) != STATE_WORK_CONTRACT[stage + "_cases_sha256"]:
            raise ValueError("OBJ02 independent native corpus changed")
    native_binding_facts(root, NATIVE_FAMILY, BINDINGS_PATH)
    original = fixed_source(str(root), "CMakeLists.txt")
    if read_regular(root / "CMakeLists.txt")[0].decode() != extend_cmake(original):
        raise ValueError("OBJ02 full-only native and test-only target recipes are absent or altered")
    expected = SOURCE_PROJECTION["adapter_sha256"]
    if set(expected) != set(SOURCE_ROLES["fixed_adapters"]):
        raise ValueError("OBJ02 frozen adapter declaration is incomplete")
    for path in SOURCE_ROLES["fixed_adapters"]:
        actual = read_regular(root / path)[0]
        if (
            actual.decode() != extend_legacy_adapter(root, path, fixed_source(str(root), path))
            or hashlib.sha256(actual).hexdigest() != expected[path]
        ):
            raise ValueError("OBJ02 frozen adapter changed: " + path)
    expected = SOURCE_PROJECTION["unchanged_sha256"]
    if set(expected) != set(SOURCE_ROLES["unchanged_candidates"]):
        raise ValueError("OBJ02 unchanged candidate declaration is incomplete")
    for path in SOURCE_ROLES["unchanged_candidates"]:
        raw = read_regular(root / path)[0]
        if raw.decode() != fixed_source(str(root), path) or hashlib.sha256(raw).hexdigest() != expected[path]:
            raise ValueError("OBJ02 changed a retained runtime/evidence or literal input: " + path)
    for path in SOURCE_ROLES["immutable_additions"]:
        read_regular(root / path)

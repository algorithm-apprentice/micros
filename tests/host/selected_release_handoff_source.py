"""Closed MEM05 source, interface and observation declarations."""

import ast
from functools import lru_cache
import hashlib
from pathlib import Path
import re
import subprocess

from tests.host.c_source_records import function_records
from tests.host.evidence_authority_source import literal_value, regular_source, single_assignment
from tests.host.evidence_authority_types import AuthorityClass, Fact, digest
from tests.host.work_count_lexical import compound_layout, function_spans, positional_tokens
from tests.host.work_count_types import Stage


RUNTIME_REFERENCE = "abe05cdd7bced5e77e86c1ec12754d017f920cab"
RUNTIME_TREE = "2c34dac3c259a4975d1c213e3e14c18d8a075658"
FAMILY = "selected-release-handoff-baseline"
SOURCE_PATH = "tests/host/selected_release_handoff_source.py"
AUTHORITY_PATH = "tests/host/selected_release_handoff_authority.py"
HEADER_PATHS = (
    "include/micros/frame_ownership.h",
    "include/micros/frame_ownership_runtime.h",
    "kernel/memory/frame_ownership_internal.h",
    "kernel/memory/user_address_space_internal.h",
)

SOURCE_ROLES = {
    "base_membership_sha256": "a7c03cfb0b1e339aa3432e582f40837cc2ffcc5864855c1b5a044dbe86409b82",
    "production_paths": (
        "include/micros/frame_ownership.h",
        "include/micros/frame_ownership_runtime.h",
        "kernel/memory/bootstrap_memory.c",
        "kernel/memory/frame_ownership.c",
        "kernel/memory/frame_ownership_bootstrap.c",
        "kernel/memory/frame_ownership_handoff.c",
        "kernel/memory/frame_ownership_internal.h",
        "kernel/memory/frame_ownership_validation.c",
        "kernel/memory/frame_ownership_vm_claim.c",
        "kernel/memory/frame_ownership_vm_freeze.c",
        "kernel/memory/frame_ownership_vm_release.c",
        "kernel/memory/user_address_space.c",
        "kernel/memory/user_address_space_handoff.c",
        "kernel/memory/user_address_space_internal.h",
        "kernel/memory/user_address_space_mutation.c",
        "kernel/memory/user_address_space_runtime.c",
        "kernel/memory/user_address_space_validation.c",
    ),
    "fixed_adapters": (
        "kernel/tests/memory/address_space_handoff_test_runtime.c",
        "kernel/tests/memory/user_address_space_test_validation.c",
        "tests/host/bootstrap_allocation_source.py",
        "tests/host/kernel_layout_source.py",
        "tests/host/kernel_mapping_source.py",
        "tests/host/selected_address_space_source.py",
        "tests/host/selected_ownership_source.py",
        "tools/generate_kernel_mapping_fixture.py",
        "tools/generate_selected_ownership_fixture.py",
    ),
    "dependent_target_paths": (
        "kernel/tests/memory/address_space_handoff_test_runtime.c",
        "kernel/tests/memory/user_address_space_test_validation.c",
    ),
    "unchanged_candidates": (
        "tests/host/memory_native_source_bindings.py",
        "tests/host/selected_address_space_native_source_bindings.py",
        "tests/host/selected_ownership_native_source_bindings.py",
        "tests/host/selected_ownership_observation_bindings.py",
    ),
    "immutable_additions": (
        "tests/host/selected_release_handoff_test.c",
        "tests/host/selected_release_handoff_test.h",
        "tests/host/selected_release_handoff_source.py",
        "tests/host/selected_release_handoff_evidence.py",
        "tests/host/selected_release_handoff_authority.py",
        "tests/host/test_selected_release_handoff.py",
        "tools/generate_selected_release_handoff_fixture.py",
        "tools/check_selected_release_handoff.py",
    ),
    "immutable_adapters": (
        "CMakeLists.txt",
        "tests/host/evidence_authority_inventory.py",
        "tests/host/evidence_authority_scope.py",
        "tests/host/test_evidence_authority.py",
        "tests/host/test_evidence_authority_scope.py",
        "tests/host/test_python_host_runtime.py",
        "tests/host/selected_ownership_evidence.py",
        "tests/host/test_selected_ownership.py",
        "tests/host/selected_address_space_evidence.py",
        "tests/host/test_selected_address_space.py",
        "tests/host/test_kernel_address_space_source_layout.py",
        "tests/host/bootstrap_allocation_test.c",
        "tests/host/selected_ownership_test.c",
        "tests/host/selected_address_space_test.c",
        "tests/host/selected_address_space_test.h",
        "tests/host/frame_ownership_test_fixture.c",
        "tests/host/frame_ownership_test_internal.h",
        "tests/host/frame_ownership_test_bootstrap.c",
        "tests/host/frame_ownership_test_handoff.c",
        "tests/host/frame_ownership_test_model.c",
        "tests/host/frame_ownership_test_validation.c",
        "tests/host/vm_bootstrap_test_fixture.c",
        "tests/host/vm_bootstrap_test_internal.h",
    ),
}

RECIPE_ADMISSION = {
    "base_cmake_sha256": "45c4a122c949f59564ba7b552257fcd805981d1097016707701a4d7e04fbe59c",
    "legacy_cmake_sha256": "f657d8b24c20ec1a2447939feff5660fe3f7f10fe4ffdea25acd08a3ec5ee085",
    "native_source_members": (
        "kernel/memory/frame_allocator.c",
        "kernel/memory/bootstrap_memory.c",
        "kernel/memory/frame_ownership.c",
        "kernel/memory/frame_ownership_validation.c",
        "kernel/memory/frame_ownership_bootstrap.c",
        "kernel/memory/frame_ownership_handoff.c",
        "kernel/memory/frame_ownership_vm_claim.c",
        "kernel/memory/frame_ownership_vm_freeze.c",
        "kernel/memory/frame_ownership_vm_release.c",
        "kernel/objects/kernel_objects.c",
        "kernel/objects/kernel_object_state.c",
        "kernel/objects/kernel_object_identity.c",
        "kernel/objects/kernel_object_stack.c",
        "kernel/objects/kernel_object_process.c",
        "kernel/objects/kernel_object_thread.c",
        "kernel/objects/kernel_object_context.c",
        "kernel/objects/kernel_object_hart.c",
        "kernel/objects/kernel_object_validation.c",
        "kernel/objects/kernel_object_runtime.c",
        "kernel/scheduler/scheduler_core.c",
        "kernel/scheduler/scheduler_core_queue.c",
        "kernel/scheduler/scheduler_core_validate.c",
        "kernel/scheduler/scheduler_core_policy.c",
        "kernel/scheduler/scheduler_core_ipc.c",
        "kernel/scheduler/scheduler_core_ipc_transition.c",
        "kernel/scheduler/scheduler_core_return_plan.c",
        "kernel/scheduler/scheduler_core_return.c",
        "kernel/scheduler/scheduler_core_accounting.c",
        "kernel/scheduler/scheduler_context.c",
        "kernel/endpoint/endpoint.c",
        "kernel/endpoint/endpoint_codec.c",
        "kernel/endpoint/endpoint_identity.c",
        "kernel/endpoint/endpoint_registry.c",
        "kernel/endpoint/endpoint_publication.c",
        "kernel/endpoint/endpoint_privilege.c",
        "kernel/endpoint/endpoint_completion_validation.c",
        "kernel/endpoint/endpoint_queue_validation.c",
        "kernel/endpoint/endpoint_object_validation.c",
        "kernel/ipc/ipc_runtime.c",
        "kernel/memory/sv39.c",
        "kernel/memory/address_space.c",
        "kernel/memory/user_address_space.c",
        "kernel/memory/user_address_space_core.c",
        "kernel/memory/user_address_space_table.c",
        "kernel/memory/user_address_space_runtime.c",
        "kernel/memory/user_address_space_mapping.c",
        "kernel/memory/user_address_space_validation.c",
        "kernel/memory/user_address_space_mutation.c",
        "kernel/memory/user_address_space_activation.c",
        "kernel/vm/vm_handoff_core.c",
        "kernel/vm/vm_handoff_boot.c",
        "kernel/vm/vm_handoff_runtime.c",
        "kernel/tty/tty_handoff_core.c",
        "kernel/tty/tty_handoff_runtime_state.c",
        "kernel/tty/tty_handoff_runtime_validate.c",
        "kernel/boot/bootstrap_control_core.c",
        "kernel/memory/user_address_space_handoff.c",
        "kernel/execution/user_execution_validation.c",
    ),
    "native_header_count": 156,
    "native_header_membership_sha256": "f6d999c1bb6b9f5fc57e097211f88ac72beb51577b964e6e4a24a777f057e3c2",
    "native_targets": (
        "selected_release_handoff_aggregate_test",
        "selected_release_handoff_frames_test",
        "selected_release_handoff_plans_test",
        "selected_release_handoff_context_test",
        "selected_release_handoff_ordinary_test",
    ),
    "native_case": "selected-release-handoff",
    "native_tiers": ("test-unit",),
    "native_label": "slow",
    "native_standard": "c17",
    "native_sanitizers": ("address", "undefined"),
    "source_pool": "mem03_source_proof",
    "generator": "tools/generate_selected_release_handoff_fixture.py",
    "generator_output": "build/host-debug/selected-release-handoff",
    "driver": "tools/check_selected_release_handoff.py",
    "fixture_header": "tests/host/selected_release_handoff_test.h",
    "fixture_sources": ("tests/host/selected_release_handoff_test.c", "tests/work/work_counts.c"),
    "baseline_headers": "fixed-runtime-reference",
    "current_headers": "actual-current-source",
    "ordinary_observations": "absent",
}

STATE_WORK_CONTRACT = {
    "version": 1,
    "dimensions": (27, 14),
    "owner_mask": 11900,
    "channels": ("aggregate", "frames", "plans", "context"),
    "producer_base": 0x4d454d3035000000,
    "current_producer_offset": 256,
    "common_cases": 128,
    "case_inputs_sha256": "fe9e35bbc0521d251eebf129ae5ec594ffb0276f70ac1bb787226c81ffac3065",
    "frame_capacity": 262144,
    "bitmap_words": 4096,
    "process_capacity": 64,
    "thread_capacity": 128,
    "frame_links_bytes": 8,
    "process_index_bytes": 8,
    "selection_bytes": 49168,
    "handoff_summary_bytes": 1024,
    "target_workflows": ("test-qemu-user-address-space", "test-qemu-address-space-handoff"),
    "preservation": (
        "complete-typed-owner-and-availability-authority",
        "mixed-portable-generations-and-all-index-transitions",
        "same-call-complete-index-proof-and-original-all-free-diagnostic-counts",
        "selected-versus-complete-corruption-scope-and-byte-exact-failure-state",
        "two-process-pass-handoff-error-order-and-nonfailing-commit",
        "all-prior-native-cases-bounds-and-complete-target-stack-coverage",
        "sixteen-retained-root-audits-and-four-owned-replacement-boundaries",
    ),
}

SOURCE_PROJECTION = {
    "source_reference": "abe05cdd7bced5e77e86c1ec12754d017f920cab",
    "adapter_reference": "8f3b19ccae21a06345d05415708cd2dc1db32e76",
    "predecessor_facts_sha256": "916c05728bdc471ddde62b47376f0911d17046bda950b3c0a5da266615b38a19",
    "affected_facts_sha256": "7175cf5a99cb2a1bf711c09de4f6e295b35e7f43c6e62900ec8a293c4b5c1b10",
    "baseline_observations_sha256": "a482276914407bf11c0509e14b6a221fad3a12d4b492bbe26efbfc929bbeb591",
    "baseline_observation_functions": 566,
    "baseline_observation_sites": 532,
    "current_function_contracts_sha256": "29ba93010d249986f2ef60e918d0c6a9e48adc24e71631c4eb16a6db67070096",
    "target_adapter_sha256": {
        "kernel/tests/memory/address_space_handoff_test_runtime.c": "ad4b199c490a04309299386e9236cd61cbda0f74ae5a8650364e2bf191976821",
        "kernel/tests/memory/user_address_space_test_validation.c": "0ef23daceff48986d34a7b4ed742bb96f267dd19a15a31773a9049365ab346fd",
    },
    "immutable_adapter_sha256": {
        "tests/host/evidence_authority_scope.py": "f3301b974dc35366de47b83a4bac23b04a2360a53b1034166763aeba79cf2155",
        "tests/host/test_evidence_authority.py": "4f90b709aebffc92f6fddee7cf221b860efb5cd80099e01b596302516a17d7d6",
        "tests/host/test_evidence_authority_scope.py": "383105d42d190dc7e29891272ecb4a4cd0ab59d2514181ee21ce2848c035bbed",
        "tests/host/test_python_host_runtime.py": "94b6159deb9918add8350b434aa95f1355488ae33a109f48b4c9c95afc4fa93d",
        "tests/host/selected_ownership_evidence.py": "cf3614dc1103d4926f4bc68cb6a54e7d49250f5d76d78302b7f9861cfea00a42",
        "tests/host/test_selected_ownership.py": "70244946c0a8b9304b78c8f07a7a4bcaaa9f6957c66efb5d12e7ef7f7e083990",
        "tests/host/selected_address_space_evidence.py": "7aa0cd5aebb9816d0d1e300212ce75bb729b98f6ec9db3bdbe077a5bff461b9d",
        "tests/host/test_selected_address_space.py": "fd659dd1359d7d974c9240e2fe622c380c3d84dd814476df76c17ea69dc67174",
        "tests/host/test_kernel_address_space_source_layout.py": "7139314dd2a83aa7fa2f3c8d9d00e70e1ecf41dd8c0611afad5beb4570e1782c",
        "tests/host/bootstrap_allocation_test.c": "710ef90747d235f0709d726292731fdaa9f4dff79b5ca2ca20dff2178b908a15",
        "tests/host/selected_ownership_test.c": "eede15d92aa039dd6adf01695ee5be46cf7efbb238f3b2389fcb10e8d749949a",
        "tests/host/selected_address_space_test.c": "3833fb68ab07868910a216c9903f13a64671e6161612fce13e511f40286d7282",
        "tests/host/selected_address_space_test.h": "992cfb783e6e665b0813a541f2b27bbf4d52fd86ec614cea0bf6bc1ca7c2c1be",
        "tests/host/frame_ownership_test_fixture.c": "ac25dbebc37c32c99e5b64fd4e0b0cc97ed2084df07fbd6fd5ced702687eeded",
        "tests/host/frame_ownership_test_internal.h": "c146d9f95f7aa964d197c0fe8419cae044fcd8ef198c5a5f87a44c0241bcf7bc",
        "tests/host/frame_ownership_test_bootstrap.c": "e9714127267cf8ed98f84e0d820bc92efc67fffc275f2e774b516128379ee3b4",
        "tests/host/frame_ownership_test_handoff.c": "a1dbeb0d5b76bfbed707462c83d0d2b5e6c7b691dd18b4bd27da3eb440654c83",
        "tests/host/frame_ownership_test_model.c": "f822e4c65c2a921d1b1f6618402f0ea14ba93052b8176b228b0d87218bbee879",
        "tests/host/frame_ownership_test_validation.c": "131c1c7ca693a3f3f23767d92e72acc24320c0de0122c9ee10e79dcb842e1252",
        "tests/host/vm_bootstrap_test_fixture.c": "49587b6ed9625691e4c6913893389904d2d4e0ae9ffdcedf15069bf07f50afdf",
        "tests/host/vm_bootstrap_test_internal.h": "eafd733d023863d3e1ce3c9bb310d8ce020af4a1ccc9d53b0ca8cabae40ab53d",
    },
    "migrated_audit_callers": (
        "micros_user_address_space_release_page", "micros_user_address_space_destroy",
        "micros_user_address_space_prepare_wired_handoff", "micros_user_address_space_complete_wired_handoff",
    ),
    "plan_boundaries": (
        (
            "kernel/memory/user_address_space.c", "micros_user_address_space_destroy",
            "micros_user_address_space_collect_bootstrap_release_locked", 1,
        ),
        (
            "kernel/memory/user_address_space_handoff.c", "micros_user_address_space_complete_wired_handoff",
            "micros_user_address_space_collect_bootstrap_handoff_locked", 1,
        ),
        (
            "kernel/memory/user_address_space_handoff.c", "micros_user_address_space_prepare_wired_handoff",
            "micros_user_address_space_collect_bootstrap_handoff_locked", 1,
        ),
        (
            "kernel/memory/user_address_space_mutation.c", "micros_user_address_space_release_page",
            "inspect_page_release", 1,
        ),
    ),
    "mutable_functions": {
        "kernel/memory/frame_ownership.c": (),
        "kernel/memory/frame_ownership_validation.c": (
            "micros_frame_ownership_count_process", "micros_frame_ownership_validate",
            "micros_frame_ownership_validate_active", "micros_frame_ownership_vm_process_set_preflight",
        ),
        "kernel/memory/frame_ownership_bootstrap.c": (
            "micros_frame_ownership_allocate", "micros_frame_ownership_release",
            "collect_process_release_summary", "process_release_counts_are_valid",
            "commit_process_release", "micros_frame_ownership_release_process_set",
        ),
        "kernel/memory/frame_ownership_handoff.c": (
            "micros_frame_ownership_prepare_wired_process_user_set", "micros_frame_ownership_complete_handoff",
        ),
        "kernel/memory/frame_ownership_vm_claim.c": ("micros_frame_ownership_vm_claim_preflight",),
        "kernel/memory/frame_ownership_vm_freeze.c": ("micros_frame_ownership_vm_freeze_commit_prevalidated",),
        "kernel/memory/frame_ownership_vm_release.c": (
            "micros_frame_ownership_vm_release_transferable_preflight",
            "micros_frame_ownership_vm_release_process_commit_prevalidated",
        ),
        "kernel/memory/bootstrap_memory.c": (
            "micros_frame_ownership_runtime_prepare_wired_process_user_set",
            "micros_frame_ownership_runtime_release_process_set",
        ),
        "kernel/memory/user_address_space.c": (
            "clear_released_frames_or_panic", "micros_user_address_space_destroy",
        ),
        "kernel/memory/user_address_space_mutation.c": (
            "clear_release_bitmap", "scratch_bitmap_add", "acquire_scratch",
            "release_page_frames", "micros_user_address_space_release_page",
        ),
        "kernel/memory/user_address_space_runtime.c": (
            "micros_user_address_space_acquire_scratch", "micros_user_address_space_release_scratch",
            "micros_user_address_space_require_idle",
        ),
        "kernel/memory/user_address_space_validation.c": (),
        "kernel/memory/user_address_space_handoff.c": (
            "validate_handoff_processes", "micros_user_address_space_prepare_wired_handoff",
            "micros_user_address_space_complete_wired_handoff", "acquire_scratch",
        ),
    },
    "removed_functions": {
        "kernel/memory/user_address_space.c": (
            "scratch_bitmap_contains", "scratch_bitmap_add", "copy_reachable_frames_to_release_set",
        ),
        "kernel/memory/user_address_space_handoff.c": (
            "scratch_bitmap_contains", "scratch_bitmap_add", "find_one_prepared_thread",
            "process_has_no_owned_frames", "reachable_user_targets_are_wired", "select_reachable_process_user_targets",
        ),
    },
    "mutable_shapes": {
        "kernel/memory/frame_ownership_validation.c/micros_frame_ownership_count_process": "",
        "kernel/memory/frame_ownership_bootstrap.c/collect_process_release_summary": """
process_index = ownership->process_frames[process.slot];
for (position = 0; position < selection->word_count; ++position) {
    word_index = selection->words[position];
    word = selection->bitmap[word_index];
    for (bit_index = 0; bit_index < 64; ++bit_index) {
        ++summary->owner_counts[owner.kind];
    }
}
""",
        "kernel/memory/frame_ownership_bootstrap.c/commit_process_release": """
for (position = 0; position < selection->word_count; ++position) {
    word_index = selection->words[position];
    word = selection->bitmap[word_index];
    for (bit_index = 0; bit_index < 64; ++bit_index) {
        ownership->owners[frame_index] = (struct micros_frame_owner){0, 0, 0, 0};
        ownership->handoff_targets[frame_index] = MICROS_FRAME_HANDOFF_NONE;
    }
}
for (frame_index = 0; frame_index < MICROS_FRAME_OWNER_KIND_COUNT; ++frame_index) {
    ownership->owner_counts[frame_index] -= summary->owner_counts[frame_index];
}
""",
        "kernel/memory/frame_ownership_handoff.c/micros_frame_ownership_prepare_wired_process_user_set": """
for (position = 0; position < selection->word_count; ++position) {
    word_index = selection->words[position];
    word = selection->bitmap[word_index];
    for (bit_index = 0; bit_index < 64; ++bit_index) {
        owner = ownership->owners[frame_index];
        if (ownership->handoff_targets[frame_index] != MICROS_FRAME_HANDOFF_NONE) {}
    }
}
if (selected_count != ownership->owner_counts[MICROS_FRAME_OWNER_PROCESS_USER]) {}
for (position = 0; position < selection->word_count; ++position) {
    word_index = selection->words[position];
    word = selection->bitmap[word_index];
    for (bit_index = 0; bit_index < 64; ++bit_index) {
        ownership->handoff_targets[frame_index] = MICROS_FRAME_HANDOFF_VM_WIRED;
    }
}
""",
        "kernel/memory/frame_ownership_vm_freeze.c/micros_frame_ownership_vm_freeze_commit_prevalidated": """
for (index = 0; index < plan->data_frame_count; ++index) {
    frame_index = plan->data_frame_indices[index];
    ownership->owners[frame_index] = wired;
}
for (index = 0; index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT; ++index) {
    frame_index = plan->table_frame_indices[index];
    ownership->owners[frame_index] = table;
    ownership->handoff_targets[frame_index] = MICROS_FRAME_HANDOFF_NONE;
}
ownership->owner_counts[MICROS_FRAME_OWNER_VM_TRANSFERABLE] -= plan->data_frame_count;
ownership->owner_counts[MICROS_FRAME_OWNER_VM_WIRED] += plan->data_frame_count;
ownership->owner_counts[MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE] += MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT;
""",
        "kernel/memory/user_address_space.c/clear_released_frames_or_panic": """
for (position = 0; position < selection->word_count; ++position) {
    word_index = selection->words[position];
    word = selection->bitmap[word_index];
    for (bit_index = 0; bit_index < 64; ++bit_index) {}
}
""",
        "kernel/memory/user_address_space.c/micros_user_address_space_destroy":
            "mutable_process = &mutable_objects->processes[process.slot];",
        "kernel/memory/user_address_space_runtime.c/micros_user_address_space_acquire_scratch": "",
        "kernel/memory/user_address_space_runtime.c/micros_user_address_space_release_scratch": "",
        "kernel/memory/user_address_space_handoff.c/validate_handoff_processes": """
for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
    const struct micros_process *record = &objects->processes[index];
    const struct micros_user_address_space_handoff_process *state = &summary[index];
    thread = &objects->threads[state->thread_slot];
}
""",
        "kernel/memory/user_address_space_handoff.c/micros_user_address_space_prepare_wired_handoff": """
for (process_index = 0; process_index < MICROS_PROCESS_CAPACITY; ++process_index) {
    const struct micros_process *record = &objects->processes[process_index];
    error = micros_user_address_space_collect_bootstrap_handoff_locked(
        &runtime, &scratch, process, summary[process_index].owned_frame_count, &plan);
}
""",
        "kernel/memory/user_address_space_handoff.c/micros_user_address_space_complete_wired_handoff": """
for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
    const struct micros_process *record = &objects->processes[index];
    error = micros_user_address_space_collect_bootstrap_handoff_locked(
        &runtime, &scratch, process, summary[index].owned_frame_count, &plan);
}
""",
    },
    "skeleton_edits": {
        "kernel/memory/user_address_space.c": (
            (
                '#include "kernel/memory/user_address_space_internal.h"',
                '#include "kernel/memory/user_address_space_internal.h"\n'
                '#include "kernel/objects/kernel_object_runtime_internal.h"',
            ),
        ),
        "kernel/memory/user_address_space_runtime.c": (
            (
                '#include "arch/riscv64/mmu.h"',
                '#include "arch/riscv64/mmu.h"\n'
                '#include "kernel/memory/frame_ownership_internal.h"',
            ),
            (
                "static uint64_t reachable_bitmap[MICROS_FRAME_ALLOCATOR_BITMAP_WORDS];\n"
                "static uint64_t release_bitmap[MICROS_FRAME_ALLOCATOR_BITMAP_WORDS];\n"
                "static bool scratch_busy;",
                "static struct micros_frame_selection reachable_selection;\n"
                "static struct micros_frame_selection release_selection;\n"
                "static uint8_t scratch_mode;",
            ),
            ("sizeof(reachable_bitmap)", "sizeof(reachable_selection.bitmap)"),
            ("sizeof(release_bitmap)", "sizeof(release_selection.bitmap)"),
        ),
    },
    "current_helpers": {
        "kernel/memory/frame_ownership.c": {
            "selection_header_is_valid": (
                "static bool selection_header_is_valid(const struct micros_frame_selection *selection)",
                (),
                """
return selection->word_count <= MICROS_FRAME_ALLOCATOR_BITMAP_WORDS
    && selection->frame_count <= MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES
    && (selection->word_count == 0) == (selection->frame_count == 0)
    && selection->word_count <= selection->frame_count;
""",
            ),
            "micros_frame_selection_contains": (
                "bool micros_frame_selection_contains(const struct micros_frame_selection *selection, uint64_t frame_index)",
                ("selection_header_is_valid",),
                """
position = selection->positions[word_index];
word = selection->bitmap[word_index];
return position != 0 && position <= selection->word_count
    && selection->words[position - 1] == word_index && word != 0 && (word & bit) != 0;
""",
            ),
            "micros_frame_selection_add": (
                "bool micros_frame_selection_add(struct micros_frame_selection *selection, uint64_t frame_index)",
                ("selection_header_is_valid",),
                """
position = selection->positions[word_index];
word = selection->bitmap[word_index];
if (position != 0) {
    if (position > selection->word_count || selection->words[position - 1] != word_index || word == 0) {}
} else {
    selection->words[selection->word_count] = (uint16_t)word_index;
    selection->positions[word_index] = (uint16_t)(selection->word_count + 1);
}
selection->bitmap[word_index] = word | bit;
""",
            ),
            "micros_frame_ownership_check_selection": (
                "enum micros_frame_ownership_error micros_frame_ownership_check_selection("
                "const struct micros_frame_selection *selection, uint64_t managed_frame_count)",
                ("selection_header_is_valid",),
                """
for (position = 0; position < selection->word_count; ++position) {
    word_index = selection->words[position];
    if (word_index >= MICROS_FRAME_ALLOCATOR_BITMAP_WORDS || selection->positions[word_index] != position + 1) {}
    word = selection->bitmap[word_index];
    for (bit_index = 0; bit_index < 64; ++bit_index) {}
}
""",
            ),
            "micros_frame_selection_clear": (
                "bool micros_frame_selection_clear(struct micros_frame_selection *selection)",
                ("micros_frame_ownership_check_selection",),
                """
for (position = 0; position < selection->word_count; ++position) {
    word_index = selection->words[position];
    selection->bitmap[word_index] = 0;
    selection->positions[word_index] = 0;
    selection->words[position] = 0;
}
""",
            ),
            "index_owner_matches_slot": (
                "static bool index_owner_matches_slot(const struct micros_frame_ownership *ownership, "
                "uint32_t encoded, uint16_t slot)",
                ("micros_frame_owner_is_valid", "micros_frame_owner_kind_is_process_bound"),
                """
if (encoded == 0 || encoded > ownership->managed_frame_count) {}
owner = ownership->owners[encoded - 1];
""",
            ),
            "index_head_is_valid": (
                "static bool index_head_is_valid(const struct micros_frame_ownership *ownership, uint16_t slot)",
                ("index_owner_matches_slot",),
                """
index = ownership->process_frames[slot];
links = ownership->owner_links[index.head - 1];
""",
            ),
            "micros_frame_ownership_index_can_insert": (
                "bool micros_frame_ownership_index_can_insert(const struct micros_frame_ownership *ownership, "
                "uint16_t slot, uint64_t frame_index, uint32_t additions)",
                ("index_head_is_valid", "micros_frame_owner_kind_is_process_bound"),
                """
if (slot >= MICROS_PROCESS_CAPACITY || frame_index >= ownership->managed_frame_count || additions == 0) {}
links = ownership->owner_links[frame_index];
owner = ownership->owners[frame_index];
index = ownership->process_frames[slot];
""",
            ),
            "micros_frame_ownership_index_can_remove": (
                "bool micros_frame_ownership_index_can_remove(const struct micros_frame_ownership *ownership, "
                "uint64_t frame_index)",
                (
                    "index_owner_matches_slot",
                    "micros_frame_owner_is_valid", "micros_frame_owner_kind_is_process_bound",
                ),
                """
if (frame_index >= ownership->managed_frame_count) {}
owner = ownership->owners[frame_index];
links = ownership->owner_links[frame_index];
index = ownership->process_frames[owner.slot];
if (links.previous != 0) {
    if (!index_owner_matches_slot(ownership, links.previous, owner.slot)
        || ownership->owner_links[links.previous - 1].next != encoded) {}
}
if (links.next != 0) {
    if (!index_owner_matches_slot(ownership, links.next, owner.slot)
        || ownership->owner_links[links.next - 1].previous != encoded) {}
}
""",
            ),
            "micros_frame_ownership_index_insert_prevalidated": (
                "void micros_frame_ownership_index_insert_prevalidated(struct micros_frame_ownership *ownership, "
                "uint64_t frame_index)",
                (),
                """
owner = ownership->owners[frame_index];
index = &ownership->process_frames[owner.slot];
ownership->owner_links[frame_index] = (struct micros_frame_owner_links){0, head};
if (head != 0) {
    ownership->owner_links[head - 1].previous = (uint32_t)(frame_index + 1);
}
""",
            ),
            "micros_frame_ownership_index_remove_prevalidated": (
                "void micros_frame_ownership_index_remove_prevalidated(struct micros_frame_ownership *ownership, "
                "uint64_t frame_index)",
                ("micros_frame_owner_kind_is_process_bound",),
                """
owner = ownership->owners[frame_index];
index = &ownership->process_frames[owner.slot];
links = ownership->owner_links[frame_index];
if (links.previous == 0) {
} else {
    ownership->owner_links[links.previous - 1].next = links.next;
}
if (links.next != 0) {
    ownership->owner_links[links.next - 1].previous = links.previous;
}
ownership->owner_links[frame_index] = (struct micros_frame_owner_links){0, 0};
""",
            ),
        },
        "kernel/memory/frame_ownership_validation.c": {
            "validate_process_chain": (
                "static enum micros_frame_ownership_error validate_process_chain("
                "const struct micros_frame_ownership *ownership, uint16_t slot, uint32_t generation, "
                "bool inspect_records, uint64_t *count)",
                (
                    "micros_frame_ownership_selected_record", "micros_frame_owner_is_valid",
                    "micros_frame_owner_kind_is_process_bound",
                ),
                """
index = ownership->process_frames[slot];
for (visited = 0; node != 0; ++visited) {
    if (visited >= index.count || node > ownership->managed_frame_count) {}
    frame_index = node - 1;
    links = ownership->owner_links[frame_index];
    if (inspect_records) {
    } else {
        owner = ownership->owners[frame_index];
    }
}
""",
            ),
            "index_storage_is_valid": (
                "static bool index_storage_is_valid(const struct micros_frame_ownership *ownership, "
                "uint64_t expected_owned, bool complete)",
                ("validate_process_chain", "micros_frame_owner_kind_is_process_bound"),
                """
for (slot = 0; slot < MICROS_PROCESS_CAPACITY; ++slot) {}
for (frame_index = 0; frame_index < ownership->managed_frame_count; ++frame_index) {
    links = ownership->owner_links[frame_index];
    if (links.previous != 0 || links.next != 0) {
        owner = ownership->owners[frame_index];
    }
}
if (complete) {
    for (frame_index = ownership->managed_frame_count;
         frame_index < MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES; ++frame_index) {
        links = ownership->owner_links[frame_index];
    }
}
""",
            ),
            "vm_index_set_preflight": (
                "static enum micros_frame_ownership_error vm_index_set_preflight("
                "const struct micros_frame_ownership *ownership, struct micros_process_handle process, "
                "const uint64_t *data_frame_indices, size_t data_frame_count, "
                "const uint64_t *table_frame_indices, bool inserting)",
                ("micros_frame_ownership_index_can_insert", "micros_frame_ownership_index_can_remove"),
                """
if (!inserting && ownership->process_frames[process.slot].count < total) {}
for (index = 0; index < data_frame_count; ++index) {
    frame_index = data_frame_indices[index];
}
for (index = 0; index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT; ++index) {
    frame_index = table_frame_indices[index];
}
""",
            ),
        },
        "kernel/memory/user_address_space_runtime.c": {
            "scratch_request_error": (
                "static enum micros_user_address_space_error scratch_request_error("
                "const struct micros_user_address_space_runtime_view *view, "
                "struct micros_user_address_space_scratch_lease *lease)",
                ("riscv_irq_is_enabled",),
                """
if (scratch_mode != MICROS_USER_ADDRESS_SPACE_SCRATCH_IDLE
    && scratch_mode != MICROS_USER_ADDRESS_SPACE_SCRATCH_FULL
    && scratch_mode != MICROS_USER_ADDRESS_SPACE_SCRATCH_SELECTED) {}
if (scratch_mode != MICROS_USER_ADDRESS_SPACE_SCRATCH_IDLE) {}
if (reachable_selection.word_count != 0 || reachable_selection.frame_count != 0
    || release_selection.word_count != 0 || release_selection.frame_count != 0) {}
""",
            ),
            "micros_user_address_space_acquire_selected_scratch": (
                "enum micros_user_address_space_error micros_user_address_space_acquire_selected_scratch("
                "const struct micros_user_address_space_runtime_view *view, "
                "struct micros_user_address_space_scratch_lease *lease)",
                ("scratch_request_error",),
                """
*lease = (struct micros_user_address_space_scratch_lease){
    .active = true,
    .mode = MICROS_USER_ADDRESS_SPACE_SCRATCH_SELECTED,
    .frame_count = view->ledger->managed_frame_count,
    .word_count = word_count,
    .reachable_bitmap = reachable_selection.bitmap,
    .release_bitmap = release_selection.bitmap,
    .reachable_selection = &reachable_selection,
    .release_selection = &release_selection,
};
""",
            ),
        },
        "kernel/memory/user_address_space_validation.c": {
            "bootstrap_collect_frame": (
                "static enum micros_user_address_space_error bootstrap_collect_frame("
                "const struct micros_frame_ownership *ledger, struct micros_user_address_space_scratch_lease *lease, "
                "struct micros_process_handle process, uint64_t physical_address, "
                "enum micros_frame_owner_kind kind, bool include_tables, "
                "struct micros_user_address_space_bootstrap_plan *plan)",
                (
                    "frame_index_for_physical_address", "micros_frame_ownership_lookup",
                    "micros_frame_selection_contains", "micros_frame_selection_add",
                ),
                """
if (kind == MICROS_FRAME_OWNER_PROCESS_USER
    && ledger->handoff_targets[frame_index] != MICROS_FRAME_HANDOFF_VM_WIRED) {}
""",
            ),
            "bootstrap_collect_leaf_table": (
                "static enum micros_user_address_space_error bootstrap_collect_leaf_table("
                "const struct micros_frame_ownership *ledger, struct micros_user_address_space_scratch_lease *lease, "
                "struct micros_process_handle process, uint64_t physical_address, bool include_tables, "
                "struct micros_user_address_space_bootstrap_plan *plan)",
                ("bootstrap_collect_frame", "table_at", "decode_pte"),
                """
for (index = 0; index < MICROS_SV39_TABLE_ENTRY_COUNT; ++index) {
    error = decode_pte(table->entries[index], &entry);
}
""",
            ),
            "bootstrap_collect_middle_table": (
                "static enum micros_user_address_space_error bootstrap_collect_middle_table("
                "const struct micros_frame_ownership *ledger, struct micros_user_address_space_scratch_lease *lease, "
                "struct micros_process_handle process, uint64_t physical_address, bool include_tables, "
                "struct micros_user_address_space_bootstrap_plan *plan)",
                ("bootstrap_collect_frame", "bootstrap_collect_leaf_table", "table_at", "decode_pte"),
                """
for (index = 0; index < MICROS_SV39_TABLE_ENTRY_COUNT; ++index) {
    error = decode_pte(table->entries[index], &entry);
}
""",
            ),
            "bootstrap_collect_root": (
                "static enum micros_user_address_space_error bootstrap_collect_root("
                "struct micros_user_address_space_runtime_view *runtime, "
                "struct micros_user_address_space_scratch_lease *lease, struct micros_process_handle process, "
                "bool include_tables, struct micros_user_address_space_bootstrap_plan *plan)",
                (
                    "micros_user_address_space_runtime_require_bootstrap",
                    "micros_frame_ownership_runtime_validate_metadata",
                    "micros_frame_selection_clear", "resolve_process_with_root", "bootstrap_collect_frame",
                    "selected_kernel_root_is_valid", "validate_selected_shared_entries",
                    "table_at", "decode_pte", "bootstrap_collect_middle_table",
                ),
                "error = decode_pte(root->entries[MICROS_USER_ADDRESS_SPACE_USER_ROOT_INDEX], &entry);",
            ),
            "micros_user_address_space_collect_bootstrap_release_locked": (
                "enum micros_user_address_space_error micros_user_address_space_collect_bootstrap_release_locked("
                "struct micros_user_address_space_runtime_view *runtime, "
                "struct micros_user_address_space_scratch_lease *lease, struct micros_process_handle process, "
                "struct micros_user_address_space_bootstrap_plan *plan)",
                (
                    "bootstrap_collect_root", "micros_frame_ownership_count_process",
                    "micros_user_address_space_map_ownership_error", "micros_frame_selection_contains",
                ),
                """
node = runtime->ledger->process_frames[process.slot].head;
for (; node != 0; node = links.next) {
    frame_index = node - 1;
    owner = runtime->ledger->owners[frame_index];
    links = runtime->ledger->owner_links[frame_index];
}
""",
            ),
            "micros_user_address_space_collect_bootstrap_handoff_locked": (
                "enum micros_user_address_space_error micros_user_address_space_collect_bootstrap_handoff_locked("
                "struct micros_user_address_space_runtime_view *runtime, "
                "struct micros_user_address_space_scratch_lease *lease, struct micros_process_handle process, "
                "uint64_t expected_owned_frames, struct micros_user_address_space_bootstrap_plan *plan)",
                ("bootstrap_collect_root",),
                "",
            ),
        },
        "kernel/memory/user_address_space_handoff.c": {
            "build_handoff_summary": (
                "static bool build_handoff_summary(const struct micros_frame_ownership *ledger, "
                "const struct micros_kernel_objects *objects, "
                "struct micros_user_address_space_handoff_process summary[MICROS_PROCESS_CAPACITY])",
                (),
                """
for (slot = 0; slot < MICROS_PROCESS_CAPACITY; ++slot) {
    summary[slot] = (struct micros_user_address_space_handoff_process){0, 0, 0, 0};
}
for (frame_index = 0; frame_index < ledger->managed_frame_count; ++frame_index) {
    owner = ledger->owners[frame_index];
    process = &objects->processes[owner.slot];
    if (process->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE && process->generation == owner.generation) {
        ++summary[owner.slot].owned_frame_count;
    }
}
for (thread_index = 0; thread_index < MICROS_THREAD_CAPACITY; ++thread_index) {
    thread = &objects->threads[thread_index];
    summary[thread->owner.slot].thread_slot = (uint16_t)thread_index;
    summary[thread->owner.slot].thread_generation = thread->generation;
    ++summary[thread->owner.slot].thread_count;
}
""",
            ),
        },
    },
    "extra_current_sites": {
        "kernel/memory/frame_ownership.c/selection_header_is_valid": (
            (
                "context", "INDEX_PROBE",
                "return selection->word_count <= MICROS_FRAME_ALLOCATOR_BITMAP_WORDS "
                "&& selection->frame_count <= MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES "
                "&& (selection->word_count == 0) == (selection->frame_count == 0) "
                "&& selection->word_count <= selection->frame_count;",
                0, "before", 1,
            ),
        ),
        "kernel/memory/frame_ownership.c/index_owner_matches_slot": (
            (
                "context", "INDEX_PROBE",
                "if (encoded == 0 || encoded > ownership->managed_frame_count)", 0, "before", 1,
            ),
            ("frames", "LINK_TRAVERSE", "owner = ownership->owners[encoded - 1];", 0, "before", 1),
        ),
        "kernel/memory/frame_ownership.c/micros_frame_ownership_index_can_insert": (
            (
                "context", "INDEX_PROBE",
                "if (slot >= MICROS_PROCESS_CAPACITY || frame_index >= ownership->managed_frame_count || additions == 0)",
                0, "before", 1,
            ),
        ),
        "kernel/memory/frame_ownership.c/micros_frame_ownership_index_can_remove": (
            ("context", "INDEX_PROBE", "if (frame_index >= ownership->managed_frame_count)", 0, "before", 1),
        ),
        "kernel/memory/frame_ownership.c/micros_frame_ownership_index_insert_prevalidated": (
            (
                "frames", "LINK_TRAVERSE",
                "ownership->owner_links[head - 1].previous = (uint32_t)(frame_index + 1);",
                0, "before", 1,
            ),
        ),
        "kernel/memory/frame_ownership.c/micros_frame_ownership_index_remove_prevalidated": (
            (
                "frames", "LINK_TRAVERSE",
                "ownership->owner_links[links.previous - 1].next = links.next;",
                0, "before", 1,
            ),
            (
                "frames", "LINK_TRAVERSE",
                "ownership->owner_links[links.next - 1].previous = links.previous;",
                0, "before", 1,
            ),
        ),
        "kernel/memory/frame_ownership_validation.c/validate_process_chain": (
            (
                "context", "INDEX_PROBE",
                "if (visited >= index.count || node > ownership->managed_frame_count)", 0, "before", 1,
            ),
            ("frames", "LINK_TRAVERSE", "frame_index = node - 1;", 0, "before", 1),
        ),
        "kernel/memory/user_address_space_runtime.c/scratch_request_error": (
            (
                "context", "INDEX_PROBE",
                "if (scratch_mode != MICROS_USER_ADDRESS_SPACE_SCRATCH_IDLE "
                "&& scratch_mode != MICROS_USER_ADDRESS_SPACE_SCRATCH_FULL "
                "&& scratch_mode != MICROS_USER_ADDRESS_SPACE_SCRATCH_SELECTED)",
                0, "before", 1,
            ),
            (
                "context", "INDEX_PROBE",
                "if (scratch_mode != MICROS_USER_ADDRESS_SPACE_SCRATCH_IDLE)", 0, "before", 1,
            ),
            (
                "context", "INDEX_PROBE",
                "if (reachable_selection.word_count != 0 || reachable_selection.frame_count != 0 "
                "|| release_selection.word_count != 0 || release_selection.frame_count != 0)",
                0, "before", 1,
            ),
        ),
        "kernel/memory/user_address_space_runtime.c/micros_user_address_space_acquire_selected_scratch": (
            (
                "context", "INDEX_PROBE",
                "*lease = (struct micros_user_address_space_scratch_lease){"
                ".active = true, .mode = MICROS_USER_ADDRESS_SPACE_SCRATCH_SELECTED,"
                ".frame_count = view->ledger->managed_frame_count, .word_count = word_count,"
                ".reachable_bitmap = reachable_selection.bitmap, .release_bitmap = release_selection.bitmap,"
                ".reachable_selection = &reachable_selection, .release_selection = &release_selection,"
                "};",
                0, "after", 1,
            ),
        ),
        "kernel/memory/user_address_space_validation.c/micros_user_address_space_collect_bootstrap_release_locked": (
            ("frames", "LINK_TRAVERSE", "frame_index = node - 1;", 0, "before", 1),
        ),
        "kernel/memory/user_address_space_handoff.c/build_handoff_summary": (
            (
                "plans", "CLEAR_UNIT",
                "summary[slot] = (struct micros_user_address_space_handoff_process){0, 0, 0, 0};",
                0, "after", 1,
            ),
            (
                "plans", "CLEARED_BYTE",
                "summary[slot] = (struct micros_user_address_space_handoff_process){0, 0, 0, 0};",
                0, "after", 16,
            ),
        ),
    },
    "declared_header_sha256": {
        "include/micros/frame_ownership.h": "5a44795584bcbc0a8a80f130b14ad76729bca523feb24681fbf6d1394034b883",
        "include/micros/frame_ownership_runtime.h": "3f636b6f4565b884a101c0a319797239914f64090ff38f03340fc9d96fbc481a",
        "kernel/memory/frame_ownership_internal.h": "153973b75baf4f9e05a79fc2bc46eac7fcc965bf54df630276e95589ffdcb77d",
        "kernel/memory/user_address_space_internal.h": "332a549df461039414828cf996dbe7b20b3fdd240a0feb9f7cbbc1f8310d6e8d",
    },
    "retirement_keys": (
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/frame_ownership_vm_freeze.c/2",
        "tests/host/memory_native_source_bindings.py:native-source-admission:MEM01/function/kernel/memory/bootstrap_memory.c/micros_frame_ownership_runtime_prepare_wired_process_user_set",
        "tests/host/memory_native_source_bindings.py:native-source-admission:MEM01/function/kernel/memory/bootstrap_memory.c/micros_frame_ownership_runtime_release_process_set",
        "tests/host/memory_native_source_bindings.py:native-source-admission:MEM01/function/kernel/memory/frame_ownership_bootstrap.c/commit_process_release",
        "tests/host/memory_native_source_bindings.py:native-source-admission:MEM01/function/kernel/memory/frame_ownership_bootstrap.c/process_release_counts_are_valid",
        "tests/host/memory_native_source_bindings.py:native-source-admission:MEM01/function/kernel/memory/frame_ownership_validation.c/micros_frame_ownership_validate",
        "tests/host/memory_native_source_bindings.py:native-source-admission:MEM01/function/kernel/memory/frame_ownership_validation.c/micros_frame_ownership_vm_process_set_preflight",
        "tests/host/memory_native_source_bindings.py:native-source-admission:MEM02/native-input/kernel/memory/frame_ownership_vm_freeze.c",
        "tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/definitions/kernel/memory/user_address_space_runtime.c",
        "tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/definitions/kernel/memory/user_address_space_validation.c",
        "tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/dependent-file/kernel/memory/user_address_space_handoff.c",
        "tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/function/kernel/memory/user_address_space_mutation.c/acquire_scratch",
        "tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/function/kernel/memory/user_address_space_mutation.c/clear_release_bitmap",
        "tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/function/kernel/memory/user_address_space_mutation.c/micros_user_address_space_release_page",
        "tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/function/kernel/memory/user_address_space_mutation.c/release_page_frames",
        "tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/function/kernel/memory/user_address_space_mutation.c/scratch_bitmap_add",
        "tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/function/kernel/memory/user_address_space_runtime.c/micros_user_address_space_acquire_scratch",
        "tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/function/kernel/memory/user_address_space_runtime.c/micros_user_address_space_release_scratch",
        "tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/header/kernel/memory/user_address_space_internal.h",
        "tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/native-input/include/micros/frame_ownership.h",
        "tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/native-input/include/micros/frame_ownership_runtime.h",
        "tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/native-input/kernel/memory/bootstrap_memory.c",
        "tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/native-input/kernel/memory/frame_ownership.c",
        "tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/native-input/kernel/memory/frame_ownership_bootstrap.c",
        "tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/native-input/kernel/memory/frame_ownership_handoff.c",
        "tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/native-input/kernel/memory/frame_ownership_internal.h",
        "tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/native-input/kernel/memory/frame_ownership_validation.c",
        "tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/native-input/kernel/memory/frame_ownership_vm_claim.c",
        "tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/native-input/kernel/memory/frame_ownership_vm_freeze.c",
        "tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/native-input/kernel/memory/frame_ownership_vm_release.c",
        "tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/native-input/kernel/memory/user_address_space.c",
        "tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/skeleton/kernel/memory/user_address_space_runtime.c",
        "tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/skeleton/kernel/memory/user_address_space_validation.c",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/definitions/kernel/memory/frame_ownership.c",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/definitions/kernel/memory/frame_ownership_validation.c",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/definitions/kernel/memory/user_address_space.c",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/definitions/kernel/memory/user_address_space_handoff.c",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/function/kernel/memory/bootstrap_memory.c/micros_frame_ownership_runtime_prepare_wired_process_user_set",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/function/kernel/memory/bootstrap_memory.c/micros_frame_ownership_runtime_release_process_set",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/function/kernel/memory/frame_ownership_bootstrap.c/collect_process_release_summary",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/function/kernel/memory/frame_ownership_bootstrap.c/commit_process_release",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/function/kernel/memory/frame_ownership_bootstrap.c/micros_frame_ownership_allocate",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/function/kernel/memory/frame_ownership_bootstrap.c/micros_frame_ownership_release",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/function/kernel/memory/frame_ownership_bootstrap.c/micros_frame_ownership_release_process_set",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/function/kernel/memory/frame_ownership_bootstrap.c/process_release_counts_are_valid",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/function/kernel/memory/frame_ownership_handoff.c/micros_frame_ownership_complete_handoff",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/function/kernel/memory/frame_ownership_handoff.c/micros_frame_ownership_prepare_wired_process_user_set",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/function/kernel/memory/frame_ownership_validation.c/micros_frame_ownership_count_process",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/function/kernel/memory/frame_ownership_validation.c/micros_frame_ownership_validate",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/function/kernel/memory/frame_ownership_validation.c/micros_frame_ownership_validate_active",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/function/kernel/memory/frame_ownership_validation.c/micros_frame_ownership_vm_process_set_preflight",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/function/kernel/memory/frame_ownership_vm_claim.c/micros_frame_ownership_vm_claim_preflight",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/function/kernel/memory/frame_ownership_vm_release.c/micros_frame_ownership_vm_release_process_commit_prevalidated",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/function/kernel/memory/frame_ownership_vm_release.c/micros_frame_ownership_vm_release_transferable_preflight",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/function/kernel/memory/user_address_space.c/clear_released_frames_or_panic",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/function/kernel/memory/user_address_space.c/copy_reachable_frames_to_release_set",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/function/kernel/memory/user_address_space.c/micros_user_address_space_destroy",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/function/kernel/memory/user_address_space.c/scratch_bitmap_add",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/function/kernel/memory/user_address_space.c/scratch_bitmap_contains",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/function/kernel/memory/user_address_space_handoff.c/acquire_scratch",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/function/kernel/memory/user_address_space_handoff.c/find_one_prepared_thread",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/function/kernel/memory/user_address_space_handoff.c/micros_user_address_space_complete_wired_handoff",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/function/kernel/memory/user_address_space_handoff.c/micros_user_address_space_prepare_wired_handoff",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/function/kernel/memory/user_address_space_handoff.c/process_has_no_owned_frames",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/function/kernel/memory/user_address_space_handoff.c/reachable_user_targets_are_wired",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/function/kernel/memory/user_address_space_handoff.c/scratch_bitmap_add",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/function/kernel/memory/user_address_space_handoff.c/scratch_bitmap_contains",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/function/kernel/memory/user_address_space_handoff.c/select_reachable_process_user_targets",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/function/kernel/memory/user_address_space_handoff.c/validate_handoff_processes",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/header/include/micros/frame_ownership.h",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/header/include/micros/frame_ownership_runtime.h",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/header/kernel/memory/frame_ownership_internal.h",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/native-input/kernel/memory/frame_ownership_vm_freeze.c",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/skeleton/kernel/memory/user_address_space.c",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/skeleton/kernel/memory/user_address_space_handoff.c",
        "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/skeleton/kernel/memory/user_address_space_validation.c",
        "tests/host/selected_ownership_observation_bindings.py:native-source-admission:MEM03/observation/kernel/memory/frame_ownership_bootstrap.c/collect_process_release_summary",
        "tests/host/selected_ownership_observation_bindings.py:native-source-admission:MEM03/observation/kernel/memory/frame_ownership_bootstrap.c/commit_process_release",
        "tests/host/selected_ownership_observation_bindings.py:native-source-admission:MEM03/observation/kernel/memory/frame_ownership_handoff.c/micros_frame_ownership_complete_handoff",
        "tests/host/selected_ownership_observation_bindings.py:native-source-admission:MEM03/observation/kernel/memory/frame_ownership_handoff.c/micros_frame_ownership_prepare_wired_process_user_set",
        "tests/host/selected_ownership_observation_bindings.py:native-source-admission:MEM03/observation/kernel/memory/frame_ownership_validation.c/micros_frame_ownership_count_process",
        "tests/host/selected_ownership_observation_bindings.py:native-source-admission:MEM03/observation/kernel/memory/frame_ownership_vm_freeze.c/micros_frame_ownership_vm_freeze_commit_prevalidated",
    ),
}

INDEX_TYPES = """\
struct micros_frame_owner_links {
    uint32_t previous;
    uint32_t next;
};

struct micros_process_frame_index {
    uint32_t head;
    uint32_t count;
};

struct micros_frame_selection {
    size_t word_count;
    uint64_t frame_count;
    uint64_t bitmap[MICROS_FRAME_ALLOCATOR_BITMAP_WORDS];
    uint16_t words[MICROS_FRAME_ALLOCATOR_BITMAP_WORDS];
    uint16_t positions[MICROS_FRAME_ALLOCATOR_BITMAP_WORDS];
};

_Static_assert(sizeof(struct micros_frame_owner_links) == 8,
               "frame links must remain compact indices");
_Static_assert(sizeof(struct micros_process_frame_index) == 8,
               "process frame indexes must remain compact");
_Static_assert(sizeof(struct micros_frame_selection) == 49168,
               "RV64 frame selections have a fixed storage bound");
_Static_assert(MICROS_FRAME_ALLOCATOR_BITMAP_WORDS <= UINT16_MAX,
               "selection positions must encode every published word");
_Static_assert(MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES < UINT32_MAX,
               "owner links must encode every managed frame plus one");

"""

SELECTION_DECLARATIONS = """\
bool micros_frame_selection_add(
    struct micros_frame_selection *selection,
    uint64_t frame_index
);

bool micros_frame_selection_contains(
    const struct micros_frame_selection *selection,
    uint64_t frame_index
);

bool micros_frame_selection_clear(
    struct micros_frame_selection *selection
);
"""

INDEX_DECLARATIONS = """\
bool micros_frame_ownership_index_can_insert(
    const struct micros_frame_ownership *ownership,
    uint16_t slot,
    uint64_t frame_index,
    uint32_t additions
);

bool micros_frame_ownership_index_can_remove(
    const struct micros_frame_ownership *ownership,
    uint64_t frame_index
);

void micros_frame_ownership_index_insert_prevalidated(
    struct micros_frame_ownership *ownership,
    uint64_t frame_index
);

void micros_frame_ownership_index_remove_prevalidated(
    struct micros_frame_ownership *ownership,
    uint64_t frame_index
);

enum micros_frame_ownership_error micros_frame_ownership_check_selection(
    const struct micros_frame_selection *selection,
    uint64_t managed_frame_count
);
"""

ORIGINAL_LEASE = """\
struct micros_user_address_space_scratch_lease {
    bool active;
    uint64_t frame_count;
    size_t word_count;
    uint64_t *reachable_bitmap;
    uint64_t *release_bitmap;
};"""

SELECTED_LEASE = """\
enum micros_user_address_space_scratch_mode {
    MICROS_USER_ADDRESS_SPACE_SCRATCH_IDLE = 0,
    MICROS_USER_ADDRESS_SPACE_SCRATCH_FULL,
    MICROS_USER_ADDRESS_SPACE_SCRATCH_SELECTED,
};

struct micros_user_address_space_scratch_lease {
    bool active;
    uint8_t mode;
    uint64_t frame_count;
    size_t word_count;
    uint64_t *reachable_bitmap;
    uint64_t *release_bitmap;
    struct micros_frame_selection *reachable_selection;
    struct micros_frame_selection *release_selection;
};"""

BOOTSTRAP_DECLARATIONS = """\
struct micros_user_address_space_bootstrap_plan {
    const struct micros_process *process;
    uint64_t root_physical_address;
    uint64_t frame_count;
    bool wired_targets;
};

struct micros_user_address_space_handoff_process {
    uint64_t owned_frame_count;
    uint32_t thread_generation;
    uint16_t thread_slot;
    uint16_t thread_count;
};

_Static_assert(sizeof(struct micros_user_address_space_bootstrap_plan) == 32,
               "bootstrap root plans must remain bounded automatic state");
_Static_assert(sizeof(struct micros_user_address_space_handoff_process) == 16,
               "handoff process summaries must remain compact");

enum micros_user_address_space_error
micros_user_address_space_acquire_selected_scratch(
    const struct micros_user_address_space_runtime_view *view,
    struct micros_user_address_space_scratch_lease *lease
);

enum micros_user_address_space_error
micros_user_address_space_collect_bootstrap_release_locked(
    struct micros_user_address_space_runtime_view *runtime,
    struct micros_user_address_space_scratch_lease *lease,
    struct micros_process_handle process,
    struct micros_user_address_space_bootstrap_plan *plan
);

enum micros_user_address_space_error
micros_user_address_space_collect_bootstrap_handoff_locked(
    struct micros_user_address_space_runtime_view *runtime,
    struct micros_user_address_space_scratch_lease *lease,
    struct micros_process_handle process,
    uint64_t expected_owned_frames,
    struct micros_user_address_space_bootstrap_plan *plan
);
"""


@lru_cache(maxsize=32)
def fixed_header(root, path):
    if path not in HEADER_PATHS:
        raise ValueError("MEM05 header leaves its fixed interface namespace")
    root = Path(root).resolve()
    tree = subprocess.check_output(
        ("git", "rev-parse", RUNTIME_REFERENCE + "^{tree}"), cwd=root, timeout=30,
    ).decode().strip()
    if tree != RUNTIME_TREE:
        raise ValueError("MEM05 fixed header commit/tree changed")
    return subprocess.check_output(
        ("git", "show", RUNTIME_REFERENCE + ":" + path), cwd=root, timeout=30,
    ).decode()


@lru_cache(maxsize=512)
def fixed_source(root, path):
    if not _paths((path,), 1):
        raise ValueError("MEM05 fixed source has an invalid repository path")
    adapters = {
        *SOURCE_ROLES["fixed_adapters"], *SOURCE_ROLES["unchanged_candidates"],
        *SOURCE_ROLES["immutable_adapters"],
    }
    reference = SOURCE_PROJECTION["adapter_reference"] if path in adapters else RUNTIME_REFERENCE
    return subprocess.check_output(
        ("git", "show", reference + ":" + path),
        cwd=Path(root).resolve(), timeout=30,
    )


def _replace_once(text, before, after):
    if text.count(before) != 1:
        raise ValueError("MEM05 declared header lacks its exact original boundary")
    return text.replace(before, after, 1)


def _append_declarations(text, declarations):
    ending = "\n#endif\n"
    if not text.endswith(ending):
        raise ValueError("MEM05 declared header lacks its final include guard")
    return text[:-len(ending)] + "\n" + declarations + ending


def declared_header(root, path):
    text = fixed_header(str(Path(root).resolve()), path)
    if path == "include/micros/frame_ownership.h":
        text = _replace_once(text, "#include <stddef.h>", "#include <stdbool.h>\n#include <stddef.h>")
        text = _replace_once(text, "struct micros_frame_ownership {", INDEX_TYPES + "struct micros_frame_ownership {")
        tail = (
            "    uint8_t\n"
            "        handoff_targets[MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES];\n"
        )
        text = _replace_once(
            text, tail,
            tail + "    struct micros_frame_owner_links\n"
            "        owner_links[MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES];\n"
            "    struct micros_process_frame_index process_frames[MICROS_PROCESS_CAPACITY];\n",
        )
        text = _append_declarations(text, SELECTION_DECLARATIONS)
    if path in HEADER_PATHS[:2]:
        for name in ("selected", "release"):
            text = _replace_once(
                text, "    const uint64_t *" + name + "_bitmap,\n    size_t " + name + "_word_count",
                "    const struct micros_frame_selection *selection",
            )
    elif path == "kernel/memory/frame_ownership_internal.h":
        text = _append_declarations(text, INDEX_DECLARATIONS)
    elif path == "kernel/memory/user_address_space_internal.h":
        text = _replace_once(text, ORIGINAL_LEASE, SELECTED_LEASE)
        text = _append_declarations(text, BOOTSTRAP_DECLARATIONS)
    return text


def project_header(root, path, text):
    original = fixed_header(str(Path(root).resolve()), path)
    if text not in (original, declared_header(root, path)):
        raise ValueError("MEM05 header is partial or changes unrelated declarations")
    return original


def _current_signature(name, signature):
    from tests.host.c_source_records import compact

    changes = {
        "micros_frame_ownership_release_process_set": (
            "const uint64_t *release_bitmap, size_t release_word_count",
            "const struct micros_frame_selection *selection",
        ),
        "micros_frame_ownership_runtime_release_process_set": (
            "const uint64_t *release_bitmap, size_t release_word_count",
            "const struct micros_frame_selection *selection",
        ),
        "micros_frame_ownership_prepare_wired_process_user_set": (
            "const uint64_t *selected_bitmap, size_t selected_word_count",
            "const struct micros_frame_selection *selection",
        ),
        "micros_frame_ownership_runtime_prepare_wired_process_user_set": (
            "const uint64_t *selected_bitmap, size_t selected_word_count",
            "const struct micros_frame_selection *selection",
        ),
        "collect_process_release_summary": (
            "const uint64_t *release_bitmap, struct process_release_summary *summary",
            "const struct micros_frame_selection *selection, struct process_release_summary *summary, uint32_t *slot_count",
        ),
        "process_release_counts_are_valid": (
            "const struct process_release_summary *summary",
            "const struct process_release_summary *summary, uint32_t slot_count",
        ),
        "commit_process_release": ("const uint64_t *release_bitmap", "const struct micros_frame_selection *selection"),
        "validate_handoff_processes": (
            "const struct micros_frame_ownership *ledger, const struct micros_kernel_objects *objects",
            "const struct micros_kernel_objects *objects, "
            "const struct micros_user_address_space_handoff_process summary[MICROS_PROCESS_CAPACITY]",
        ),
    }
    value = compact(signature)
    if name in changes:
        before, after = map(compact, changes[name])
        if value.count(before) != 1:
            raise ValueError("MEM05 fixed signature lost its original boundary: " + name)
        value = value.replace(before, after, 1)
    return value


def _current_calls(name, body):
    from tests.host.selected_address_space_source import _function_calls

    calls = _function_calls(body)
    additions = {
        "micros_frame_ownership_count_process": ("validate_process_chain",),
        "micros_frame_ownership_validate": ("index_storage_is_valid",),
        "micros_frame_ownership_validate_active": ("index_storage_is_valid",),
        "micros_frame_ownership_vm_process_set_preflight": ("vm_index_set_preflight",),
        "micros_frame_ownership_allocate": (
            "micros_frame_owner_kind_is_process_bound", "micros_frame_ownership_index_can_insert",
            "micros_frame_ownership_index_can_remove", "micros_frame_ownership_index_insert_prevalidated",
        ),
        "micros_frame_ownership_release": (
            "micros_frame_ownership_index_can_remove", "micros_frame_ownership_index_remove_prevalidated",
        ),
        "collect_process_release_summary": ("micros_frame_ownership_index_can_remove",),
        "commit_process_release": ("micros_frame_ownership_index_remove_prevalidated",),
        "micros_frame_ownership_release_process_set": ("micros_frame_ownership_check_selection",),
        "micros_frame_ownership_prepare_wired_process_user_set": ("micros_frame_ownership_check_selection",),
        "micros_frame_ownership_complete_handoff": (
            "micros_frame_owner_kind_is_process_bound", "micros_frame_ownership_index_remove_prevalidated",
        ),
        "micros_frame_ownership_vm_claim_preflight": ("micros_frame_ownership_index_can_remove",),
        "micros_frame_ownership_vm_freeze_commit_prevalidated": ("micros_frame_ownership_index_insert_prevalidated",),
        "micros_frame_ownership_vm_release_transferable_preflight": ("micros_frame_ownership_index_can_remove",),
        "micros_frame_ownership_vm_release_process_commit_prevalidated": ("micros_frame_ownership_index_remove_prevalidated",),
        "micros_user_address_space_destroy": (
            "micros_user_address_space_acquire_selected_scratch",
            "micros_user_address_space_collect_bootstrap_release_locked",
            "micros_kernel_object_runtime_authoritative_registry",
        ),
        "clear_release_bitmap": ("micros_frame_selection_clear", "panic_invariant"),
        "scratch_bitmap_add": ("micros_frame_selection_add",),
        "acquire_scratch": ("micros_user_address_space_acquire_selected_scratch",),
        "micros_user_address_space_acquire_scratch": ("scratch_request_error",),
        "micros_user_address_space_release_scratch": (
            "micros_frame_ownership_runtime_ledger", "micros_frame_ownership_check_selection",
            "micros_frame_selection_clear", "riscv_irq_is_enabled",
        ),
        "micros_user_address_space_prepare_wired_handoff": (
            "build_handoff_summary", "micros_user_address_space_collect_bootstrap_handoff_locked",
        ),
        "micros_user_address_space_complete_wired_handoff": (
            "build_handoff_summary", "micros_user_address_space_collect_bootstrap_handoff_locked",
        ),
    }
    removals = {
        "micros_frame_ownership_count_process": (
            "micros_frame_owner_is_valid", "micros_frame_owner_kind_is_process_bound",
            "micros_frame_ownership_selected_record",
        ),
        "clear_released_frames_or_panic": ("scratch_bitmap_contains",),
        "micros_user_address_space_destroy": (
            "acquire_scratch", "copy_reachable_frames_to_release_set", "micros_kernel_object_runtime_detach_address_space",
            "micros_user_address_space_validate_locked", "panic_invariant",
        ),
        "clear_release_bitmap": ("micros_user_address_space_clear_bitmap",),
        "scratch_bitmap_add": ("micros_user_address_space_bitmap_add",),
        "acquire_scratch": ("micros_user_address_space_acquire_scratch",),
        "micros_user_address_space_release_page": ("micros_user_address_space_validate_locked",),
        "micros_user_address_space_acquire_scratch": ("riscv_irq_is_enabled",),
        "validate_handoff_processes": ("find_one_prepared_thread", "process_has_no_owned_frames"),
        "micros_user_address_space_prepare_wired_handoff": (
            "micros_user_address_space_validate_locked", "select_reachable_process_user_targets",
        ),
        "micros_user_address_space_complete_wired_handoff": (
            "micros_user_address_space_validate_locked", "reachable_user_targets_are_wired",
        ),
    }
    removed = set(removals.get(name, ()))
    added = set(additions.get(name, ()))
    if not removed <= calls or added & calls:
        raise ValueError("MEM05 current callee declaration differs from its fixed predecessor: " + name)
    return calls - removed | added


def _current_shape(path, name, original):
    shapes = SOURCE_PROJECTION["mutable_shapes"]
    key = path + "/" + name
    if key in shapes:
        return shapes[key]
    if name in ("micros_frame_ownership_validate", "micros_frame_ownership_validate_active"):
        complete = "true" if name == "micros_frame_ownership_validate" else "false"
        return original + (
            "\nif (!index_storage_is_valid(ownership,\n"
            "    observed_counts[MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE]\n"
            "        + observed_counts[MICROS_FRAME_OWNER_PROCESS_USER]\n"
            "        + observed_counts[MICROS_FRAME_OWNER_VM_WIRED], " + complete + ")) {}\n"
        )
    if name == "micros_frame_ownership_complete_handoff":
        anchor = "        ownership->owners[frame_index] = resulting_owner("
        addition = (
            "        if (micros_frame_owner_kind_is_process_bound(ownership->owners[frame_index].kind)\n"
            "            && ownership->handoff_targets[frame_index] == MICROS_FRAME_HANDOFF_VM_TRANSFERABLE) {\n"
            "            micros_frame_ownership_index_remove_prevalidated(ownership, frame_index);\n"
            "        }\n"
        )
        if original.count(anchor) != 1:
            raise ValueError("MEM05 handoff shape lost its original non-failing publication boundary")
        return original.replace(anchor, addition + anchor, 1)
    return original


@lru_cache(maxsize=4)
def current_function_contracts(root):
    from tests.host.c_source_records import compact

    root = str(Path(root).resolve())
    contracts = {}
    for path, mutable in SOURCE_PROJECTION["mutable_functions"].items():
        original = function_records(fixed_source(root, path).decode())
        added = SOURCE_PROJECTION["current_helpers"].get(path, {})
        removed = SOURCE_PROJECTION["removed_functions"].get(path, ())
        if not set((*mutable, *removed)) <= set(original) or set(added) & set(original) or set(mutable) & set(removed):
            raise ValueError("MEM05 current function partition differs from its fixed definitions")
        for name in mutable:
            record = original[name]
            contracts[path + "/" + name] = (
                _current_signature(name, record.signature), record.guards,
                tuple(sorted(_current_calls(name, record.body))),
                primitive_shape(path, name, _current_shape(path, name, record.body)),
            )
        for name, (signature, calls, shape) in added.items():
            contracts[path + "/" + name] = (
                compact(signature), (), tuple(sorted(calls)), primitive_shape(path, name, shape),
            )
    if len(contracts) != 55:
        raise ValueError("MEM05 current function/primitive declaration is incomplete")
    return contracts


def expected_skeleton(root, path):
    from tests.host.selected_address_space_source import _source_skeleton

    text = fixed_source(str(Path(root).resolve()), path).decode()
    for before, after in SOURCE_PROJECTION["skeleton_edits"].get(path, ()):
        if text.count(before) != 1:
            raise ValueError("MEM05 source skeleton lost its exact original boundary: " + path)
        text = text.replace(before, after, 1)
    return _source_skeleton(text)


def _current_authority(root):
    from tests.host.evidence_authority_scope import identity_required

    # This one verified transition retires its complete 82-key set atomically.
    return not identity_required(root, SOURCE_PROJECTION["retirement_keys"][0])


def native_assertion_admission(root, family, path, text, stage):
    from tests.host.object_hart_validation_source import native_assertion_admission as next_admission

    admitted = next_admission(root, family, path, text, stage)
    if admitted:
        return admitted
    owners = {
        "MEM01": "tests/host/memory_native_source_bindings.py",
        "MEM02": "tests/host/memory_native_source_bindings.py",
        "MEM03": "tests/host/selected_ownership_native_source_bindings.py",
        "MEM04": "tests/host/selected_address_space_native_source_bindings.py",
    }
    if stage not in (Stage.RED, Stage.GREEN) or family not in owners:
        raise ValueError("MEM05 legacy admission has an unknown family or stage")
    if stage is Stage.RED or path not in SOURCE_ROLES["production_paths"] or not _current_authority(root):
        return frozenset()
    prefix = owners[family] + ":native-source-admission:"
    selected = frozenset(
        key.removeprefix(prefix) for key in SOURCE_PROJECTION["retirement_keys"]
        if key.startswith(prefix) and (
            key.removeprefix(prefix) in {
                family + "/" + kind + "/" + path
                for kind in ("native-input", "dependent-file", "header", "definitions", "skeleton")
            }
            or key.removeprefix(prefix).startswith(family + "/function/" + path + "/")
        )
    )
    if selected:
        validate_retained_source(root, path, text, Stage.GREEN)
    return selected


def legacy_replaced_functions(root, family, path, text, stage):
    from tests.host.object_hart_validation_source import legacy_replaced_functions as next_admission

    admitted = next_admission(root, family, path, text, stage)
    if admitted:
        return admitted
    if family not in ("MEM01", "MEM02", "MEM03", "MEM04") or stage not in (Stage.RED, Stage.GREEN):
        raise ValueError("MEM05 legacy observation has an unknown family or stage")
    if stage is Stage.RED or path not in SOURCE_PROJECTION["mutable_functions"] or not _current_authority(root):
        return frozenset()
    validate_retained_source(root, path, text, Stage.GREEN)
    result = set(SOURCE_PROJECTION["current_helpers"].get(path, ()))
    result.update(SOURCE_PROJECTION["removed_functions"].get(path, ()))
    mutable = set(SOURCE_PROJECTION["mutable_functions"][path])
    if family == "MEM03":
        prefix = (
            "tests/host/selected_ownership_observation_bindings.py:native-source-admission:"
            "MEM03/observation/" + path + "/"
        )
        result.update(key.removeprefix(prefix) for key in SOURCE_PROJECTION["retirement_keys"] if key.startswith(prefix))
    elif family == "MEM04":
        from tests.host import selected_address_space_source as previous

        protected = set(previous.SOURCE_PROJECTION["new_helpers"].get(path, ()))
        protected.update(previous.SOURCE_PROJECTION["mutable_functions"].get(path, ()))
        if result & protected:
            raise ValueError("MEM05 cannot omit a retained MEM04 current-observation contract")
        result.update(mutable - protected)
    else:
        result.update(mutable)
    return frozenset(result)


CHANNELS = ("aggregate", "frames", "plans", "context", "ordinary")
OBSERVED_HEADERS = ("kernel/boot/bootstrap_control_internal.h",)
OBSERVATION_PREFIX = '#include "tests/host/selected_release_handoff_test.h"\n'
STORE_RULES = {
    ("kernel/memory/frame_ownership.c", "micros_frame_ownership_clear_bytes", "bytes"): ("CLEAR", 1),
    ("kernel/memory/frame_ownership.c", "micros_frame_ownership_copy_bytes", "output"): ("COPY", 1),
    ("kernel/memory/user_address_space_validation.c", "clear_bytes", "bytes"): ("CLEAR", 1),
    ("kernel/memory/user_address_space_runtime.c", "clear_scratch_bitmap", "bitmap"): ("CLEAR", 8),
    ("kernel/memory/user_address_space_table.c", "micros_user_address_space_clear_bitmap", "bitmap"): ("CLEAR", 8),
    ("kernel/memory/user_address_space_table.c", "micros_user_address_space_clear_page", "words"): ("CLEAR", 8),
    ("kernel/objects/kernel_object_state.c", "clear_bytes", "bytes"): ("CLEAR", 1),
    ("kernel/objects/kernel_object_state.c", "kernel_object_clear_user_context", "bytes"): ("CLEAR", 1),
    ("kernel/objects/kernel_object_state.c", "kernel_object_copy_user_context", "destination_bytes"): ("COPY", 1),
    ("kernel/memory/frame_ownership.c", "micros_frame_selection_clear", "bitmap"): ("CLEAR", 8),
    ("kernel/memory/frame_ownership.c", "micros_frame_selection_clear", "positions"): ("CLEAR", 2),
    ("kernel/memory/frame_ownership.c", "micros_frame_selection_clear", "words"): ("CLEAR", 2),
}


def _owner(path):
    owners = {
        "memory": "KERNEL_MEMORY", "objects": "KERNEL_OBJECTS",
        "scheduler": "KERNEL_SCHEDULER", "endpoint": "KERNEL_ENDPOINT",
        "ipc": "KERNEL_IPC", "execution": "KERNEL_EXECUTION",
        "boot": "KERNEL_BOOT", "vm": "KERNEL_VM", "tty": "KERNEL_TTY",
    }
    if not path.startswith("kernel/") or path.split("/")[1] not in owners:
        raise ValueError("MEM05 primitive has an unknown physical owner")
    return owners[path.split("/")[1]]


def _selected_functions(path, text):
    from tests.host import selected_address_space_source as previous
    from tools.generate_selected_release_handoff_fixture import EXECUTION_FUNCTIONS

    names = previous.NATIVE_FUNCTION_SUBSETS.get(path)
    if path == "kernel/memory/bootstrap_memory.c":
        names = (*previous.BOOTSTRAP_FUNCTIONS, "micros_frame_ownership_runtime_prepare_wired_process_user_set")
    elif path == "kernel/vm/vm_handoff_runtime.c":
        names = (*names, "micros_vm_handoff_runtime_inspect_scratch_binding")
    elif path == "kernel/execution/user_execution_validation.c":
        names = EXECUTION_FUNCTIONS
    return tuple(function_records(text)) if names is None else names


def _field_kind(path, field, member, written, assigned):
    if field in ("owners", "handoff_targets") and member:
        return "frames", "RECORD_VISIT"
    if field == "allocated_bitmap" and member:
        return "frames", "INDEX_PROBE" if assigned else "BITMAP_WORD_LOAD"
    if field in ("bitmap", "reachable_bitmap", "release_bitmap", "selected_bitmap"):
        return "plans", "INDEX_PROBE" if assigned else "BITMAP_WORD_LOAD"
    if field in ("owner_links", "process_frames") and member:
        return "frames", "INDEX_PROBE"
    if field in ("words", "positions") and member:
        return "plans", "INDEX_PROBE"
    if field == "entries" and member and "/memory/user_address_space" in path:
        return "plans", "COPY_UNIT" if written else "PTE_VISIT"
    if field == "managed_ranges" and member:
        return "context", "RANGE_VISIT"
    if field in ("processes", "threads", "harts", "endpoints") and member:
        return "context", "RECORD_VISIT"
    if field in ("owner_counts", "mappings") and member:
        return "context", "INDEX_PROBE"
    if field in ("observed_counts", "resulting_counts", "summary"):
        return "context", "INDEX_PROBE"
    if field in (
        "frame_indices", "data_frame_indices", "table_frame_indices",
        "physical_addresses", "data_physical_addresses", "table_physical_addresses",
    ):
        return "context", "INDEX_PROBE"
    return None


def _array_declaration(values, stacks, occurrence):
    enclosing = stacks[occurrence]
    start = enclosing[-1] + 1 if enclosing else 0
    for index in range(start, occurrence):
        if values[index] in (";", "}") and stacks[index + 1] == enclosing:
            start = index + 1
    prefix = list(values[start:occurrence])
    while prefix and prefix[0] in ("const", "volatile", "static", "register", "restrict"):
        prefix.pop(0)
    if not prefix or prefix[0] not in (
        "struct", "union", "enum", "char", "unsigned", "signed", "short", "int", "long",
        "bool", "size_t", "uintptr_t", "intptr_t", "uint64_t", "uint32_t", "uint16_t", "uint8_t",
        "int64_t", "int32_t", "int16_t", "int8_t",
    ):
        return False
    depth, initialized = 0, False
    for value in prefix:
        if value in ("(", "["):
            depth += 1
        elif value in (")", "]"):
            depth -= 1
        elif depth == 0 and value == "=":
            initialized = True
        elif depth == 0 and value == ",":
            initialized = False
    return depth == 0 and not initialized


def _body_sites(path, name, body):
    from tests.host.selected_address_space_source import COMPLETE_FUNCTIONS, _matching
    from tests.host.selected_ownership_source import _original_regions

    positions = positional_tokens(body)
    values = tuple(item.value for item in positions)
    stacks, closings = compound_layout(values)
    sites = []
    owner = _owner(path)

    def add(mode, channel, kind, start, end, units=1):
        begin = positions[start].start if start < len(positions) else len(body)
        finish = positions[end - 1].end if end > start else begin
        regions = _original_regions(values, stacks, closings, stacks[start]) if start < len(stacks) else ()
        identifier = "mem05-" + hashlib.sha256(
            repr((path, name, mode, kind, start, end, units)).encode()
        ).hexdigest()[:20]
        sites.append((identifier, mode, channel, owner, kind, begin, finish, body[begin:finish], regions, units))

    if name in COMPLETE_FUNCTIONS or path == "kernel/memory/user_address_space_validation.c" and name == "bootstrap_collect_root":
        identifier = "mem05-audit-" + hashlib.sha256((path + "/" + name).encode()).hexdigest()[:20]
        sites.append((identifier, "insert", "context", owner, "COMPLETE_VALIDATOR_CALL", 0, 0, "", (), 1))
    for index, value in enumerate(values):
        if value in ("for", "while") and index + 1 < len(values) and values[index + 1] == "(":
            closing = _matching(values, index + 1, "(", ")")
            if closing + 1 >= len(values) or values[closing + 1] != "{":
                raise ValueError("MEM05 observed loop lacks its explicit compound")
            add("insert", "context", "INDEX_PROBE", closing + 2, closing + 2)
        if index + 1 >= len(values) or values[index + 1] != "[":
            continue
        member = index > 0 and values[index - 1] in (".", "->")
        if not member and _array_declaration(values, stacks, index):
            continue
        closing = _matching(values, index + 1, "[", "]")
        tail = closing + 1
        while tail + 1 < len(values) and values[tail] in (".", "->"):
            tail += 2
        assigned = tail < len(values) and values[tail] == "="
        written = assigned or tail < len(values) and values[tail] in ("|=", "&=", "^=", "+=", "-=", "++", "--")
        classification = _field_kind(path, value, member, written, assigned)
        if classification is not None:
            if closing == index + 2:
                raise ValueError("MEM05 observed access has no index expression")
            add("wrap", *classification, index + 2, closing)
        movement = STORE_RULES.get((path, name, value))
        if (
            path == "kernel/memory/frame_ownership.c"
            and name == "micros_frame_selection_clear" and value == "words" and not written
        ):
            movement = None
        if movement is not None:
            if not assigned:
                raise ValueError("MEM05 movement primitive lacks its fixed store")
            semicolon = values.index(";", tail)
            operation, units = movement
            add("insert", "plans", operation + "_UNIT", semicolon + 1, semicolon + 1)
            volume = "CLEARED_BYTE" if operation == "CLEAR" else "COPIED_BYTE"
            add("insert", "plans", volume, semicolon + 1, semicolon + 1, units)
    if path == "kernel/memory/user_address_space_mutation.c" and name == "commit_page_release":
        fragment = ("*", "release", "->", "leaf_pte", "=", "0", ";")
        matches = [index for index in range(len(values)) if values[index:index + len(fragment)] == fragment]
        if len(matches) != 1:
            raise ValueError("MEM05 fixed selected PTE store is missing or ambiguous")
        end = matches[0] + len(fragment)
        add("insert", "plans", "COPY_UNIT", end, end)
    extras = SOURCE_PROJECTION["extra_current_sites"].get(path + "/" + name, ())
    for channel, kind, primitive, occurrence, placement, units in extras:
        from tests.host.c_source_records import tokens

        fragment = tokens(primitive)
        matches = [index for index in range(len(values)) if values[index:index + len(fragment)] == fragment]
        expected_count = 1 + max(row[3] for row in extras if tokens(row[2]) == fragment)
        if len(matches) != expected_count or placement not in ("before", "after"):
            raise ValueError("MEM05 explicit current primitive is absent or has an invalid placement")
        first = matches[occurrence]
        token = first if placement == "before" else first + len(fragment)
        position = positions[token].start if token < len(positions) else len(body)
        regions = _original_regions(values, stacks, closings, stacks[first])
        identifier = "mem05-extra-" + hashlib.sha256(
            repr((path, name, channel, kind, fragment, occurrence, placement, units)).encode()
        ).hexdigest()[:20]
        sites.append((identifier, "insert", channel, owner, kind, position, position, primitive, regions, units))
    if len({site[0] for site in sites}) != len(sites):
        raise ValueError("MEM05 primitive inventory has duplicate sites")
    return tuple(sites)


def primitive_shape(path, name, body):
    """Bind primitive syntax and enclosing regions, excluding scalar-only statements."""
    from tests.host.selected_ownership_source import _original_statement

    positions = positional_tokens(body)
    values = tuple(item.value for item in positions)
    stacks, _ = compound_layout(values)
    result = []
    for site in sorted(_body_sites(path, name, body), key=lambda row: row[5]):
        _, mode, channel, owner, kind, begin, _, declared, regions, units = site
        if mode == "wrap":
            occurrence = next(index for index, token in enumerate(positions) if token.start == begin) - 2
            first, last = _original_statement(values, stacks, occurrence)
            primitive = " ".join(values[first:last])
        elif declared:
            primitive = " ".join(item.value for item in positional_tokens(declared))
        elif kind == "INDEX_PROBE":
            primitive = regions[-1][0]
        elif begin == 0:
            primitive = "function-entry"
        else:
            following = next((index for index, token in enumerate(positions) if token.start >= begin), len(values))
            first, last = _original_statement(values, stacks, following - 2)
            primitive = " ".join(values[first:last])
        result.append((mode, channel, owner, kind, primitive, regions, units))
    return tuple(result)


def validate_helper_contract(root, path, name, record):
    from tests.host.c_source_records import compact
    from tests.host.selected_address_space_source import _function_calls

    declarations = read_declarations(root)["SOURCE_PROJECTION"]["current_helpers"]
    contract = declarations.get(path, {}).get(name)
    if contract is None:
        raise ValueError("MEM05 current helper is not independently declared")
    signature, calls, shape = contract
    if (
        compact(record.signature) != compact(signature) or record.guards
        or _function_calls(record.body) != set(calls) or re.search(r"^\s*#", record.body, re.M)
        or primitive_shape(path, name, record.body) != primitive_shape(path, name, shape)
    ):
        raise ValueError("MEM05 current helper changed its signature, callees or complete primitive bindings")


@lru_cache(maxsize=256)
def baseline_observations(root, path):
    from tools.generate_selected_release_handoff_fixture import fixed_inputs, native_source_members

    if path not in (*native_source_members(root), *OBSERVED_HEADERS):
        raise ValueError("MEM05 observation leaves the fixed compiled closure")
    original = fixed_inputs(root)[path][0].decode()
    spans = function_spans(original)
    names = _selected_functions(path, original)
    if not set(names) <= set(spans):
        raise ValueError("MEM05 fixed observation closure omitted a function")
    return {
        name: _body_sites(path, name, original[spans[name].body_start:spans[name].body_end])
        for name in names
    }


@lru_cache(maxsize=4)
def validate_primitive_declarations(root):
    from tools.generate_selected_release_handoff_fixture import native_source_members

    root = str(Path(root).resolve())
    catalog = {path: baseline_observations(root, path) for path in (*native_source_members(root), *OBSERVED_HEADERS)}
    if (
        digest(catalog) != SOURCE_PROJECTION["baseline_observations_sha256"]
        or sum(len(functions) for functions in catalog.values()) != SOURCE_PROJECTION["baseline_observation_functions"]
        or sum(len(sites) for functions in catalog.values() for sites in functions.values())
        != SOURCE_PROJECTION["baseline_observation_sites"]
    ):
        raise ValueError("MEM05 complete fixed primitive inventory differs from its independent declaration")
    for path, functions in SOURCE_PROJECTION["current_helpers"].items():
        for name, (_, _, shape) in functions.items():
            primitive_shape(path, name, shape)
    if digest(current_function_contracts(root)) != SOURCE_PROJECTION["current_function_contracts_sha256"]:
        raise ValueError("MEM05 current function/primitive contracts differ from their independent declaration")
    return True


def validate_retained_source(root, path, text, stage):
    from tests.host.object_hart_validation_source import native_assertion_admission as next_admission

    admitted = next_admission(root, "MEM05", path, text, stage)
    if admitted:
        return
    from tests.host.c_source_records import compact
    from tests.host.selected_address_space_source import _function_calls, _source_skeleton
    from tools.generate_selected_release_handoff_fixture import fixed_inputs

    root = Path(root).resolve()
    fixed = fixed_inputs(str(root))
    if stage not in (Stage.RED, Stage.GREEN) or type(text) is not str or path not in fixed:
        raise ValueError("MEM05 source admission has an unknown stage, representation or input")
    original = fixed[path][0].decode()
    if stage is Stage.RED or path not in SOURCE_ROLES["production_paths"]:
        if text != original:
            raise ValueError("MEM05 fixed or noncandidate native source changed: " + path)
        return
    validate_primitive_declarations(root)
    if path in HEADER_PATHS:
        expected = declared_header(root, path)
        if text != expected or hashlib.sha256(expected.encode()).hexdigest() != SOURCE_PROJECTION["declared_header_sha256"][path]:
            raise ValueError("MEM05 current header differs from its independently declared complete interface")
        return
    before, after = function_records(original), function_records(text)
    mutable = SOURCE_PROJECTION["mutable_functions"][path]
    removed = SOURCE_PROJECTION["removed_functions"].get(path, ())
    additions = SOURCE_PROJECTION["current_helpers"].get(path, {})
    if set(after) != set(before).difference(removed) | set(additions):
        raise ValueError("MEM05 current source has missing or unadmitted definitions: " + path)
    contracts = current_function_contracts(str(root))
    for name, record in after.items():
        if name not in mutable and name not in additions:
            old = before[name]
            if record.signature != old.signature or record.body != old.body or record.guards != old.guards:
                raise ValueError("MEM05 changed an unrelated original function: " + path + "/" + name)
            continue
        signature, guards, calls, shape = contracts[path + "/" + name]
        if (
            compact(record.signature) != signature or record.guards != guards
            or tuple(sorted(_function_calls(record.body))) != calls
            or primitive_shape(path, name, record.body) != shape
            or name in additions and re.search(r"^\s*#", record.body, re.M)
        ):
            raise ValueError("MEM05 current function escaped its declared signature/callees/primitive bindings: " + name)
        if name == "micros_user_address_space_require_idle":
            expected = before[name].body.replace(
                "if (scratch_busy)", "if (scratch_mode != MICROS_USER_ADDRESS_SPACE_SCRATCH_IDLE)",
            )
            if record.body != expected:
                raise ValueError("MEM05 idle check changed beyond its retained scalar mode adaptation")
    if _source_skeleton(text) != expected_skeleton(root, path):
        raise ValueError("MEM05 changed an unadmitted source include, type or storage owner: " + path)


def _current_observations(root, path, text):
    validate_retained_source(root, path, text, Stage.GREEN)
    records = function_records(text)
    contracts = current_function_contracts(str(Path(root).resolve()))
    result = {}
    for name in _selected_functions(path, text):
        record = records[name]
        declared = contracts.get(path + "/" + name)
        if declared is None:
            sites = baseline_observations(str(Path(root).resolve()), path)[name]
        else:
            if primitive_shape(path, name, record.body) != declared[3]:
                raise ValueError("MEM05 current observed function has undeclared work: " + path + "/" + name)
            sites = _body_sites(path, name, record.body)
        sites = sorted(sites, key=lambda row: row[5])
        result[name] = tuple(
            (
                "mem05-current-" + hashlib.sha256((path + "/" + name + "/" + str(index)).encode()).hexdigest()[:20],
                *site[1:],
            )
            for index, site in enumerate(sites)
        )
    return result


def _observation_call(site, fragment=None):
    identifier, mode, _, owner, kind, _, _, original, _, units = site
    if mode in ("wrap", "legacy-wrap", "compare"):
        expression = original if fragment is None else fragment
        if mode == "compare":
            return f'MICROS_TEST_WORK_COMPARE("{identifier}", MICROS_WORK_OWNER_{owner}, ({expression}))'
        macro = "MICROS_AS_OBSERVE_VALUE" if mode == "legacy-wrap" else "MICROS_MEM05_OBSERVE_VALUE"
        return (
            f'{macro}("{identifier}", MICROS_WORK_OWNER_{owner}, '
            f"MICROS_WORK_KIND_{kind}, ({expression}))"
        )
    return f'MICROS_TEST_WORK_HIT("{identifier}", MICROS_WORK_OWNER_{owner}, MICROS_WORK_KIND_{kind}, {units});'


def _render_body(body, sites):
    if any(site[1] not in ("wrap", "legacy-wrap", "compare", "insert") for site in sites):
        raise ValueError("MEM05 observation has an unknown rendering mode")
    wraps = sorted((site for site in sites if site[1] in ("wrap", "legacy-wrap", "compare")), key=lambda site: (site[5], -site[6]))
    top, stack = [], []
    for site in wraps:
        node = [site, []]
        while stack and site[5] >= stack[-1][0][6]:
            stack.pop()
        if stack:
            if site[6] > stack[-1][0][6] or site[5:7] == stack[-1][0][5:7]:
                raise ValueError("MEM05 observed expressions have ambiguous overlap")
            stack[-1][1].append(node)
        else:
            top.append(node)
        stack.append(node)

    def render(node):
        site, children = node
        fragment = body[site[5]:site[6]]
        for child in reversed(children):
            first, last = child[0][5:7]
            fragment = fragment[:first - site[5]] + render(child) + fragment[last - site[5]:]
        return _observation_call(site, fragment)

    edits = [(node[0][5], node[0][6], render(node)) for node in top]
    insertions = {}
    for site in sites:
        if site[1] != "insert":
            continue
        if any(node[0][5] < site[5] < node[0][6] for node in top):
            raise ValueError("MEM05 statement observation enters an expression")
        insertions.setdefault(site[5], []).append(_observation_call(site))
    edits.extend((position, position, "".join(calls)) for position, calls in insertions.items())
    for first, last, replacement in sorted(edits, reverse=True):
        body = body[:first] + replacement + body[last:]
    return body


def observe_source(root, path, text, channel, stage):
    from tests.host.object_hart_validation_source import legacy_observe_source as next_admission

    admitted = next_admission(root, "MEM05", path, text, stage, channel)
    if admitted is not None:
        return admitted
    if stage not in (Stage.RED, Stage.GREEN) or channel not in CHANNELS:
        raise ValueError("MEM05 observation has an unadmitted stage or channel")
    validate_retained_source(root, path, text, stage)
    declarations = (
        baseline_observations(str(Path(root).resolve()), path) if stage is Stage.RED
        else _current_observations(root, path, text)
    )
    if channel == "ordinary":
        return text
    spans = function_spans(text)
    edits = []
    for name, sites in declarations.items():
        selected = tuple(site for site in sites if channel == "aggregate" or site[2] == channel)
        span = spans[name]
        edits.append((span.body_start, span.body_end, _render_body(text[span.body_start:span.body_end], selected)))
    for first, last, body in sorted(edits, reverse=True):
        text = text[:first] + body + text[last:]
    return OBSERVATION_PREFIX + text


def project_observations(root, path, text, channel, stage):
    from tools.generate_selected_release_handoff_fixture import fixed_inputs

    original = (
        fixed_inputs(str(Path(root).resolve()))[path][0].decode() if stage is Stage.RED
        else regular_source(Path(root), path).read_text()
    )
    if text != observe_source(root, path, original, channel, stage):
        raise ValueError("MEM05 observation changed, moved or omitted its fixed primitive")
    return original


LEGACY_VALUE_PREFIX = (
    '#include "micros/work_observation.h"\n'
    "#define MICROS_MEM05_OBSERVE_VALUE(site, owner, kind, value) "
    "(micros_test_work_record((owner), (kind), 1), (value))\n"
)


def _legacy_backend(family):
    from tests.host import (
        bootstrap_allocation_source, kernel_mapping_source,
        selected_ownership_source, selected_address_space_source,
    )

    backends = {
        "MEM01": bootstrap_allocation_source, "MEM02": kernel_mapping_source,
        "MEM03": selected_ownership_source, "MEM04": selected_address_space_source,
    }
    if family not in backends:
        raise ValueError("MEM05 legacy producer has an unknown owner")
    return backends[family]


def _legacy_site_selected(family, channel, site):
    if channel == "aggregate" or family in ("MEM01", "MEM02"):
        return True
    _, _, category, _, kind, _, _, _, regions, _ = site
    if family == "MEM04":
        selected_path = category == "frames" and kind == "RECORD_VISIT" or category == "plans" and kind in ("PTE_VISIT", "COPY_UNIT")
        return channel == ("path" if selected_path else "context")
    if category == "frames" and kind == "RECORD_VISIT":
        inactive = any(
            "ownership -> managed_frame_count" in fragment
            and "MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES" in fragment
            for fragment, _ in regions
        )
        return channel == ("inactive-frame" if inactive else "active-frame")
    if kind == "RANGE_VISIT":
        return channel == "active-geometry"
    return False


def _legacy_retained_sites(root, family, path, text, channel):
    backend = _legacy_backend(family)
    spans = function_spans(text)
    result = {name: [] for name in spans}

    def statement(name, identifier, owner, kind, position, units):
        offset = position - spans[name].body_start
        result[name].append((identifier, "insert", "legacy", owner, kind, offset, offset, "", (), units))

    if family == "MEM01":
        for site, begin, _ in backend._observation_spans(path, text, Stage.GREEN, root=root):
            statement(site[0], site[1], "KERNEL_MEMORY", site[2], begin, 1)
    elif family == "MEM02":
        for site, position in backend._observation_positions(root, path, text, Stage.GREEN):
            statement(site[0], site[1], "KERNEL_MEMORY", site[2], position, site[6])
    elif family == "MEM03":
        backend._validate_observation_coverage(root, path, text, Stage.GREEN)
        for site, position, _ in backend._observation_bindings(root, path, text, Stage.GREEN, channel):
            statement(site[0], site[1], site[2], site[4], position, site[8])
    else:
        for name, sites in backend.observation_sites(root, path, text, Stage.GREEN).items():
            for site in sites:
                identifier, mode, category, owner, kind, start, end, fragment, regions = site
                if channel == "aggregate" or category == channel:
                    mode = {"wrap": "legacy-wrap", "after": "insert"}.get(mode, mode)
                    result[name].append((
                        identifier, mode, category, owner, kind,
                        start, end, fragment, regions, 1,
                    ))
    return result


def legacy_observe_source(root, family, path, text, stage, channel="aggregate"):
    from tests.host.object_hart_validation_source import legacy_observe_source as next_admission

    admitted = next_admission(root, family, path, text, stage, channel)
    if admitted is not None:
        return admitted
    from collections import Counter

    backend = _legacy_backend(family)
    channels = (
        backend.STATE_WORK_CONTRACT["channels"] if family == "MEM03"
        else backend.CHANNELS if family == "MEM04" else ("aggregate",)
    )
    if stage not in (Stage.RED, Stage.GREEN) or channel not in (*channels, "ordinary"):
        raise ValueError("MEM05 legacy observation has an unadmitted phase/channel")
    if stage is Stage.RED or path not in SOURCE_PROJECTION["mutable_functions"] or not _current_authority(root):
        return None
    validate_retained_source(root, path, text, Stage.GREEN)
    if channel == "ordinary":
        return text
    replaced = legacy_replaced_functions(root, family, path, text, Stage.GREEN)
    current = _current_observations(root, path, text)
    records = function_records(text)
    original = function_records(fixed_source(str(Path(root).resolve()), path).decode())
    retained = _legacy_retained_sites(root, family, path, text, channel)
    for name, sites in current.items():
        if name in replaced:
            added = sites
        elif name in SOURCE_PROJECTION["mutable_functions"][path]:
            before = Counter(primitive_shape(path, name, original[name].body))
            additions = []
            for site, shape in zip(sites, primitive_shape(path, name, records[name].body), strict=True):
                if before[shape]:
                    before[shape] -= 1
                else:
                    additions.append(site)
            if any(before.values()):
                raise ValueError("MEM05 changed an unretired legacy primitive instead of supplementing it")
            added = tuple(additions)
        else:
            added = ()
        retained[name].extend(site for site in added if _legacy_site_selected(family, channel, site))
    spans = function_spans(text)
    for name in sorted(spans, key=lambda key: spans[key].body_start, reverse=True):
        span = spans[name]
        text = text[:span.body_start] + _render_body(text[span.body_start:span.body_end], retained[name]) + text[span.body_end:]
    prefix = backend.NATIVE_PREFIX if family == "MEM04" else backend.OBSERVATION_PREFIX
    return prefix + LEGACY_VALUE_PREFIX + text


def project_legacy_observations(root, family, path, text, stage, channel="aggregate"):
    from tests.host.object_hart_validation_source import project_legacy_observations as next_admission

    admitted = next_admission(root, family, path, text, stage, channel)
    if admitted is not None:
        return admitted
    if stage is Stage.RED or path not in SOURCE_PROJECTION["mutable_functions"] or not _current_authority(root):
        return None
    original = regular_source(Path(root), path).read_text()
    expected = legacy_observe_source(root, family, path, original, stage, channel)
    if text != expected:
        raise ValueError("MEM05 legacy observations changed, moved or omitted retained/current primitive work")
    return original


def legacy_binding_prefix(text):
    if "MICROS_MEM05_OBSERVE_VALUE" not in text:
        return ""
    if text.count(LEGACY_VALUE_PREFIX) != 1:
        raise ValueError("MEM05 extracted legacy source lost its exact value-observation declaration")
    return LEGACY_VALUE_PREFIX


def current_scratch_storage(root):
    if not _current_authority(root):
        return False
    path = "kernel/memory/user_address_space_runtime.c"
    validate_retained_source(root, path, regular_source(Path(root), path).read_text(), Stage.GREEN)
    return True


def legacy_scratch_binding(text, stage):
    from tests.host import selected_address_space_source as previous

    if stage not in (Stage.RED, Stage.GREEN):
        raise ValueError("MEM05 legacy scratch binding has an unknown stage")
    if stage is Stage.RED or not current_scratch_storage(Path(__file__).resolve().parents[2]):
        return None
    return previous.NATIVE_PREFIX + previous.replace_body(
        text, "read_satp", "    return micros_as_native_read_satp();",
    ) + (
        "\nconst uint64_t *micros_as_native_scratch_bitmap(bool release)\n"
        "{ return release ? release_selection.bitmap : reachable_selection.bitmap; }\n"
        "bool micros_as_native_scratch_busy(void)\n"
        "{ return scratch_mode != MICROS_USER_ADDRESS_SPACE_SCRATCH_IDLE; }\n"
        "const struct micros_frame_selection *micros_as_native_scratch_selection(bool release)\n"
        "{ return release ? &release_selection : &reachable_selection; }\n"
        "uint8_t micros_as_native_scratch_mode(void)\n"
        "{ return scratch_mode; }\n"
    )


def legacy_audit_boundaries(root, stage):
    if stage not in (Stage.RED, Stage.GREEN):
        raise ValueError("MEM05 legacy audit has an unknown stage")
    if stage is Stage.RED or not _current_authority(root):
        return None
    measured = measure_audit_boundaries(root, stage)
    return {"retained": measured["retained"], "migrated": measured["migrated"]}


def legacy_expected_audits(root):
    from tests.host import selected_address_space_source as previous

    declaration = declared_audit_boundaries if _current_authority(root) else previous.declared_audit_boundaries
    return declaration(root)["retained"]


def project_include_transports(root, logical, raw, transports, required):
    from tests.host.object_hart_validation_source import project_include_transports as project_next

    transports = project_next(root, logical, raw, transports, required)
    if type(required) is not bool:
        raise AssertionError("MEM05 include transport requires checked source authority")
    if logical not in ("kernel/user_address_space.c", "kernel/user_address_space_runtime.c") or required or not _current_authority(root):
        return transports
    path = "kernel/memory/" + Path(logical).name
    validate_retained_source(root, path, raw.decode(), Stage.GREEN)
    line = 7 if logical == "kernel/user_address_space.c" else 1
    if transports != ((
        line, "kernel/user_address_space_internal.h",
        '#include "kernel/user_address_space_internal.h"\n',
        '#include "kernel/memory/user_address_space_internal.h"\n',
    ),):
        raise AssertionError("MEM05 predecessor include transport changed")
    addition = (
        (8, "kernel/kernel_object_runtime_internal.h",
         '#include "kernel/kernel_object_runtime_internal.h"\n',
         '#include "kernel/objects/kernel_object_runtime_internal.h"\n')
        if logical == "kernel/user_address_space.c" else
        (8, "kernel/frame_ownership_internal.h",
         '#include "kernel/frame_ownership_internal.h"\n',
         '#include "kernel/memory/frame_ownership_internal.h"\n')
    )
    return (*transports, addition)


def _legacy_changes(path):
    families = {
        "tests/host/bootstrap_allocation_source.py": "MEM01",
        "tests/host/kernel_mapping_source.py": "MEM02",
        "tests/host/selected_ownership_source.py": "MEM03",
        "tests/host/selected_address_space_source.py": "MEM04",
    }
    edits = []
    if path in families:
        family = families[path]
        if family == "MEM01":
            observe = "def observe_source(path, text, *, stage, root=None):\n"
            project = "def project_observations(path, text, *, stage, root=None):\n"
            root_line = "    root = Path(__file__).resolve().parents[2] if root is None else Path(root)\n"
            render = "    return _render_observations(path, text, stage, root=root)\n"
            arguments = 'root, "MEM01", path, text, stage'
        elif family == "MEM04":
            observe = "def observe_source(root, path, text, channel, stage=Stage.RED):\n"
            project = "def project_observations(root, path, text, channel, stage=Stage.RED):\n"
            root_line = ""
            render = "    if channel == \"ordinary\":\n        return text\n    spans = function_spans(text)\n"
            arguments = 'root, "MEM04", path, text, stage, channel'
        else:
            parameters = "root, path, text, stage" + (", channel" if family == "MEM03" else "")
            observe = "def observe_source(" + parameters + "):\n"
            project = "def project_observations(" + parameters + "):\n"
            root_line = ""
            render = "    return _render_observations(" + parameters + ")\n"
            arguments = 'root, "' + family + '", path, text, stage' + (", channel" if family == "MEM03" else "")
        edits.append((
            observe,
            observe + "    from tests.host.selected_release_handoff_source import legacy_observe_source\n\n",
        ))
        edits.append((
            render,
            "    current = legacy_observe_source(" + arguments + ")\n"
            "    if current is not None:\n        return current\n" + render,
        ))
        call = (
            "    current = project_legacy_observations(" + arguments + ")\n"
            "    if current is not None:\n        return current\n"
        )
        if family == "MEM04":
            edits.append((
                project,
                project + "    from tests.host.selected_release_handoff_source import project_legacy_observations\n\n",
            ))
            edits.append((
                "    original = source_bytes(root, path, stage).decode()\n",
                call + "    original = source_bytes(root, path, stage).decode()\n",
            ))
        else:
            edits.append((
                project + root_line,
                project + "    from tests.host.selected_release_handoff_source import project_legacy_observations\n\n"
                + root_line + call,
            ))
    if path in (
        "tests/host/bootstrap_allocation_source.py", "tests/host/kernel_mapping_source.py",
        "tests/host/selected_ownership_source.py",
    ):
        family = families[path]
        root_line = (
            "    root = Path(__file__).resolve().parents[2] if root is None else Path(root)\n"
            if family == "MEM01" else ""
        )
        before = "    from tests.host.work_count_types import Region\n\n    spans = function_spans(text)\n"
        edits.append((
            before,
            "    from tests.host.work_count_types import Region\n"
            "    from tests.host.selected_release_handoff_source import legacy_replaced_functions\n\n"
            + root_line + "    spans = function_spans(text)\n"
            '    replaced = legacy_replaced_functions(root, "' + family + '", path, text, stage)\n',
        ))
        unpack = {
            "MEM01": "        function, _, _, regions, primitive = site\n",
            "MEM02": "        function, identifier, kind, regions, primitive, placement, units = site\n",
            "MEM03": "        function, identifier, _, _, kind, regions, primitive, placement, units, witness = site\n",
        }[family]
        edits.append((unpack, unpack + "        if function in replaced:\n            continue\n"))
    if path == "tests/host/bootstrap_allocation_source.py":
        edits.append((
            "    for function in set(MEASURED_FUNCTIONS.get(path, ())) & set(spans):\n",
            "    for function in (set(MEASURED_FUNCTIONS.get(path, ())) & set(spans)) - replaced:\n",
        ))
    if path == "tests/host/selected_ownership_source.py":
        edits.extend((
            (
                '    if path.endswith(".h"):\n        if original.count("\\n#endif") != 1:\n',
                '    if path.endswith(".h"):\n'
                '        if "MEM03/header/" + path in retired:\n            return\n'
                '        if original.count("\\n#endif") != 1:\n',
            ),
            (
                "    for name in additions:\n        current = after[name]\n",
                '    for name in additions:\n        if "MEM03/function/" + path + "/" + name in retired:\n'
                "            continue\n        current = after[name]\n",
            ),
            (
                "    if _source_skeleton(text, after) != _source_skeleton(original, before):\n",
                '    if "MEM03/skeleton/" + path not in retired and _source_skeleton(text, after) != _source_skeleton(original, before):\n',
            ),
            (
                "def _validate_observation_coverage(root, path, text, stage):\n"
                "    from tests.host.work_count_lexical import function_spans, positional_tokens\n",
                "def _validate_observation_coverage(root, path, text, stage):\n"
                "    from tests.host.work_count_lexical import function_spans, positional_tokens\n"
                "    from tests.host.selected_release_handoff_source import legacy_replaced_functions\n",
            ),
            (
                "    fields, loops = set(), set()\n",
                '    replaced = legacy_replaced_functions(root, "MEM03", path, text, stage)\n'
                "    fields, loops = set(), set()\n",
            ),
            (
                "    for name, span in function_spans(text).items():\n"
                "        if names is not None and name not in names:\n",
                "    for name, span in function_spans(text).items():\n"
                "        if name in replaced or names is not None and name not in names:\n",
            ),
            (
                "    from tests.host.selected_address_space_source import native_recipe as selected_address_space_recipe\n",
                "    from tests.host.selected_address_space_source import native_recipe as selected_address_space_recipe\n"
                "    from tests.host.selected_release_handoff_source import native_recipe as selected_release_handoff_recipe\n",
            ),
            (
                "    return text.replace(anchor, native_recipe() + selected_address_space_recipe() + anchor, 1)\n",
                "    return text.replace(anchor, native_recipe() + selected_address_space_recipe() + selected_release_handoff_recipe() + anchor, 1)\n",
            ),
        ))
    if path == "tests/host/selected_address_space_source.py":
        edits.extend((
            (
                "def validate_retained_source(root, path, text, stage):\n"
                "    from tests.host.c_source_records import compact\n",
                "def validate_retained_source(root, path, text, stage):\n"
                "    from tests.host.c_source_records import compact\n"
                "    from tests.host.selected_release_handoff_source import native_assertion_admission as next_admission\n",
            ),
            (
                '        raise ValueError("MEM04 source admission has an unknown stage or representation")\n',
                '        raise ValueError("MEM04 source admission has an unknown stage or representation")\n'
                '    retired = next_admission(root, "MEM04", path, text, stage)\n',
            ),
            (
                '            raise ValueError("MEM04 fixed or noncandidate native source changed: " + path)\n',
                '            if "MEM04/native-input/" + path in retired:\n                return\n'
                '            raise ValueError("MEM04 fixed or noncandidate native source changed: " + path)\n',
            ),
            (
                '    if path in SOURCE_ROLES["dependent_production_paths"]:\n        if text != extend_handoff_preflight(original):\n',
                '    if path in SOURCE_ROLES["dependent_production_paths"]:\n'
                '        if "MEM04/dependent-file/" + path in retired:\n            return\n'
                "        if text != extend_handoff_preflight(original):\n",
            ),
            (
                '    if path.endswith(".h"):\n        if text != extended_header(path, original):\n',
                '    if path.endswith(".h"):\n        if "MEM04/header/" + path in retired:\n            return\n'
                "        if text != extended_header(path, original):\n",
            ),
            (
                "    if set(after) != set(before).difference(removed) | set(additions):\n",
                '    if "MEM04/definitions/" + path not in retired and set(after) != set(before).difference(removed) | set(additions):\n',
            ),
            (
                "        if name in removed:\n",
                '        if name in removed or "MEM04/function/" + path + "/" + name in retired:\n',
            ),
            (
                "    for name in additions:\n        current = after[name]\n",
                '    for name in additions:\n        if "MEM04/function/" + path + "/" + name in retired:\n'
                "            continue\n        current = after[name]\n",
            ),
            (
                "    if _source_skeleton(text) != _extended_skeleton(path, original):\n",
                '    if "MEM04/skeleton/" + path not in retired and _source_skeleton(text) != _extended_skeleton(path, original):\n',
            ),
            (
                "def observation_sites(root, path, text, stage):\n"
                "    from tests.host.c_source_records import tokens\n\n    records = function_records(text)\n",
                "def observation_sites(root, path, text, stage):\n"
                "    from tests.host.c_source_records import tokens\n"
                "    from tests.host.selected_release_handoff_source import legacy_replaced_functions\n\n"
                "    records = function_records(text)\n"
                '    replaced = legacy_replaced_functions(root, "MEM04", path, text, stage)\n',
            ),
            (
                "    for name in selected:\n        if name not in records:\n",
                "    for name in selected:\n        if name in replaced:\n            continue\n        if name not in records:\n",
            ),
            (
                "def native_assertion_admission(root, family, path, text, stage):\n"
                "    from tests.host.evidence_authority_scope import identity_required\n",
                "def native_assertion_admission(root, family, path, text, stage):\n"
                "    from tests.host.evidence_authority_scope import identity_required\n"
                "    from tests.host.selected_release_handoff_source import native_assertion_admission as next_admission\n\n"
                "    inherited = next_admission(root, family, path, text, stage)\n",
            ),
            (
                '    if stage is Stage.RED or family == "MEM01":\n        return frozenset()\n',
                '    if stage is Stage.RED or family == "MEM01":\n        return inherited\n',
            ),
            ("    return selected\n", "    return selected | inherited\n"),
            (
                "def project_cmake(text):\n",
                "def project_cmake(text):\n"
                "    from tests.host.selected_release_handoff_source import project_cmake as project_next\n\n"
                "    text = project_next(text)\n",
            ),
            (
                "def project_legacy_adapter(root, path, text):\n",
                "def project_legacy_adapter(root, path, text):\n"
                "    from tests.host.selected_release_handoff_source import project_legacy_adapter as project_next\n\n"
                "    text = project_next(root, path, text)\n",
            ),
            (
                "def bind_native(path, text, stage=Stage.RED):\n",
                "def bind_native(path, text, stage=Stage.RED):\n"
                "    from tests.host.selected_release_handoff_source import legacy_binding_prefix, legacy_scratch_binding\n\n",
            ),
            (
                '            prefix + \'#include "micros/kernel_object_runtime.h"\\n#include <string.h>\\n\'\n',
                '            prefix + legacy_binding_prefix(text) + \'#include "micros/kernel_object_runtime.h"\\n#include <string.h>\\n\'\n',
            ),
            (
                '    if path == "kernel/memory/user_address_space_runtime.c":\n'
                '        return prefix + replace_body(text, "read_satp", "    return micros_as_native_read_satp();") + (\n',
                '    if path == "kernel/memory/user_address_space_runtime.c":\n'
                "        current = legacy_scratch_binding(text, stage)\n"
                "        if current is not None:\n            return current\n"
                '        return prefix + replace_body(text, "read_satp", "    return micros_as_native_read_satp();") + (\n',
            ),
            (
                "def measure_audit_boundaries(root, stage):\n",
                "def measure_audit_boundaries(root, stage):\n"
                "    from tests.host.selected_release_handoff_source import legacy_audit_boundaries\n\n",
            ),
            (
                '        raise ValueError("MEM04 audit scope has an unknown stage")\n',
                '        raise ValueError("MEM04 audit scope has an unknown stage")\n'
                "    current = legacy_audit_boundaries(root, stage)\n"
                "    if current is not None:\n        return current\n",
            ),
        ))
    if path == "tools/generate_kernel_mapping_fixture.py":
        edits.extend((
            (
                "def _bootstrap_unit(text, observed):\n",
                "def _bootstrap_unit(text, observed):\n"
                "    from tests.host.selected_release_handoff_source import legacy_binding_prefix\n\n",
            ),
            (
                '        _prefix(observed)\n        + "static struct micros_frame_allocator bootstrap_allocator;\\n"\n',
                '        _prefix(observed) + legacy_binding_prefix(text)\n'
                '        + "static struct micros_frame_allocator bootstrap_allocator;\\n"\n',
            ),
        ))
    if path == "tools/generate_selected_ownership_fixture.py":
        edits.extend((
            (
                "def _bootstrap_unit(text, stage):\n",
                "def _bootstrap_unit(text, stage):\n"
                "    from tests.host.selected_release_handoff_source import legacy_binding_prefix\n\n",
            ),
            (
                '        \'#include "tests/host/selected_ownership_test.h"\\n\'\n',
                '        legacy_binding_prefix(text) + \'#include "tests/host/selected_ownership_test.h"\\n\'\n',
            ),
        ))
    if path == "tests/host/kernel_layout_source.py":
        edits.extend((
            (
                "    transports = project_include_transports(root, logical, raw, transports, required)\n",
                "    transports = project_include_transports(root, logical, raw, transports, required)\n"
                "    from tests.host.selected_release_handoff_source import project_include_transports as project_selected\n\n"
                "    transports = project_selected(root, logical, raw, transports, required)\n",
            ),
        ))
    if path == "tests/host/test_kernel_address_space_source_layout.py":
        edits.extend((
            (
                "    def test_scratch_storage_has_one_configured_owner(self) -> None:\n",
                "    def test_scratch_storage_has_one_configured_owner(self) -> None:\n"
                "        from tests.host.selected_release_handoff_source import current_scratch_storage\n\n",
            ),
            (
                "        self.assertIn(SCRATCH_OWNER, ACCEPTED_SCRATCH_OWNERS)\n",
                "        obsolete = set(patterns) if current_scratch_storage(REPOSITORY_ROOT) else set()\n"
                "        if obsolete:\n"
                "            patterns.update({\n"
                '                "reachable_selection": re.compile(r"(?m)^static\\s+struct\\s+micros_frame_selection\\s+reachable_selection\\s*;"),\n'
                '                "release_selection": re.compile(r"(?m)^static\\s+struct\\s+micros_frame_selection\\s+release_selection\\s*;"),\n'
                '                "scratch_mode": re.compile(r"(?m)^static\\s+uint8_t\\s+scratch_mode\\s*;"),\n'
                "            })\n"
                "        self.assertIn(SCRATCH_OWNER, ACCEPTED_SCRATCH_OWNERS)\n",
            ),
            (
                "                self.assertEqual([SCRATCH_OWNER], owners)\n",
                "                self.assertEqual([] if name in obsolete else [SCRATCH_OWNER], owners)\n",
            ),
        ))
    return tuple(edits)


@lru_cache(maxsize=64)
def baseline_adapter_source(root, path):
    if path not in (*SOURCE_ROLES["fixed_adapters"], *SOURCE_ROLES["immutable_adapters"]):
        raise ValueError("MEM05 has no independently declared adapter owner")
    return subprocess.check_output(
        ("git", "show", SOURCE_PROJECTION["adapter_reference"] + ":" + path),
        cwd=root, text=True, timeout=30,
    )


def extend_legacy_adapter(root, path, text):
    if text != baseline_adapter_source(str(Path(root).resolve()), path):
        raise ValueError("MEM05 adapter lacks its complete fixed predecessor")
    for before, after in _legacy_changes(path):
        if text.count(before) != 1 or after in text:
            raise ValueError("MEM05 adapter lost its exact original clause: " + path)
        text = text.replace(before, after, 1)
    return text


def project_legacy_adapter(root, path, text):
    from tests.host.object_hart_validation_source import project_legacy_adapter as project_next

    text = project_next(root, path, text)
    if (
        path not in (*SOURCE_ROLES["fixed_adapters"], "tests/host/test_kernel_address_space_source_layout.py")
        or path.startswith("kernel/tests/")
    ):
        return text
    if "tests.host.selected_release_handoff_source" not in text:
        return text
    original = baseline_adapter_source(str(Path(root).resolve()), path)
    if text != extend_legacy_adapter(root, path, original):
        raise ValueError("MEM05 legacy adapter is changed, partial or alters unrelated authority")
    return original


def candidate_paths():
    return tuple(sorted((
        *SOURCE_ROLES["production_paths"], *SOURCE_ROLES["fixed_adapters"],
        *SOURCE_ROLES["unchanged_candidates"],
    )))


def _paths(value, count):
    return (
        type(value) is tuple and len(value) == count and len(set(value)) == count
        and all(type(path) is str and path and not Path(path).is_absolute()
                and "\\" not in path and ".." not in Path(path).parts
                and Path(path).as_posix() == path for path in value)
    )


def read_declarations(root):
    result = {}
    for path, names in (
        (AUTHORITY_PATH, ("DESIGN_PINS", "ORACLE_RULES")),
        (SOURCE_PATH, ("SOURCE_ROLES", "RECIPE_ADMISSION", "STATE_WORK_CONTRACT", "SOURCE_PROJECTION")),
    ):
        tree = ast.parse(regular_source(Path(root), path).read_text())
        result.update((name, literal_value(single_assignment(tree, name).value)) for name in names)
    validate_declarations(result)
    return result


def validate_declarations(values):
    if type(values) is not dict or set(values) != {
        "DESIGN_PINS", "ORACLE_RULES", "SOURCE_ROLES", "RECIPE_ADMISSION",
        "STATE_WORK_CONTRACT", "SOURCE_PROJECTION",
    } or any(type(values[name]) is not dict for name in values if name != "ORACLE_RULES"):
        raise ValueError("MEM05 has incomplete literal authority declarations")
    pins = values["DESIGN_PINS"]
    documents = ("design_document", "adr_document", "research_document")
    revisions = (
        "design_commit", "design_tree", "implementation_base_commit", "implementation_base_tree",
        "runtime_reference", "runtime_tree", "minix_reference", "minix_tree",
    )
    if (
        set(pins) != set((*documents, *revisions))
        or any(type(pins[name]) is not str or len(pins[name]) != 40
               or any(character not in "0123456789abcdef" for character in pins[name]) for name in revisions)
        or not _paths(tuple(pins[name] for name in documents), 3)
    ):
        raise ValueError("MEM05 fixed design/runtime pins are incomplete")
    roles, projection = values["SOURCE_ROLES"], values["SOURCE_PROJECTION"]
    if set(roles) != {
        "base_membership_sha256", "production_paths", "fixed_adapters", "unchanged_candidates",
        "immutable_additions", "immutable_adapters", "dependent_target_paths",
    } or any(
        not _paths(roles.get(name), count) for name, count in (
            ("production_paths", 17), ("fixed_adapters", 9), ("unchanged_candidates", 4),
            ("immutable_additions", 8), ("immutable_adapters", 23), ("dependent_target_paths", 2),
        )
    ):
        raise ValueError("MEM05 source roles leave their finite declaration")
    candidates = (*roles["production_paths"], *roles["fixed_adapters"], *roles["unchanged_candidates"])
    immutable = (*roles["immutable_additions"], *roles["immutable_adapters"])
    if (
        len(set(candidates)) != 30 or len(set(immutable)) != 31 or set(candidates) & set(immutable)
        or roles["dependent_target_paths"] != (
            "kernel/tests/memory/address_space_handoff_test_runtime.c",
            "kernel/tests/memory/user_address_space_test_validation.c",
        )
        or not set(roles["dependent_target_paths"]) <= set(roles["fixed_adapters"])
        or type(projection.get("target_adapter_sha256")) is not dict
        or set(projection["target_adapter_sha256"]) != set(roles["dependent_target_paths"])
        or any(type(value) is not str or re.fullmatch(r"[a-f0-9]{64}", value) is None
               for value in projection["target_adapter_sha256"].values())
        or not _paths(projection.get("retirement_keys"), 82)
        or tuple(sorted(projection["retirement_keys"])) != projection["retirement_keys"]
        or projection.get("source_reference") != pins["runtime_reference"]
        or projection.get("adapter_reference") != pins["implementation_base_commit"]
        or set(projection.get("declared_header_sha256", ())) != set(HEADER_PATHS)
        or type(projection.get("immutable_adapter_sha256")) is not dict
        or set(projection["immutable_adapter_sha256"]) != set(roles["immutable_adapters"]) - {
            "CMakeLists.txt", "tests/host/evidence_authority_inventory.py",
        }
        or any(type(value) is not str or re.fullmatch(r"[a-f0-9]{64}", value) is None
               for value in projection["immutable_adapter_sha256"].values())
    ):
        raise ValueError("MEM05 candidates, immutable inputs or retirement identities overlap or drift")
    rules = values["ORACLE_RULES"]
    if type(rules) is not tuple or len(rules) != 3:
        raise ValueError("MEM05 requires its three separately owned replacement oracles")
    identifiers, tests, positions = set(), set(), set()
    for row in rules:
        if (
            type(row) is not tuple or len(row) != 6
            or any(type(item) is not str or not item for item in row[:5])
            or row[0] in identifiers or row[1] in tests or row[3] != "ADR-0114"
            or type(row[5]) is not tuple or not row[5] or len(set(row[5])) != len(row[5])
            or any(type(index) is not int or not 0 <= index < 82 for index in row[5])
            or positions & set(row[5])
        ):
            raise ValueError("MEM05 oracle scope is malformed or duplicated")
        identifiers.add(row[0])
        tests.add(row[1])
        positions.update(row[5])
    if positions != set(range(82)) or tuple(len(row[5]) for row in rules) != (41, 21, 20):
        raise ValueError("MEM05 oracle partition differs from its fixed scope")
    recipe, contract = values["RECIPE_ADMISSION"], values["STATE_WORK_CONTRACT"]
    if (
        not _paths(recipe.get("native_source_members"), 58)
        or not _paths(recipe.get("native_targets"), 5)
        or recipe.get("native_tiers") != ("test-unit",) or recipe.get("native_label") != "slow"
        or recipe.get("native_header_count") != 156
        or contract.get("dimensions") != (27, 14) or contract.get("owner_mask") != 11900
        or contract.get("common_cases") != 128
        or contract.get("channels") != ("aggregate", "frames", "plans", "context")
        or contract.get("producer_base") != 0x4d454d3035000000
        or contract.get("current_producer_offset") != 256
    ):
        raise ValueError("MEM05 native recipe, primitive dimensions or case coverage changed")
    validate_current_contracts(projection, roles)


def validate_current_contracts(projection, roles):
    from tests.host.work_count_types import Kind

    helpers = projection.get("current_helpers")
    extras = projection.get("extra_current_sites")
    mutable = projection.get("mutable_functions")
    removed = projection.get("removed_functions")
    shapes = projection.get("mutable_shapes")
    skeletons = projection.get("skeleton_edits")
    production = {path for path in roles["production_paths"] if path.endswith(".c")}
    if (
        type(helpers) is not dict or not helpers or type(extras) is not dict
        or type(mutable) is not dict or set(mutable) != production
        or type(removed) is not dict or not set(removed) <= production
        or type(shapes) is not dict or type(skeletons) is not dict or not set(skeletons) <= production
    ):
        raise ValueError("MEM05 current helper/primitive declarations are absent")
    known = set()
    for path, names in (*mutable.items(), *removed.items()):
        if (
            type(names) is not tuple
            or any(type(name) is not str or re.fullmatch(r"[a-zA-Z_]\w*", name) is None for name in names)
            or len(set(names)) != len(names)
        ):
            raise ValueError("MEM05 mutable/removed function partition is malformed")
    if any(set(mutable[path]) & set(removed.get(path, ())) for path in production):
        raise ValueError("MEM05 mutable and removed function scopes overlap")
    if sum(map(len, mutable.values())) != 32 or sum(map(len, removed.values())) != 9:
        raise ValueError("MEM05 current function partition changed its finite scope")
    mutable_keys = {path + "/" + name for path, names in mutable.items() for name in names}
    if not set(shapes) <= mutable_keys or any(type(shape) is not str for shape in shapes.values()):
        raise ValueError("MEM05 current primitive shape has no mutable owner")
    known.update(mutable_keys)
    for changes in skeletons.values():
        if type(changes) is not tuple or not changes:
            raise ValueError("MEM05 source skeleton lacks its finite inverse")
        for change in changes:
            if (
                type(change) is not tuple or len(change) != 2
                or any(type(value) is not str or not value for value in change) or change[0] == change[1]
            ):
                raise ValueError("MEM05 source skeleton has a malformed replacement")
    for path, functions in helpers.items():
        if path not in roles["production_paths"] or not path.endswith(".c") or type(functions) is not dict or not functions:
            raise ValueError("MEM05 current helper declaration leaves its production owner")
        for name, contract in functions.items():
            if (
                type(name) is not str or re.fullmatch(r"[a-zA-Z_]\w*", name) is None
                or type(contract) is not tuple or len(contract) != 3
                or type(contract[0]) is not str or not contract[0]
                or type(contract[1]) is not tuple
                or any(type(callee) is not str or re.fullmatch(r"[a-zA-Z_]\w*", callee) is None for callee in contract[1])
                or len(set(contract[1])) != len(contract[1])
                or type(contract[2]) is not str or re.search(r"^\s*#", contract[2], re.M)
                or set(function_records(contract[0] + " {\n" + contract[2] + "\n}")) != {name}
            ):
                raise ValueError("MEM05 current helper has a malformed signature/callee/primitive contract")
            known.add(path + "/" + name)
    for key, sites in extras.items():
        if key not in known or type(sites) is not tuple or not sites:
            raise ValueError("MEM05 explicit primitive has no declared current helper")
        for site in sites:
            if (
                type(site) is not tuple or len(site) != 6
                or type(site[0]) is not str or site[0] not in ("frames", "plans", "context")
                or type(site[1]) is not str or site[1] not in Kind.__members__
                or type(site[2]) is not str or not site[2]
                or type(site[3]) is not int or site[3] < 0
                or type(site[4]) is not str or site[4] not in ("before", "after")
                or type(site[5]) is not int or not 1 <= site[5] < 1 << 64
            ):
                raise ValueError("MEM05 explicit primitive has an unadmitted dimension or occurrence")


def declaration_facts(root, owner):
    if owner not in (AUTHORITY_PATH, SOURCE_PATH):
        raise ValueError("MEM05 has no declaration in this source owner")
    values = read_declarations(root)
    pins, roles = values["DESIGN_PINS"], values["SOURCE_ROLES"]
    recipe = values["RECIPE_ADMISSION"]
    references = tuple(pins[name] for name in ("design_document", "adr_document", "research_document"))
    consumers = tuple(f"B{index:02d}" for index in range(1, 13)) + ("B14",)
    candidates = (*roles["production_paths"], *roles["fixed_adapters"], *roles["unchanged_candidates"])

    def fact(kind, scope, classification, payloads, value):
        return Fact(
            owner + ":" + kind + ":" + scope, FAMILY, owner, kind, scope, classification,
            "B02", consumers, tuple(sorted({owner, *references, *payloads})), digest(value),
        )

    if owner == AUTHORITY_PATH:
        return (
            fact("design-anchor", "MEM05", AuthorityClass.TRANSPORT_IDENTITY, (), pins),
            *(fact("baseline-oracle", row[0], AuthorityClass.BASELINE_ORACLE,
                   roles["immutable_additions"], row) for row in values["ORACLE_RULES"]),
        )
    return (
        fact("source-membership", "MEM05", AuthorityClass.PERMANENT_INVARIANT,
             (*candidates, *roles["immutable_additions"], *roles["immutable_adapters"]), roles),
        fact("artifact-contract", "MEM05-recipes", AuthorityClass.ARTIFACT_CONTRACT,
             ("CMakeLists.txt", "CMakePresets.json", "include/micros/work_observation.h",
              "tests/work/work_counts.h", *recipe["fixture_sources"], *recipe["native_source_members"],
              recipe["generator"], recipe["driver"]), recipe),
        fact("state-work-contract", "MEM05", AuthorityClass.PERMANENT_INVARIANT,
             (*roles["production_paths"], *roles["immutable_additions"][:2],
              "tests/host/selected_release_handoff_evidence.py", "include/micros/work_observation.h",
              "tests/work/work_counts.h"), values["STATE_WORK_CONTRACT"]),
        fact("source-projection", "MEM05", AuthorityClass.TRANSPORT_IDENTITY,
             (*candidates, "tests/host/c_source_records.py", "tests/host/work_count_evidence.py",
              recipe["generator"]), values["SOURCE_PROJECTION"]),
    )


def extended_inventory(root):
    from tests.host.evidence_authority_types import fact_set_digest

    text = baseline_adapter_source(str(Path(root).resolve()), "tests/host/evidence_authority_inventory.py")
    fingerprint = fact_set_digest(tuple(
        fact for owner in (AUTHORITY_PATH, SOURCE_PATH) for fact in declaration_facts(root, owner)
    ))
    owner = (
        '    "selected-ownership-observation-admission": (\n'
        '        "tests/host/selected_ownership_observation_bindings.py",\n'
        "    ),\n"
    )
    previous_fingerprint = (
        '    "selected-ownership-observation-admission":\n'
        '        "0760c510cdc26d7dc638959e8bcda5c8df72e0b3016ba81533f373130c78a0e4",\n'
    )
    dispatch = (
        "    if family == selected_address_space_source.FAMILY:\n"
        "        return selected_address_space_source.declaration_facts(root, relative)\n"
    )
    changes = (
        (
            "    panic_trace_source, selected_address_space_source, selected_ownership_source, terminal_failure_source,\n",
            "    panic_trace_source, selected_address_space_source, selected_ownership_source,\n"
            "    selected_release_handoff_source, terminal_failure_source,\n",
        ),
        (
            owner,
            owner + '    "selected-release-handoff-baseline": (\n'
            '        "tests/host/selected_release_handoff_authority.py",\n'
            '        "tests/host/selected_release_handoff_source.py",\n'
            "    ),\n",
        ),
        (
            previous_fingerprint,
            previous_fingerprint + '    "selected-release-handoff-baseline":\n        "' + fingerprint + '",\n',
        ),
        (
            dispatch,
            dispatch + "    if family == selected_release_handoff_source.FAMILY:\n"
            "        return selected_release_handoff_source.declaration_facts(root, relative)\n",
        ),
    )
    for before, after in changes:
        if text.count(before) != 1 or after in text:
            raise ValueError("MEM05 inventory registration lost its exact historical boundary")
        text = text.replace(before, after, 1)
    return text


def read_scaffold_input(root, path):
    from tools.stack_artifact_files import read_regular

    return read_regular(regular_source(Path(root), path))[0]


def validate_scaffold(root):
    from tests.host.selected_release_handoff_authority import DESIGN_PINS, design_blob

    root = Path(root).resolve()
    read_declarations(root)
    validate_primitive_declarations(str(root))
    original = baseline_adapter_source(str(root), "CMakeLists.txt")
    if read_scaffold_input(root, "CMakeLists.txt").decode() != extend_cmake(original):
        raise ValueError("MEM05 five-product/full-only recipe is absent or altered")
    for path in SOURCE_ROLES["fixed_adapters"]:
        actual = read_scaffold_input(root, path)
        if path in SOURCE_ROLES["dependent_target_paths"]:
            if hashlib.sha256(actual).hexdigest() != SOURCE_PROJECTION["target_adapter_sha256"][path]:
                raise ValueError("MEM05 frozen target fixture changed")
        elif actual.decode() != extend_legacy_adapter(root, path, baseline_adapter_source(str(root), path)):
            raise ValueError("MEM05 frozen legacy source adapter changed: " + path)
    for path in SOURCE_ROLES["unchanged_candidates"]:
        if read_scaffold_input(root, path) != fixed_source(str(root), path):
            raise ValueError("MEM05 changed an original literal binding module: " + path)
    if read_scaffold_input(root, "tests/host/evidence_authority_inventory.py").decode() != extended_inventory(root):
        raise ValueError("MEM05 closed inventory registration is absent or altered")
    for path, expected in SOURCE_PROJECTION["immutable_adapter_sha256"].items():
        if hashlib.sha256(read_scaffold_input(root, path)).hexdigest() != expected:
            raise ValueError("MEM05 immutable fixture/authority adapter changed: " + path)
    for path in SOURCE_ROLES["immutable_additions"]:
        read_scaffold_input(root, path)
    contract = design_blob(root, DESIGN_PINS["design_document"]).decode()
    section = contract.split("### Fixed function and clause selectors\n", 1)[1].split(
        "This table has forty existing functions.", 1,
    )[0]
    expected = {
        ("kernel/memory/" + filename, name)
        for filename, names in re.findall(r"^\| `([^`]+\.c)` \| ([^\n]+) \|$", section, re.M)
        for name in re.findall(r"`([^`]+)`", names)
    }
    current = {
        (path, name) for group in ("mutable_functions", "removed_functions")
        for path, names in SOURCE_PROJECTION[group].items() for name in names
    } - {("kernel/memory/user_address_space_runtime.c", "micros_user_address_space_require_idle")}
    if len(expected) != 40 or current != expected:
        raise ValueError("MEM05 current function partition differs from the Accepted selectors")
    for path in HEADER_PATHS:
        validate_retained_source(root, path, declared_header(root, path), Stage.GREEN)


def _native_target_recipe(index, channel, target):
    observed = channel != "ordinary"
    root = "${MEM05_NATIVE_ROOT}/" + channel
    result = (
        "    add_executable(" + target + " ${MEM05_SOURCES_" + str(index) + "}\n"
        "        tests/host/selected_release_handoff_test.c\n"
        + ("        tests/work/work_counts.c\n" if observed else "")
        + "    )\n"
        "    target_include_directories(" + target + " PRIVATE\n"
        '        "' + root + '" "' + root + '/include"\n'
        '        "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/include")\n'
        "    target_compile_features(" + target + " PRIVATE c_std_17)\n"
        "    target_compile_options(" + target
        + " PRIVATE -Wall -Wextra -Werror -Wpedantic -Wmissing-prototypes -Wstrict-prototypes -fno-builtin)\n"
        "    target_compile_definitions(" + target + " PRIVATE MICROS_BUILD_USER_ADDRESS_SPACE_TEST=1)\n"
        '    if(MICROS_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")\n'
        "        target_compile_options(" + target + " PRIVATE -fno-omit-frame-pointer -fsanitize=address,undefined)\n"
        "        target_link_options(" + target + " PRIVATE -fsanitize=address,undefined)\n"
        "    endif()\n"
    )
    if observed:
        return result + (
            "    target_compile_definitions(" + target + " PRIVATE MICROS_TEST_WORK_COUNTS=1 "
            "MICROS_MEM05_NATIVE_CHANNEL=" + str(index) + ")\n"
        )
    return result + (
        "    target_compile_definitions(" + target + " PRIVATE\n"
        "        MICROS_SELECTED_RELEASE_HANDOFF_LAUNCHER=1\n"
        '        "MICROS_SELECTED_RELEASE_HANDOFF_PYTHON=\\"${MICROS_PYTHON}\\""\n'
        '        "MICROS_SELECTED_RELEASE_HANDOFF_DRIVER=\\"${CMAKE_CURRENT_SOURCE_DIR}/tools/check_selected_release_handoff.py\\""\n'
        '        "MICROS_SELECTED_RELEASE_HANDOFF_ROOT=\\"${CMAKE_CURRENT_SOURCE_DIR}\\""\n'
        "    )\n"
    )


def native_recipe():
    result = "".join(
        "    add_custom_command(\n"
        "        OUTPUT ${" + owner + "_NATIVE_OUTPUTS} APPEND\n"
        "        DEPENDS tests/host/selected_release_handoff_source.py\n"
        "            tests/host/selected_release_handoff_authority.py\n"
        "            tests/host/evidence_authority_scope.py\n"
        "            tools/validation_context.py\n"
        "            tools/validation_inputs.py\n"
        "    )\n"
        for owner in ("MEM01", "MEM02", "MEM03", "MEM04")
    )
    result += (
        '    set(MEM05_NATIVE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/selected-release-handoff")\n'
        "    set(MEM05_NATIVE_INPUTS\n"
        + "".join("        " + path + "\n" for path in RECIPE_ADMISSION["native_source_members"])
        + "    )\n"
        "    file(GLOB_RECURSE MEM05_NATIVE_HEADERS\n"
        '        "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"\n'
        '        "${CMAKE_CURRENT_SOURCE_DIR}/arch/*.h"\n'
        '        "${CMAKE_CURRENT_SOURCE_DIR}/kernel/*.h"\n'
        "    )\n"
        "    set(MEM05_NATIVE_OUTPUTS)\n"
    )
    for index, channel in enumerate(CHANNELS):
        root = "${MEM05_NATIVE_ROOT}/" + channel
        result += (
            "    set(MEM05_SOURCES_" + str(index) + ")\n"
            "    foreach(mem05_source IN LISTS MEM05_NATIVE_INPUTS)\n"
            "        list(APPEND MEM05_SOURCES_" + str(index) + ' "' + root + '/${mem05_source}")\n'
            "    endforeach()\n"
            "    list(APPEND MEM05_NATIVE_OUTPUTS ${MEM05_SOURCES_" + str(index) + "})\n"
            "    foreach(mem05_header IN LISTS MEM05_NATIVE_HEADERS)\n"
            '        file(RELATIVE_PATH mem05_relative_header "${CMAKE_CURRENT_SOURCE_DIR}" "${mem05_header}")\n'
            '        list(APPEND MEM05_NATIVE_OUTPUTS "' + root + '/${mem05_relative_header}")\n'
            "    endforeach()\n"
        )
    result += (
        "    add_custom_command(\n"
        "        OUTPUT ${MEM05_NATIVE_OUTPUTS}\n"
        "        JOB_POOL mem03_source_proof\n"
        '        COMMAND "${MICROS_PYTHON}" ${MICROS_PYTHON_ARGS}\n'
        '            "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_selected_release_handoff_fixture.py"\n'
        "        DEPENDS ${MEM05_NATIVE_INPUTS} ${MEM05_NATIVE_HEADERS}\n"
        + "".join("            " + path + "\n" for path in SOURCE_ROLES["immutable_additions"])
        + "            tests/host/evidence_authority_scope.py\n"
        "            tests/host/selected_ownership_source.py\n"
        "            tests/host/selected_address_space_source.py\n"
        "            tests/host/c_source_records.py\n"
        "            tests/host/work_count_lexical.py\n"
        "            tests/host/work_count_projection.py\n"
        "            tools/validation_context.py\n"
        "            tools/validation_inputs.py\n"
        '        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"\n'
        "        VERBATIM\n"
        "    )\n"
    )
    for index, (channel, target) in enumerate(zip(CHANNELS, RECIPE_ADMISSION["native_targets"], strict=True)):
        result += _native_target_recipe(index, channel, target)
    return result + (
        "    add_test(NAME selected-release-handoff COMMAND selected_release_handoff_ordinary_test\n"
        + "".join('        "$<TARGET_FILE:' + target + '>"\n' for target in RECIPE_ADMISSION["native_targets"][:-1])
        + "    )\n"
        "    set_tests_properties(selected-release-handoff PROPERTIES LABELS slow)\n"
        "    add_dependencies(test-unit " + " ".join(RECIPE_ADMISSION["native_targets"]) + ")\n\n"
    )


def extend_cmake(text):
    anchor = '    include("${CMAKE_CURRENT_SOURCE_DIR}/tests/stack/StackEvidence.cmake")\n'
    if hashlib.sha256(text.encode()).hexdigest() != RECIPE_ADMISSION["base_cmake_sha256"] or text.count(anchor) != 1:
        raise ValueError("MEM05 native recipe lacks its complete reviewed base")
    if "    set_property(GLOBAL APPEND PROPERTY JOB_POOLS mem03_source_proof=1)\n" not in text:
        raise ValueError("MEM05 requires the existing depth-one source-proof pool")
    return text.replace(anchor, native_recipe() + anchor, 1)


def project_cmake(text):
    from tests.host.object_hart_validation_source import project_cmake as project_next

    text = project_next(text)
    if not any(marker in text for marker in ("MEM05_NATIVE", "selected_release_handoff", "selected-release-handoff")):
        return text
    addition = native_recipe()
    anchor = '    include("${CMAKE_CURRENT_SOURCE_DIR}/tests/stack/StackEvidence.cmake")\n'
    if text.count(addition + anchor) != 1:
        raise ValueError("MEM05 native recipe is changed, partial or relocated")
    original = text.replace(addition, "", 1)
    if hashlib.sha256(original.encode()).hexdigest() not in (
        RECIPE_ADMISSION["base_cmake_sha256"], RECIPE_ADMISSION["legacy_cmake_sha256"],
    ):
        raise ValueError("MEM05 native recipe changed unrelated predecessor bytes")
    return original


def _call_rows(path, text, callees):
    from tests.host.work_count_lexical import c_lexical_text

    rows = []
    for name, record in function_records(text).items():
        body = c_lexical_text(record.body)
        for callee in callees:
            references = len(re.findall(r"\b" + callee + r"\b", body))
            calls = len(re.findall(r"\b" + callee + r"\s*\(", body))
            if references != calls:
                raise ValueError("MEM05 complete-audit or private-plan authority escaped its direct caller")
            if calls:
                rows.append((path, name, callee, calls))
    return tuple(rows)


AUDIT_CALLEES = (
    "micros_user_address_space_validate", "micros_user_address_space_validate_locked",
    "micros_user_address_space_validate_all_process_roots_locked",
)


@lru_cache(maxsize=4)
def declared_audit_boundaries(root):
    from tests.host import selected_address_space_source as previous

    root = str(Path(root).resolve())
    paths = previous.original_audit_paths(root)
    original = tuple(sorted(
        row for path in paths
        for row in _call_rows(path, fixed_source(root, path).decode(), AUDIT_CALLEES)
    ))
    if original != previous.declared_audit_boundaries(root)["retained"] or len(original) != 20:
        raise ValueError("MEM05 fixed audit ledger is not MEM04's twenty retained callers")
    migrated = tuple(row for row in original if row[1] in SOURCE_PROJECTION["migrated_audit_callers"])
    retained = tuple(row for row in original if row not in migrated)
    plans = SOURCE_PROJECTION["plan_boundaries"]
    if len(migrated) != 4 or len(retained) != 16 or len(plans) != 4:
        raise ValueError("MEM05 audit migration leaves its four-call boundary")
    return {"original": original, "retained": retained, "migrated": migrated, "plans": plans}


def measure_audit_boundaries(root, stage):
    from tests.host import selected_address_space_source as previous

    if stage not in (Stage.RED, Stage.GREEN):
        raise ValueError("MEM05 audit scope has an unknown source stage")
    root = Path(root).resolve()
    paths = previous.original_audit_paths(str(root))
    current = tuple(
        name for name in subprocess.check_output(("git", "ls-files"), cwd=root, text=True, timeout=30).splitlines()
        if name.startswith("kernel/") and not name.startswith("kernel/tests/") and name.endswith(".c")
    )
    if current != paths:
        raise ValueError("MEM05 complete audit namespace changed")
    expected = declared_audit_boundaries(str(root))
    subjects = {
        path: fixed_source(str(root), path).decode() if stage is Stage.RED else regular_source(root, path).read_text()
        for path in paths
    }
    rows = tuple(sorted(row for path, text in subjects.items() for row in _call_rows(path, text, AUDIT_CALLEES)))
    planned_calls = tuple(sorted({row[2] for row in expected["plans"]}))
    plans = tuple(sorted(row for path, text in subjects.items() for row in _call_rows(path, text, planned_calls)))
    wanted_rows = expected["original"] if stage is Stage.RED else expected["retained"]
    wanted_plans = tuple(row for row in expected["plans"] if row[2] == "inspect_page_release") if stage is Stage.RED else expected["plans"]
    if rows != wanted_rows or plans != wanted_plans:
        raise ValueError("MEM05 removed, added or retained an unowned complete audit/private-plan caller")
    return {
        "retained": expected["retained"],
        "migrated": expected["migrated"] if stage is Stage.RED else (),
        "plans": plans,
    }

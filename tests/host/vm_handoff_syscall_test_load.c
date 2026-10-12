#include "vm_handoff_syscall_test_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

bool vm_handoff_syscall_test_load_begin_dispatches(
    struct vm_handoff_syscall_test_context *fixture
)
{
    const struct micros_vm_load_begin_result *result =
        (const struct micros_vm_load_begin_result *)
            &fixture->user_memory[MICROS_SV39_PAGE_SIZE - 8];
    struct micros_hart hart = {
        .hardware_id = 0,
    };
    struct micros_trap_frame frame = {0};
    struct micros_syscall_context context = {
        .objects = &fixture->objects,
        .current = fixture->vm_thread,
        .process = fixture->vm_process,
    };
    struct micros_syscall_arguments arguments =
        vm_handoff_syscall_test_load_begin_arguments();

    vm_handoff_syscall_test_reset(fixture);
    fixture->bootstrap.phase = MICROS_BOOTSTRAP_PHASE_SEALED;
    return (
        micros_vm_handoff_handle_captured_user_ecall(
            &hart,
            &frame,
            &context,
            &arguments
        ) == MICROS_SYSCALL_RETURN_NORMAL
        && frame.a0 == MICROS_SYSCALL_ABI_OK
        && result->version == MICROS_VM_LOAD_BEGIN_RESULT_VERSION
        && result->size == MICROS_VM_LOAD_BEGIN_RESULT_SIZE
        && result->load_token == 1
        && result->process_slot == fixture->child_process.slot
        && result->process_generation == fixture->child_process.generation
        && fixture->vm_state.load.phase == MICROS_VM_LOAD_PHASE_LOADING
    );
}

bool vm_handoff_syscall_test_load_begin_failures(
    struct vm_handoff_syscall_test_context *fixture
)
{
    struct micros_trap_frame frame = {0};
    struct micros_syscall_arguments arguments =
        vm_handoff_syscall_test_load_begin_arguments();
    size_t output_offset = sizeof(fixture->user_memory) - 16;
    size_t index;

    vm_handoff_syscall_test_reset(fixture);
    fixture->bootstrap.phase = MICROS_BOOTSTRAP_PHASE_SEALED;
    arguments.a1 = 2;
    if (
        !vm_handoff_syscall_test_invoke_vm_command(fixture, &arguments, &frame)
        || frame.a0
            != (uint64_t)(int64_t)MICROS_SYSCALL_ABI_STATE
        || fixture->vm_state.load.phase != MICROS_VM_LOAD_PHASE_IDLE
    ) {
        return false;
    }
    for (index = 0; index < MICROS_VM_LOAD_BEGIN_RESULT_SIZE; ++index) {
        if (
            fixture->user_memory[MICROS_SV39_PAGE_SIZE - 8 + index]
                != UINT8_C(0xa5)
        ) {
            return false;
        }
    }

    vm_handoff_syscall_test_reset(fixture);
    fixture->bootstrap.phase = MICROS_BOOTSTRAP_PHASE_SEALED;
    arguments = vm_handoff_syscall_test_load_begin_arguments();
    arguments.a2 = MICROS_USER_VIRTUAL_BASE + output_offset;
    frame = (struct micros_trap_frame){0};
    if (
        !vm_handoff_syscall_test_invoke_vm_command(fixture, &arguments, &frame)
        || frame.a0
            != (uint64_t)(int64_t)
                MICROS_SYSCALL_ABI_MEMORY_FAULT
        || fixture->vm_state.load.phase != MICROS_VM_LOAD_PHASE_IDLE
    ) {
        return false;
    }
    for (index = output_offset; index < sizeof(fixture->user_memory); ++index) {
        if (fixture->user_memory[index] != UINT8_C(0xa5)) {
            return false;
        }
    }

    vm_handoff_syscall_test_reset(fixture);
    fixture->bootstrap.phase = MICROS_BOOTSTRAP_PHASE_SEALED;
    arguments = vm_handoff_syscall_test_load_begin_arguments();
    arguments.a1 = 2;
    arguments.a2 = MICROS_USER_VIRTUAL_BASE + output_offset;
    frame = (struct micros_trap_frame){0};
    if (
        !vm_handoff_syscall_test_invoke_vm_command(fixture, &arguments, &frame)
        || frame.a0
            != (uint64_t)(int64_t)
                MICROS_SYSCALL_ABI_MEMORY_FAULT
        || fixture->vm_state.load.phase != MICROS_VM_LOAD_PHASE_IDLE
    ) {
        return false;
    }
    return true;
}

bool vm_handoff_syscall_test_phase_and_authority(
    struct vm_handoff_syscall_test_context *fixture
)
{
    struct micros_hart hart = {
        .hardware_id = 0,
    };
    struct micros_syscall_context foreign_context = {
        .objects = &fixture->objects,
        .current = fixture->foreign_thread,
        .process = fixture->foreign_process,
    };
    struct micros_syscall_arguments arguments =
        vm_handoff_syscall_test_load_begin_arguments();
    struct micros_trap_frame frame = {0};

    vm_handoff_syscall_test_reset(fixture);
    if (
        !vm_handoff_syscall_test_invoke_vm_command(fixture, &arguments, &frame)
        || frame.a0
            != (uint64_t)(int64_t)MICROS_SYSCALL_ABI_STATE
        || fixture->vm_state.load.phase != MICROS_VM_LOAD_PHASE_IDLE
    ) {
        return false;
    }

    vm_handoff_syscall_test_reset(fixture);
    fixture->bootstrap.phase = MICROS_BOOTSTRAP_PHASE_SEALED;
    frame = (struct micros_trap_frame){0};
    return (
        micros_vm_handoff_handle_captured_user_ecall(
            &hart,
            &frame,
            &foreign_context,
            &arguments
        ) == MICROS_SYSCALL_RETURN_NORMAL
        && frame.a0
            == (uint64_t)(int64_t)
                MICROS_SYSCALL_ABI_UNAUTHORIZED
        && fixture->vm_state.load.phase == MICROS_VM_LOAD_PHASE_IDLE
    );
}

bool vm_handoff_syscall_test_freeze_failure(
    struct vm_handoff_syscall_test_context *fixture
)
{
    struct micros_vm_load_freeze_plan *plan;
    struct micros_syscall_arguments arguments = {
        .a0 = MICROS_VM_LOAD_FREEZE,
        .a1 = MICROS_USER_VIRTUAL_BASE
            + MICROS_SV39_PAGE_SIZE - 40,
        .a2 = MICROS_VM_LOAD_FREEZE_PLAN_SIZE,
        .a3 = MICROS_USER_VIRTUAL_BASE
            + 2 * MICROS_SV39_PAGE_SIZE - 8,
        .a4 = MICROS_VM_LOAD_FREEZE_RESULT_SIZE,
        .a7 = MICROS_SYSCALL_ABI_VM_HANDOFF,
    };
    struct micros_trap_frame frame = {0};
    size_t output_offset = 2 * MICROS_SV39_PAGE_SIZE - 8;
    size_t index;

    vm_handoff_syscall_test_reset(fixture);
    if (!vm_handoff_syscall_test_begin_load(fixture, &frame) || !vm_handoff_syscall_test_fill_minimum_load(fixture)) {
        return false;
    }
    plan = vm_handoff_syscall_test_prepare_freeze_plan(fixture);
    plan->version = 0;
    ++plan->load_token;
    fixture->vm_operation = TEST_VM_OPERATION_FREEZE;
    fixture->vm_operation_stage = 0;
    frame = (struct micros_trap_frame){0};
    if (
        !vm_handoff_syscall_test_invoke_vm_command(fixture, &arguments, &frame)
        || frame.a0
            != (uint64_t)(int64_t)MICROS_SYSCALL_ABI_ARGUMENT
        || fixture->vm_state.load.phase != MICROS_VM_LOAD_PHASE_LOADING
        || fixture->child_process_record.address_space_root != 0
        || fixture->vm_operation_stage != 0
    ) {
        return false;
    }

    vm_handoff_syscall_test_reset(fixture);
    if (!vm_handoff_syscall_test_begin_load(fixture, &frame) || !vm_handoff_syscall_test_fill_minimum_load(fixture)) {
        return false;
    }
    plan = vm_handoff_syscall_test_prepare_freeze_plan(fixture);
    plan->page_plan_digest ^= 1;
    fixture->vm_operation = TEST_VM_OPERATION_FREEZE;
    fixture->vm_operation_stage = 0;
    frame = (struct micros_trap_frame){0};
    if (
        !vm_handoff_syscall_test_invoke_vm_command(fixture, &arguments, &frame)
        || frame.a0
            != (uint64_t)(int64_t)MICROS_SYSCALL_ABI_ARGUMENT
        || fixture->vm_state.load.phase != MICROS_VM_LOAD_PHASE_LOADING
        || fixture->child_process_record.address_space_root != 0
        || fixture->vm_operation_stage != 0
    ) {
        return false;
    }
    for (index = 0; index < MICROS_VM_LOAD_FREEZE_RESULT_SIZE; ++index) {
        if (fixture->user_memory[output_offset + index] != UINT8_C(0xa5)) {
            return false;
        }
    }
    return true;
}

bool vm_handoff_syscall_test_invariants_are_fatal(
    struct vm_handoff_syscall_test_context *fixture
)
{
    struct micros_syscall_arguments arguments;
    struct micros_trap_frame frame = {0};

    vm_handoff_syscall_test_reset(fixture);
    fixture->bootstrap.phase = MICROS_BOOTSTRAP_PHASE_SEALED;
    fixture->vm_state.last_load_token = UINT64_MAX;
    arguments = vm_handoff_syscall_test_load_begin_arguments();
    if (
        !vm_handoff_syscall_test_invoke_vm_command_panics(fixture, &arguments, &frame)
        || fixture->failure_reason != 0
        || fixture->vm_state.load.phase != MICROS_VM_LOAD_PHASE_IDLE
    ) {
        return false;
    }

    vm_handoff_syscall_test_reset(fixture);
    if (!vm_handoff_syscall_test_begin_load(fixture, &frame)) {
        return false;
    }
    arguments = vm_handoff_syscall_test_scratch_map_arguments(
        fixture->vm_state.load.load_token,
        0
    );
    fixture->vm_operation = TEST_VM_OPERATION_SCRATCH_MAP;
    fixture->claim_preflight_error =
        MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER;
    frame = (struct micros_trap_frame){0};
    if (
        !vm_handoff_syscall_test_invoke_vm_command_panics(fixture, &arguments, &frame)
        || fixture->failure_reason != 0
        || fixture->vm_state.load.scratch_phase
            != MICROS_VM_SCRATCH_CLEAR
        || fixture->vm_operation_stage != 0
    ) {
        return false;
    }

    vm_handoff_syscall_test_reset(fixture);
    if (!vm_handoff_syscall_test_begin_load(fixture, &frame)) {
        return false;
    }
    arguments = vm_handoff_syscall_test_scratch_map_arguments(
        fixture->vm_state.load.load_token,
        0
    );
    fixture->vm_operation = TEST_VM_OPERATION_SCRATCH_MAP;
    fixture->scratch_mapping_error =
        MICROS_USER_ADDRESS_SPACE_ERROR_CONFLICT;
    frame = (struct micros_trap_frame){0};
    return (
        vm_handoff_syscall_test_invoke_vm_command_panics(fixture, &arguments, &frame)
        && fixture->failure_reason == 0
        && fixture->vm_state.load.scratch_phase
            == MICROS_VM_SCRATCH_CLEAR
        && fixture->vm_operation_stage == 1
    );
}

bool vm_handoff_syscall_test_lifecycle_commit_and_exit(
    struct vm_handoff_syscall_test_context *fixture
)
{
    struct micros_trap_frame frame = {0};
    struct micros_syscall_arguments commit;
    struct micros_syscall_arguments release;
    uint64_t load_token;
    uint64_t mapping_generation;

    vm_handoff_syscall_test_reset(fixture);
    if (
        !vm_handoff_syscall_test_begin_load(fixture, &frame)
        || !vm_handoff_syscall_test_fill_minimum_load(fixture)
        || !vm_handoff_syscall_test_freeze_load(fixture, true, &mapping_generation)
    ) {
        return false;
    }
    load_token = fixture->vm_state.load.load_token;
    if (
        micros_vm_load_mark_prepared(
            &fixture->vm_state,
            fixture->child_process,
            fixture->pm_state.active_transaction,
            load_token,
            mapping_generation
        ) != MICROS_VM_HANDOFF_OK
        || micros_vm_load_mark_activated(
            &fixture->vm_state,
            fixture->child_process,
            fixture->pm_state.active_transaction,
            load_token,
            mapping_generation
        ) != MICROS_VM_HANDOFF_OK
    ) {
        return false;
    }
    fixture->pm_state.active_transaction = 0;
    fixture->pm_state.reserved_process =
        (struct micros_process_handle){0};
    commit = (struct micros_syscall_arguments){
        .a0 = MICROS_VM_LOAD_COMMIT,
        .a1 = load_token,
        .a2 = mapping_generation,
        .a7 = MICROS_SYSCALL_ABI_VM_HANDOFF,
    };
    frame = (struct micros_trap_frame){0};
    if (
        !vm_handoff_syscall_test_invoke_vm_command(fixture, &commit, &frame)
        || frame.a0 != MICROS_SYSCALL_ABI_OK
        || fixture->vm_state.load.phase != MICROS_VM_LOAD_PHASE_IDLE
        || fixture->vm_state.mappings[fixture->child_process.slot].phase
            != MICROS_VM_KERNEL_MAPPING_ACTIVE
    ) {
        return false;
    }
    if (
        micros_vm_load_mark_exit_detached(
            &fixture->vm_state,
            fixture->child_process,
            UINT64_C(0x77),
            load_token,
            mapping_generation
        ) != MICROS_VM_HANDOFF_OK
    ) {
        return false;
    }
    release = (struct micros_syscall_arguments){
        .a0 = MICROS_VM_EXIT_RELEASE,
        .a1 = UINT64_C(0x77),
        .a2 = load_token,
        .a3 = mapping_generation,
        .a7 = MICROS_SYSCALL_ABI_VM_HANDOFF,
    };
    fixture->vm_operation = TEST_VM_OPERATION_EXIT_RELEASE;
    fixture->vm_operation_stage = 0;
    frame = (struct micros_trap_frame){0};
    return (
        vm_handoff_syscall_test_invoke_vm_command(fixture, &release, &frame)
        && frame.a0 == MICROS_SYSCALL_ABI_OK
        && fixture->vm_operation_stage == 4
        && !fixture->vm_order_violation
        && fixture->child_process_record.address_space_root == 0
        && fixture->vm_state.mappings[fixture->child_process.slot].phase
            == MICROS_VM_KERNEL_MAPPING_UNUSED
    );
}

bool vm_handoff_syscall_test_abort_loading_scratch(
    struct vm_handoff_syscall_test_context *fixture
)
{
    struct micros_trap_frame frame = {0};
    struct micros_syscall_arguments map;
    struct micros_syscall_arguments abort_arguments;

    vm_handoff_syscall_test_reset(fixture);
    if (!vm_handoff_syscall_test_begin_load(fixture, &frame)) {
        return false;
    }
    map = vm_handoff_syscall_test_scratch_map_arguments(fixture->vm_state.load.load_token, 0);
    fixture->vm_operation = TEST_VM_OPERATION_SCRATCH_MAP;
    fixture->vm_operation_stage = 0;
    frame = (struct micros_trap_frame){0};
    if (
        !vm_handoff_syscall_test_invoke_vm_command(fixture, &map, &frame)
        || frame.a0 != MICROS_SYSCALL_ABI_OK
        || fixture->vm_state.load.scratch_phase
            != MICROS_VM_SCRATCH_MAPPED
    ) {
        return false;
    }
    abort_arguments = (struct micros_syscall_arguments){
        .a0 = MICROS_VM_LOAD_ABORT,
        .a1 = fixture->vm_state.load.load_token,
        .a7 = MICROS_SYSCALL_ABI_VM_HANDOFF,
    };
    fixture->vm_operation = TEST_VM_OPERATION_ABORT_LOADING;
    fixture->vm_operation_stage = 0;
    fixture->abort_has_scratch = true;
    frame = (struct micros_trap_frame){0};
    return (
        vm_handoff_syscall_test_invoke_vm_command(fixture, &abort_arguments, &frame)
        && frame.a0 == MICROS_SYSCALL_ABI_OK
        && fixture->vm_operation_stage == 4
        && !fixture->vm_order_violation
        && fixture->vm_state.load.phase == MICROS_VM_LOAD_PHASE_IDLE
        && fixture->vm_state.mappings[fixture->child_process.slot].phase
            == MICROS_VM_KERNEL_MAPPING_UNUSED
    );
}

bool vm_handoff_syscall_test_abort_loading_clear(
    struct vm_handoff_syscall_test_context *fixture
)
{
    struct micros_trap_frame frame = {0};
    struct micros_syscall_arguments arguments;

    vm_handoff_syscall_test_reset(fixture);
    if (!vm_handoff_syscall_test_begin_load(fixture, &frame)) {
        return false;
    }
    arguments = (struct micros_syscall_arguments){
        .a0 = MICROS_VM_LOAD_ABORT,
        .a1 = fixture->vm_state.load.load_token,
        .a7 = MICROS_SYSCALL_ABI_VM_HANDOFF,
    };
    fixture->vm_operation = TEST_VM_OPERATION_ABORT_LOADING;
    fixture->vm_operation_stage = 0;
    fixture->abort_has_scratch = false;
    frame = (struct micros_trap_frame){0};
    return (
        vm_handoff_syscall_test_invoke_vm_command(fixture, &arguments, &frame)
        && frame.a0 == MICROS_SYSCALL_ABI_OK
        && fixture->vm_operation_stage == 4
        && !fixture->vm_order_violation
        && fixture->vm_state.load.phase == MICROS_VM_LOAD_PHASE_IDLE
    );
}

bool vm_handoff_syscall_test_abort_frozen(
    struct vm_handoff_syscall_test_context *fixture
)
{
    struct micros_trap_frame frame = {0};
    struct micros_syscall_arguments arguments;
    uint64_t mapping_generation;

    vm_handoff_syscall_test_reset(fixture);
    if (
        !vm_handoff_syscall_test_begin_load(fixture, &frame)
        || !vm_handoff_syscall_test_fill_minimum_load(fixture)
        || !vm_handoff_syscall_test_freeze_load(fixture, false, &mapping_generation)
    ) {
        return false;
    }
    arguments = (struct micros_syscall_arguments){
        .a0 = MICROS_VM_LOAD_ABORT,
        .a1 = fixture->vm_state.load.load_token,
        .a2 = mapping_generation,
        .a7 = MICROS_SYSCALL_ABI_VM_HANDOFF,
    };
    fixture->vm_operation = TEST_VM_OPERATION_ABORT_FROZEN;
    fixture->vm_operation_stage = 0;
    frame = (struct micros_trap_frame){0};
    return (
        vm_handoff_syscall_test_invoke_vm_command(fixture, &arguments, &frame)
        && frame.a0 == MICROS_SYSCALL_ABI_OK
        && fixture->vm_operation_stage == 4
        && !fixture->vm_order_violation
        && fixture->child_process_record.address_space_root == 0
        && fixture->vm_state.load.phase == MICROS_VM_LOAD_PHASE_IDLE
    );
}

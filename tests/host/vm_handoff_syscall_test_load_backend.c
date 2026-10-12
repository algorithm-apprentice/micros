#include "vm_handoff_syscall_test_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

enum micros_user_address_space_error
micros_user_address_space_translate(
    struct micros_process_handle process,
    uint64_t user_address,
    uint64_t *physical_address,
    uint32_t *permissions,
    size_t *contiguous_bytes
)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    size_t offset;
    size_t page_offset;

    if (
        !vm_handoff_syscall_test_process_handles_equal(process, fixture->vm_process)
        || physical_address == NULL
        || permissions == NULL
        || contiguous_bytes == NULL
        || user_address < MICROS_USER_VIRTUAL_BASE
        || user_address
            >= MICROS_USER_VIRTUAL_BASE + sizeof(fixture->user_memory)
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_NOT_MAPPED;
    }
    offset = (size_t)(user_address - MICROS_USER_VIRTUAL_BASE);
    page_offset = offset % MICROS_SV39_PAGE_SIZE;
    *physical_address = (uint64_t)(uintptr_t)&fixture->user_memory[offset];
    *permissions = MICROS_SV39_PERMISSION_READ
        | MICROS_SV39_PERMISSION_WRITE
        | MICROS_SV39_PERMISSION_USER;
    *contiguous_bytes = MICROS_SV39_PAGE_SIZE - page_offset;
    return MICROS_USER_ADDRESS_SPACE_OK;
}

enum micros_frame_ownership_error
micros_frame_ownership_runtime_vm_claim_preflight(
    uint64_t physical_address,
    struct micros_frame_vm_claim_plan *plan
)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    if (physical_address == 0 || plan == NULL) {
        return MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT;
    }
    if (
        fixture->vm_operation != TEST_VM_OPERATION_SCRATCH_MAP
        || fixture->vm_operation_stage != 0
        || fixture->vm_state.load.scratch_phase != MICROS_VM_SCRATCH_CLEAR
    ) {
        fixture->vm_order_violation = true;
        return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
    }
    if (fixture->claim_preflight_error != MICROS_FRAME_OWNERSHIP_OK) {
        return fixture->claim_preflight_error;
    }
    plan->frame_index = physical_address / MICROS_SV39_PAGE_SIZE;
    fixture->vm_operation_stage = 1;
    return MICROS_FRAME_OWNERSHIP_OK;
}

void micros_frame_ownership_runtime_vm_claim_commit_prevalidated(
    const struct micros_frame_vm_claim_plan *plan
)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    if (
        plan == NULL
        || fixture->vm_operation != TEST_VM_OPERATION_SCRATCH_MAP
        || fixture->vm_operation_stage != 2
        || fixture->vm_state.load.scratch_phase != MICROS_VM_SCRATCH_CLEAR
        || fixture->vm_state.load.scratch.physical_address == 0
    ) {
        fixture->vm_order_violation = true;
        return;
    }
    fixture->vm_operation_stage = 3;
}

enum micros_frame_ownership_error
micros_frame_ownership_runtime_vm_release_transferable_preflight(
    const uint64_t *physical_addresses,
    size_t physical_address_count,
    struct micros_frame_vm_transferable_release_plan *plan
)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    if (
        plan == NULL
        || (
            physical_address_count != 0
            && physical_addresses == NULL
        )
    ) {
        return MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT;
    }
    if (
        fixture->vm_operation == TEST_VM_OPERATION_ABORT_LOADING
        && (
            fixture->vm_operation_stage == 0
            || fixture->vm_operation_stage == 1
        )
    ) {
        fixture->vm_operation_stage = 2;
    } else if (fixture->vm_operation == TEST_VM_OPERATION_ABORT_LOADING) {
        fixture->vm_order_violation = true;
        return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
    }
    plan->frame_count = (uint32_t)physical_address_count;
    return MICROS_FRAME_OWNERSHIP_OK;
}

void
micros_frame_ownership_runtime_vm_release_transferable_commit_prevalidated(
    const struct micros_frame_vm_transferable_release_plan *plan
)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    if (
        plan == NULL
        || fixture->vm_operation != TEST_VM_OPERATION_ABORT_LOADING
        || fixture->vm_operation_stage != (fixture->abort_has_scratch ? 3 : 2)
    ) {
        fixture->vm_order_violation = true;
        return;
    }
    fixture->vm_operation_stage = 4;
}

enum micros_frame_ownership_error
micros_frame_ownership_runtime_vm_freeze_preflight(
    struct micros_process_handle process,
    const uint64_t *data_physical_addresses,
    size_t data_physical_address_count,
    const uint64_t
        table_physical_addresses[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT],
    struct micros_frame_vm_freeze_plan *plan
)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    if (
        !vm_handoff_syscall_test_process_handles_equal(process, fixture->child_process)
        || data_physical_addresses == NULL
        || data_physical_address_count == 0
        || table_physical_addresses == NULL
        || plan == NULL
    ) {
        return MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT;
    }
    if (
        fixture->vm_operation != TEST_VM_OPERATION_FREEZE
        || fixture->vm_operation_stage != 0
    ) {
        fixture->vm_order_violation = true;
        return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
    }
    plan->process = process;
    plan->data_frame_count = (uint32_t)data_physical_address_count;
    fixture->vm_operation_stage = 1;
    if (fixture->mutate_freeze_input) {
        struct micros_vm_load_freeze_plan *input =
            (struct micros_vm_load_freeze_plan *)
                &fixture->user_memory[MICROS_SV39_PAGE_SIZE - 40];

        input->load_token ^= UINT64_C(0x55);
    }
    return MICROS_FRAME_OWNERSHIP_OK;
}

void micros_frame_ownership_runtime_vm_freeze_commit_prevalidated(
    const struct micros_frame_vm_freeze_plan *plan
)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    if (
        plan == NULL
        || fixture->vm_operation != TEST_VM_OPERATION_FREEZE
        || fixture->vm_operation_stage != 3
        || fixture->vm_state.load.phase != MICROS_VM_LOAD_PHASE_LOADING
        || fixture->child_process_record.address_space_root != 0
    ) {
        fixture->vm_order_violation = true;
        return;
    }
    fixture->vm_operation_stage = 4;
}

enum micros_frame_ownership_error
micros_frame_ownership_runtime_vm_release_process_preflight(
    struct micros_process_handle process,
    const uint64_t *data_physical_addresses,
    size_t data_physical_address_count,
    const uint64_t
        table_physical_addresses[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT],
    struct micros_frame_vm_process_release_plan *plan
)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    if (
        !vm_handoff_syscall_test_process_handles_equal(process, fixture->child_process)
        || data_physical_addresses == NULL
        || data_physical_address_count == 0
        || table_physical_addresses == NULL
        || plan == NULL
    ) {
        return MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT;
    }
    if (
        (
            fixture->vm_operation != TEST_VM_OPERATION_ABORT_FROZEN
            && fixture->vm_operation != TEST_VM_OPERATION_EXIT_RELEASE
        )
        || fixture->vm_operation_stage != 1
    ) {
        fixture->vm_order_violation = true;
        return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
    }
    plan->process = process;
    plan->data_frame_count = (uint32_t)data_physical_address_count;
    fixture->vm_operation_stage = 2;
    return MICROS_FRAME_OWNERSHIP_OK;
}

void
micros_frame_ownership_runtime_vm_release_process_commit_prevalidated(
    const struct micros_frame_vm_process_release_plan *plan
)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    if (
        plan == NULL
        || (
            fixture->vm_operation != TEST_VM_OPERATION_ABORT_FROZEN
            && fixture->vm_operation != TEST_VM_OPERATION_EXIT_RELEASE
        )
        || fixture->vm_operation_stage != 3
        || fixture->child_process_record.address_space_root != 0
    ) {
        fixture->vm_order_violation = true;
        return;
    }
    fixture->vm_operation_stage = 4;
}

enum micros_user_address_space_error
micros_user_address_space_prepare_vm_scratch_mapping(
    struct micros_process_handle process,
    uint64_t expected_root_physical_address,
    uint64_t physical_address,
    struct micros_user_address_space_vm_scratch_plan *plan
)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    if (
        !vm_handoff_syscall_test_process_handles_equal(process, fixture->vm_process)
        || expected_root_physical_address
            != fixture->vm_state.root_physical_address
        || physical_address == 0
        || plan == NULL
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    if (
        fixture->vm_operation != TEST_VM_OPERATION_SCRATCH_MAP
        || fixture->vm_operation_stage != 1
    ) {
        fixture->vm_order_violation = true;
        return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    if (fixture->scratch_mapping_error != MICROS_USER_ADDRESS_SPACE_OK) {
        return fixture->scratch_mapping_error;
    }
    plan->active = true;
    plan->process = process;
    plan->physical_address = physical_address;
    fixture->vm_operation_stage = 2;
    return MICROS_USER_ADDRESS_SPACE_OK;
}

void micros_user_address_space_commit_vm_scratch_mapping(
    struct micros_user_address_space_vm_scratch_plan *plan
)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    if (
        plan == NULL
        || fixture->vm_operation != TEST_VM_OPERATION_SCRATCH_MAP
        || fixture->vm_operation_stage != 3
        || fixture->vm_state.load.scratch_phase != MICROS_VM_SCRATCH_CLEAR
        || fixture->vm_state.load.scratch.physical_address
            != plan->physical_address
    ) {
        fixture->vm_order_violation = true;
        return;
    }
    plan->active = false;
    fixture->vm_operation_stage = 4;
}

enum micros_user_address_space_error
micros_user_address_space_prepare_vm_scratch_removal(
    struct micros_process_handle process,
    uint64_t expected_root_physical_address,
    uint64_t physical_address,
    struct micros_user_address_space_vm_scratch_plan *plan
)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    if (
        !vm_handoff_syscall_test_process_handles_equal(process, fixture->vm_process)
        || expected_root_physical_address
            != fixture->vm_state.root_physical_address
        || physical_address == 0
        || plan == NULL
        || (
            fixture->vm_operation != TEST_VM_OPERATION_SCRATCH_COMMIT
            && fixture->vm_operation != TEST_VM_OPERATION_ABORT_LOADING
        )
        || fixture->vm_operation_stage != 0
        || fixture->vm_state.load.scratch_phase
            != MICROS_VM_SCRATCH_MAPPED
    ) {
        fixture->vm_order_violation = true;
        return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    plan->active = true;
    plan->process = process;
    plan->physical_address = physical_address;
    fixture->vm_operation_stage = 1;
    return MICROS_USER_ADDRESS_SPACE_OK;
}

void micros_user_address_space_commit_vm_scratch_removal(
    struct micros_user_address_space_vm_scratch_plan *plan
)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    if (
        plan == NULL
        || !plan->active
        || (
            fixture->vm_operation == TEST_VM_OPERATION_SCRATCH_COMMIT
                ? fixture->vm_operation_stage != 1
                : fixture->vm_operation
                        != TEST_VM_OPERATION_ABORT_LOADING
                    || fixture->vm_operation_stage != 2
        )
        || fixture->vm_state.load.scratch_phase
            != MICROS_VM_SCRATCH_MAPPED
    ) {
        fixture->vm_order_violation = true;
        return;
    }
    plan->active = false;
    fixture->vm_operation_stage = fixture->vm_operation
            == TEST_VM_OPERATION_SCRATCH_COMMIT
        ? 2
        : 3;
}

enum micros_user_address_space_error
micros_user_address_space_prepare_vm_freeze(
    const struct micros_vm_load_freeze_action *action,
    struct micros_user_address_space_vm_freeze_plan *plan
)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    if (
        action == NULL
        || action->entry_count == 0
        || plan == NULL
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    if (
        fixture->vm_operation != TEST_VM_OPERATION_FREEZE
        || fixture->vm_operation_stage != 1
    ) {
        fixture->vm_order_violation = true;
        return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    plan->phase = MICROS_USER_ADDRESS_SPACE_VM_FREEZE_PLAN_PREPARED;
    plan->tables.process = action->process;
    plan->tables.entry_count = action->entry_count;
    for (
        size_t index = 0;
        index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT;
        ++index
    ) {
        plan->tables.table_frames[index] =
            action->table_frames[index];
    }
    fixture->vm_operation_stage = 2;
    return MICROS_USER_ADDRESS_SPACE_OK;
}

void micros_user_address_space_construct_vm_freeze_prevalidated(
    struct micros_user_address_space_vm_freeze_plan *plan
)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    if (
        plan == NULL
        || fixture->vm_operation != TEST_VM_OPERATION_FREEZE
        || fixture->vm_operation_stage != 2
    ) {
        fixture->vm_order_violation = true;
        return;
    }
    plan->phase = MICROS_USER_ADDRESS_SPACE_VM_FREEZE_PLAN_CONSTRUCTED;
    fixture->vm_operation_stage = 3;
}

void micros_user_address_space_install_vm_freeze_prevalidated(
    struct micros_user_address_space_vm_freeze_plan *plan
)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    if (
        plan == NULL
        || fixture->vm_operation != TEST_VM_OPERATION_FREEZE
        || fixture->vm_operation_stage != 4
        || plan->phase
            != MICROS_USER_ADDRESS_SPACE_VM_FREEZE_PLAN_CONSTRUCTED
    ) {
        fixture->vm_order_violation = true;
        return;
    }
    fixture->child_process_record.address_space_root =
        plan->tables.table_frames[0];
    plan->phase = MICROS_USER_ADDRESS_SPACE_VM_FREEZE_PLAN_UNUSED;
    fixture->vm_operation_stage = 5;
}

enum micros_user_address_space_error
micros_user_address_space_prepare_vm_teardown(
    struct micros_process_handle process,
    uint64_t root_physical_address,
    const uint64_t
        table_frames[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT],
    const struct micros_vm_kernel_mapping_entry *entries,
    size_t entry_count,
    struct micros_user_address_space_vm_teardown_plan *plan
)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    if (
        !vm_handoff_syscall_test_process_handles_equal(process, fixture->child_process)
        || root_physical_address == 0
        || table_frames == NULL
        || entries == NULL
        || entry_count == 0
        || plan == NULL
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    if (
        (
            fixture->vm_operation != TEST_VM_OPERATION_ABORT_FROZEN
            && fixture->vm_operation != TEST_VM_OPERATION_EXIT_RELEASE
        )
        || fixture->vm_operation_stage != 0
    ) {
        fixture->vm_order_violation = true;
        return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    plan->active = true;
    plan->process = process;
    plan->root_physical_address = root_physical_address;
    plan->entry_count = (uint32_t)entry_count;
    fixture->vm_operation_stage = 1;
    return MICROS_USER_ADDRESS_SPACE_OK;
}

void micros_user_address_space_commit_vm_teardown_prevalidated(
    struct micros_user_address_space_vm_teardown_plan *plan
)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    if (
        plan == NULL
        || !plan->active
        || (
            fixture->vm_operation != TEST_VM_OPERATION_ABORT_FROZEN
            && fixture->vm_operation != TEST_VM_OPERATION_EXIT_RELEASE
        )
        || fixture->vm_operation_stage != 2
    ) {
        fixture->vm_order_violation = true;
        return;
    }
    fixture->child_process_record.address_space_root = 0;
    plan->active = false;
    fixture->vm_operation_stage = 3;
}

#include "kernel/pm/pm_control_runtime_core.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static bool bytes_are_zero(const void *storage, size_t size)
{
    const unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static bool process_handles_equal(
    struct micros_process_handle left,
    struct micros_process_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

static bool process_is_empty(const struct micros_process *process)
{
    return (
        process->live_thread_count == 0
        && process->address_space_root == 0
        && process->primary_endpoint == MICROS_PROCESS_ENDPOINT_NONE
        && process->privilege_profile == 0
        && !process->endpoint_lifecycle_consumed
    );
}

static bool frame_has_process_owner(
    const struct micros_frame_ownership *ownership,
    uint64_t physical_address,
    enum micros_frame_owner_kind kind,
    struct micros_process_handle process
)
{
    struct micros_frame_owner owner;

    return (
        micros_frame_ownership_lookup(
            ownership,
            physical_address,
            &owner
        ) == MICROS_FRAME_OWNERSHIP_OK
        && owner.kind == kind
        && owner.slot == process.slot
        && owner.generation == process.generation
        && owner.reserved == 0
    );
}

static bool frozen_vm_reservation_is_valid(
    const struct micros_pm_control_state *state,
    const struct micros_process *process,
    const struct micros_frame_ownership *ownership,
    const struct micros_vm_handoff_state *vm
)
{
    const struct micros_vm_kernel_mapping_record *record;
    uint64_t frame_count = 0;
    size_t index;

    if (
        vm == NULL
        || micros_vm_handoff_state_validate(vm)
            != MICROS_VM_HANDOFF_OK
        || vm->phase != MICROS_VM_HANDOFF_PHASE_HANDED_OFF
        || vm->load.phase != MICROS_VM_LOAD_PHASE_FROZEN
        || vm->load.process_reservation_token
            != state->active_transaction
        || !process_handles_equal(
            vm->load.process,
            state->reserved_process
        )
        || state->reserved_process.slot >= MICROS_PROCESS_CAPACITY
    ) {
        return false;
    }
    record = &vm->mappings[state->reserved_process.slot];
    if (
        record->phase != MICROS_VM_KERNEL_MAPPING_FROZEN
        || !process_handles_equal(
            record->process,
            state->reserved_process
        )
        || record->root_physical_address == 0
        || process->live_thread_count != 0
        || process->address_space_root
            != record->root_physical_address
        || process->primary_endpoint != MICROS_PROCESS_ENDPOINT_NONE
        || process->privilege_profile != 0
        || process->endpoint_lifecycle_consumed
        || micros_frame_ownership_count_process(
            ownership,
            state->reserved_process,
            &frame_count
        ) != MICROS_FRAME_OWNERSHIP_OK
        || frame_count
            != record->entry_count
                + MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT
    ) {
        return false;
    }
    for (index = 0; index < record->entry_count; ++index) {
        if (
            !frame_has_process_owner(
                ownership,
                record->entries[index].physical_address,
                MICROS_FRAME_OWNER_VM_WIRED,
                state->reserved_process
            )
        ) {
            return false;
        }
    }
    for (
        index = 0;
        index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT;
        ++index
    ) {
        if (
            !frame_has_process_owner(
                ownership,
                record->table_frames[index],
                MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
                state->reserved_process
            )
        ) {
            return false;
        }
    }
    return true;
}

static bool prepared_vm_reservation_is_valid(
    const struct micros_pm_control_state *state,
    const struct micros_process *process,
    const struct micros_frame_ownership *ownership,
    const struct micros_vm_handoff_state *vm
)
{
    const struct micros_vm_kernel_mapping_record *record;
    uint64_t frame_count = 0;
    size_t index;

    if (
        state->prepared.token == 0
        || vm == NULL
        || micros_vm_handoff_state_validate(vm)
            != MICROS_VM_HANDOFF_OK
        || vm->phase != MICROS_VM_HANDOFF_PHASE_HANDED_OFF
        || vm->load.phase != MICROS_VM_LOAD_PHASE_FROZEN
        || vm->load.process_reservation_token
            != state->prepared.reservation_token
        || vm->load.load_token != state->prepared.load_token
        || !process_handles_equal(
            vm->load.process,
            state->prepared.process
        )
    ) {
        return false;
    }
    record = &vm->mappings[state->prepared.process.slot];
    if (
        record->phase != MICROS_VM_KERNEL_MAPPING_PREPARED
        || record->mapping_generation
            != state->prepared.mapping_generation
        || record->load_token != state->prepared.load_token
        || !process_handles_equal(
            record->process,
            state->prepared.process
        )
        || record->root_physical_address == 0
        || process->address_space_root
            != record->root_physical_address
        || micros_frame_ownership_count_process(
            ownership,
            state->prepared.process,
            &frame_count
        ) != MICROS_FRAME_OWNERSHIP_OK
        || frame_count
            != record->entry_count
                + MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT
    ) {
        return false;
    }
    for (index = 0; index < record->entry_count; ++index) {
        if (
            !frame_has_process_owner(
                ownership,
                record->entries[index].physical_address,
                MICROS_FRAME_OWNER_VM_WIRED,
                state->prepared.process
            )
        ) {
            return false;
        }
    }
    for (
        index = 0;
        index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT;
        ++index
    ) {
        if (
            !frame_has_process_owner(
                ownership,
                record->table_frames[index],
                MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
                state->prepared.process
            )
        ) {
            return false;
        }
    }
    return true;
}

static bool loading_vm_reservation_is_valid(
    const struct micros_pm_control_state *state,
    const struct micros_vm_handoff_state *vm
)
{
    const struct micros_vm_kernel_mapping_record *record;

    if (
        vm == NULL
        || micros_vm_handoff_state_validate(vm)
            != MICROS_VM_HANDOFF_OK
        || vm->phase != MICROS_VM_HANDOFF_PHASE_HANDED_OFF
        || vm->load.phase != MICROS_VM_LOAD_PHASE_LOADING
        || vm->load.process_reservation_token
            != state->active_transaction
        || !process_handles_equal(
            vm->load.process,
            state->reserved_process
        )
        || state->reserved_process.slot >= MICROS_PROCESS_CAPACITY
    ) {
        return false;
    }
    record = &vm->mappings[state->reserved_process.slot];
    return (
        record->phase == MICROS_VM_KERNEL_MAPPING_LOADING
        && record->load_token == vm->load.load_token
        && process_handles_equal(
            record->process,
            state->reserved_process
        )
    );
}

static bool activated_pending_vm_load_is_valid(
    const struct micros_pm_control_state *state,
    const struct micros_vm_handoff_state *vm
)
{
    const struct micros_vm_kernel_mapping_record *record;

    if (
        vm == NULL
        || micros_vm_handoff_state_validate(vm)
            != MICROS_VM_HANDOFF_OK
        || state->active_transaction != 0
        || vm->phase != MICROS_VM_HANDOFF_PHASE_HANDED_OFF
        || vm->load.phase != MICROS_VM_LOAD_PHASE_FROZEN
        || vm->load.process_reservation_token
            != state->last_transaction
        || vm->load.process.slot >= MICROS_PROCESS_CAPACITY
    ) {
        return false;
    }
    record = &vm->mappings[vm->load.process.slot];
    return (
        record->phase
            == MICROS_VM_KERNEL_MAPPING_ACTIVATED_PENDING_COMMIT
        && record->load_token == vm->load.load_token
        && process_handles_equal(
            record->process,
            vm->load.process
        )
    );
}

static bool exit_vm_state_is_valid(
    const struct micros_pm_control_state *state,
    const struct micros_process *process,
    const struct micros_frame_ownership *ownership,
    const struct micros_vm_handoff_state *vm
)
{
    const struct micros_vm_kernel_mapping_record *record;
    uint64_t frame_count = 0;
    size_t index;

    if (
        state->exit.phase == MICROS_PM_CONTROL_EXIT_NONE
        || vm == NULL
        || micros_vm_handoff_state_validate(vm)
            != MICROS_VM_HANDOFF_OK
        || vm->phase != MICROS_VM_HANDOFF_PHASE_HANDED_OFF
        || vm->load.phase != MICROS_VM_LOAD_PHASE_IDLE
    ) {
        return false;
    }
    record = &vm->mappings[state->exit.process.slot];
    if (
        state->exit.phase == MICROS_PM_CONTROL_EXIT_DETACHED
        && bytes_are_zero(record, sizeof(*record))
    ) {
        return (
            process->address_space_root == 0
            && micros_frame_ownership_count_process(
                ownership,
                state->exit.process,
                &frame_count
            ) == MICROS_FRAME_OWNERSHIP_OK
            && frame_count == 0
        );
    }
    if (
        record->phase
            != (
                state->exit.phase == MICROS_PM_CONTROL_EXIT_STOPPED
                ? MICROS_VM_KERNEL_MAPPING_ACTIVE
                : MICROS_VM_KERNEL_MAPPING_EXIT_DETACHED
            )
        || !process_handles_equal(
            record->process,
            state->exit.process
        )
        || record->load_token != state->exit.load_token
        || record->mapping_generation
            != state->exit.mapping_generation
        || (
            state->exit.phase == MICROS_PM_CONTROL_EXIT_STOPPED
            ? record->exit_token != 0
            : record->exit_token != state->exit.token
        )
        || process->address_space_root
            != record->root_physical_address
        || micros_frame_ownership_count_process(
            ownership,
            state->exit.process,
            &frame_count
        ) != MICROS_FRAME_OWNERSHIP_OK
        || frame_count
            != record->entry_count
                + MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT
    ) {
        return false;
    }
    for (index = 0; index < record->entry_count; ++index) {
        if (
            !frame_has_process_owner(
                ownership,
                record->entries[index].physical_address,
                MICROS_FRAME_OWNER_VM_WIRED,
                state->exit.process
            )
        ) {
            return false;
        }
    }
    for (
        index = 0;
        index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT;
        ++index
    ) {
        if (
            !frame_has_process_owner(
                ownership,
                record->table_frames[index],
                MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
                state->exit.process
            )
        ) {
            return false;
        }
    }
    return true;
}

static bool exit_vm_state_is_shallow_valid(
    const struct micros_pm_control_state *state,
    const struct micros_vm_handoff_state *vm
)
{
    const struct micros_vm_kernel_mapping_record *record;

    if (
        state->exit.phase == MICROS_PM_CONTROL_EXIT_NONE
        || vm == NULL
        || micros_vm_handoff_state_validate(vm)
            != MICROS_VM_HANDOFF_OK
        || vm->phase != MICROS_VM_HANDOFF_PHASE_HANDED_OFF
        || vm->load.phase != MICROS_VM_LOAD_PHASE_IDLE
    ) {
        return false;
    }
    record = &vm->mappings[state->exit.process.slot];
    if (
        state->exit.phase == MICROS_PM_CONTROL_EXIT_DETACHED
        && bytes_are_zero(record, sizeof(*record))
    ) {
        return true;
    }
    return (
        record->phase
            == (
                state->exit.phase == MICROS_PM_CONTROL_EXIT_STOPPED
                ? MICROS_VM_KERNEL_MAPPING_ACTIVE
                : MICROS_VM_KERNEL_MAPPING_EXIT_DETACHED
            )
        && record->load_token == state->exit.load_token
        && record->mapping_generation
            == state->exit.mapping_generation
        && process_handles_equal(
            record->process,
            state->exit.process
        )
        && (
            state->exit.phase == MICROS_PM_CONTROL_EXIT_STOPPED
            ? record->exit_token == 0
            : record->exit_token == state->exit.token
        )
    );
}

static bool active_vm_load_matches_reservation(
    const struct micros_pm_control_state *state,
    const struct micros_vm_handoff_state *vm
)
{
    const struct micros_vm_kernel_mapping_record *record;
    enum micros_vm_kernel_mapping_phase expected_phase;

    if (
        vm->phase != MICROS_VM_HANDOFF_PHASE_HANDED_OFF
        || (
            vm->load.phase != MICROS_VM_LOAD_PHASE_LOADING
            && vm->load.phase != MICROS_VM_LOAD_PHASE_FROZEN
        )
        || vm->load.process_reservation_token
            != state->active_transaction
        || !process_handles_equal(
            vm->load.process,
            state->reserved_process
        )
        || state->reserved_process.slot >= MICROS_PROCESS_CAPACITY
    ) {
        return false;
    }
    if (vm->load.phase == MICROS_VM_LOAD_PHASE_LOADING) {
        expected_phase = MICROS_VM_KERNEL_MAPPING_LOADING;
    } else if (state->prepared.token != 0) {
        expected_phase = MICROS_VM_KERNEL_MAPPING_PREPARED;
    } else {
        expected_phase = MICROS_VM_KERNEL_MAPPING_FROZEN;
    }
    record = &vm->mappings[state->reserved_process.slot];
    return (
        record->phase == expected_phase
        && record->load_token == vm->load.load_token
        && process_handles_equal(
            record->process,
            state->reserved_process
        )
    );
}

enum micros_pm_control_error
micros_pm_control_runtime_validate_reservation(
    const struct micros_pm_control_state *state,
    const struct micros_kernel_objects *objects,
    const struct micros_frame_ownership *ownership,
    const struct micros_vm_handoff_state *vm
)
{
    const struct micros_process *process;
    uint64_t frame_count = 0;

    if (
        state == NULL
        || objects == NULL
        || ownership == NULL
        || micros_pm_control_state_validate(state)
            != MICROS_PM_CONTROL_OK
        || micros_kernel_objects_validate(objects)
            != MICROS_KERNEL_OBJECT_OK
        || micros_frame_ownership_validate(ownership)
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        return MICROS_PM_CONTROL_ERROR_INVARIANT;
    }
    if (state->active_transaction == 0) {
        if (state->exit.phase != MICROS_PM_CONTROL_EXIT_NONE) {
            if (
                micros_process_resolve(
                    objects,
                    state->exit.process,
                    &process
                ) != MICROS_KERNEL_OBJECT_OK
                || micros_pm_control_state_validate_objects(
                    state,
                    objects
                ) != MICROS_PM_CONTROL_OK
            ) {
                return MICROS_PM_CONTROL_ERROR_INVARIANT;
            }
            return exit_vm_state_is_valid(
                state,
                process,
                ownership,
                vm
            ) ? MICROS_PM_CONTROL_OK
              : MICROS_PM_CONTROL_ERROR_INVARIANT;
        }
        if (
            vm == NULL
            || vm->load.phase == MICROS_VM_LOAD_PHASE_IDLE
        ) {
            return MICROS_PM_CONTROL_OK;
        }
        return activated_pending_vm_load_is_valid(state, vm)
            ? MICROS_PM_CONTROL_OK
            : MICROS_PM_CONTROL_ERROR_INVARIANT;
    }
    if (
        micros_process_resolve(
            objects,
            state->reserved_process,
            &process
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return MICROS_PM_CONTROL_ERROR_INVARIANT;
    }
    if (state->prepared.token != 0) {
        return (
            micros_pm_control_state_validate_objects(state, objects)
                == MICROS_PM_CONTROL_OK
            && prepared_vm_reservation_is_valid(
                state,
                process,
                ownership,
                vm
            )
        ) ? MICROS_PM_CONTROL_OK
          : MICROS_PM_CONTROL_ERROR_INVARIANT;
    }
    if (!process_is_empty(process)) {
        return frozen_vm_reservation_is_valid(
            state,
            process,
            ownership,
            vm
        ) ? MICROS_PM_CONTROL_OK
          : MICROS_PM_CONTROL_ERROR_INVARIANT;
    }
    if (
        micros_frame_ownership_count_process(
            ownership,
            state->reserved_process,
            &frame_count
        ) == MICROS_FRAME_OWNERSHIP_OK
        && frame_count == 0
    ) {
        if (
            vm != NULL
            && vm->load.phase != MICROS_VM_LOAD_PHASE_IDLE
        ) {
            return loading_vm_reservation_is_valid(state, vm)
                ? MICROS_PM_CONTROL_OK
                : MICROS_PM_CONTROL_ERROR_INVARIANT;
        }
        return MICROS_PM_CONTROL_OK;
    }
    return MICROS_PM_CONTROL_ERROR_INVARIANT;
}

enum micros_pm_control_error
micros_pm_control_runtime_abort_preflight(
    const struct micros_pm_control_state *state,
    const struct micros_vm_handoff_state *vm
)
{
    if (state == NULL || vm == NULL) {
        return MICROS_PM_CONTROL_ERROR_ARGUMENT;
    }
    if (
        micros_pm_control_state_validate(state)
            != MICROS_PM_CONTROL_OK
        || micros_vm_handoff_state_validate(vm)
            != MICROS_VM_HANDOFF_OK
    ) {
        return MICROS_PM_CONTROL_ERROR_INVARIANT;
    }
    if (vm->load.phase == MICROS_VM_LOAD_PHASE_IDLE) {
        if (state->exit.phase != MICROS_PM_CONTROL_EXIT_NONE) {
            return exit_vm_state_is_shallow_valid(state, vm)
                ? MICROS_PM_CONTROL_ERROR_STATE
                : MICROS_PM_CONTROL_ERROR_INVARIANT;
        }
        return MICROS_PM_CONTROL_OK;
    }
    if (state->active_transaction == 0) {
        return activated_pending_vm_load_is_valid(state, vm)
            ? MICROS_PM_CONTROL_ERROR_STATE
            : MICROS_PM_CONTROL_ERROR_INVARIANT;
    }
    return active_vm_load_matches_reservation(state, vm)
        ? MICROS_PM_CONTROL_ERROR_STATE
        : MICROS_PM_CONTROL_ERROR_INVARIANT;
}

enum micros_pm_control_error
micros_pm_control_runtime_reserve_preflight(
    const struct micros_pm_control_state *state,
    const struct micros_vm_handoff_state *vm
)
{
    if (state == NULL || vm == NULL) {
        return MICROS_PM_CONTROL_ERROR_ARGUMENT;
    }
    if (
        micros_pm_control_state_validate(state)
            != MICROS_PM_CONTROL_OK
        || micros_vm_handoff_state_validate(vm)
            != MICROS_VM_HANDOFF_OK
    ) {
        return MICROS_PM_CONTROL_ERROR_INVARIANT;
    }
    if (state->exit.phase != MICROS_PM_CONTROL_EXIT_NONE) {
        return exit_vm_state_is_shallow_valid(state, vm)
            ? MICROS_PM_CONTROL_ERROR_STATE
            : MICROS_PM_CONTROL_ERROR_INVARIANT;
    }
    if (state->active_transaction != 0) {
        if (vm->load.phase == MICROS_VM_LOAD_PHASE_IDLE) {
            return MICROS_PM_CONTROL_ERROR_STATE;
        }
        return active_vm_load_matches_reservation(state, vm)
            ? MICROS_PM_CONTROL_ERROR_STATE
            : MICROS_PM_CONTROL_ERROR_INVARIANT;
    }
    if (vm->load.phase == MICROS_VM_LOAD_PHASE_IDLE) {
        return MICROS_PM_CONTROL_OK;
    }
    return activated_pending_vm_load_is_valid(state, vm)
        ? MICROS_PM_CONTROL_ERROR_STATE
        : MICROS_PM_CONTROL_ERROR_INVARIANT;
}

#include "kernel/vm/vm_handoff_internal.h"
#include "lib/vm/vm_load_token.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint64_t next_load_token(uint64_t previous, uint16_t slot)
{
    uint64_t token = (previous & ~(uint64_t)(MICROS_VM_LOAD_TOKEN_SLOT_COUNT - 1)) | slot;

    if (token <= previous) {
        if (token > UINT64_MAX - MICROS_VM_LOAD_TOKEN_SLOT_COUNT) {
            return 0;
        }
        token += MICROS_VM_LOAD_TOKEN_SLOT_COUNT;
    }
    return token;
}

enum micros_vm_handoff_error micros_vm_load_begin_preflight(
    const struct micros_vm_handoff_state *state,
    uint64_t process_reservation_token,
    struct micros_process_handle process,
    bool process_is_empty,
    struct micros_vm_load_begin_plan *plan
)
{
    struct micros_vm_load_begin_plan candidate;
    enum micros_vm_handoff_error error;
    uint64_t load_token;

    if (
        plan == NULL
        || !micros_vm_handoff_pointer_is_aligned(
            plan,
            _Alignof(struct micros_vm_load_begin_plan)
        )
    ) {
        return MICROS_VM_HANDOFF_ERROR_ARGUMENT;
    }
    error = micros_vm_handoff_validate_selected(state);
    if (error != MICROS_VM_HANDOFF_OK) {
        return error;
    }
    if (
        state->phase != MICROS_VM_HANDOFF_PHASE_HANDED_OFF
        || state->load.phase != MICROS_VM_LOAD_PHASE_IDLE
        || process_reservation_token == 0
        || !micros_vm_handoff_process_is_valid(process)
        || micros_vm_handoff_process_handles_equal(process, state->process)
        || !process_is_empty
    ) {
        return MICROS_VM_HANDOFF_ERROR_STATE;
    }
    if (!micros_vm_handoff_selected_record_is_valid(state, process.slot)) {
        return MICROS_VM_HANDOFF_ERROR_INVARIANT;
    }
    if (!micros_vm_handoff_mapping_record_is_zero(&state->mappings[process.slot])) {
        return MICROS_VM_HANDOFF_ERROR_STATE;
    }
    load_token = next_load_token(state->last_load_token, process.slot);
    if (load_token == 0) {
        return MICROS_VM_HANDOFF_ERROR_CAPACITY;
    }

    micros_vm_handoff_clear_bytes(&candidate, sizeof(candidate));
    candidate.process_reservation_token = process_reservation_token;
    candidate.load_token = load_token;
    candidate.process = process;
    candidate.result.version = MICROS_VM_LOAD_BEGIN_RESULT_VERSION;
    candidate.result.size = MICROS_VM_LOAD_BEGIN_RESULT_SIZE;
    candidate.result.load_token = load_token;
    candidate.result.process_slot = process.slot;
    candidate.result.process_generation = process.generation;
    micros_vm_handoff_copy_bytes(plan, &candidate, sizeof(candidate));
    return MICROS_VM_HANDOFF_OK;
}

void micros_vm_load_begin_commit_prevalidated(
    struct micros_vm_handoff_state *state,
    const struct micros_vm_load_begin_plan *plan
)
{
    struct micros_vm_kernel_mapping_record *record =
        &state->mappings[plan->process.slot];

    state->last_load_token = plan->load_token;
    state->load.phase = MICROS_VM_LOAD_PHASE_LOADING;
    state->load.scratch_phase = MICROS_VM_SCRATCH_CLEAR;
    state->load.process_reservation_token = plan->process_reservation_token;
    state->load.load_token = plan->load_token;
    state->load.process = plan->process;

    record->load_token = plan->load_token;
    record->process = plan->process;
    record->phase = MICROS_VM_KERNEL_MAPPING_LOADING;
}

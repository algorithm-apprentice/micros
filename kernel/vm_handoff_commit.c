#include "kernel/vm_handoff_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static enum micros_vm_handoff_error load_record_transition(
    struct micros_vm_handoff_state *state,
    struct micros_process_handle process,
    uint64_t process_reservation_token,
    uint64_t load_token,
    uint64_t mapping_generation,
    enum micros_vm_kernel_mapping_phase expected_phase,
    enum micros_vm_kernel_mapping_phase next_phase
)
{
    struct micros_vm_kernel_mapping_record *record;
    enum micros_vm_handoff_error error =
        micros_vm_handoff_state_validate(state);

    if (error != MICROS_VM_HANDOFF_OK) {
        return error;
    }
    if (
        state->phase != MICROS_VM_HANDOFF_PHASE_HANDED_OFF
        || state->load.phase != MICROS_VM_LOAD_PHASE_FROZEN
        || !micros_vm_handoff_process_handles_equal(process, state->load.process)
        || process_reservation_token != state->load.process_reservation_token
        || load_token != state->load.load_token
    ) {
        return MICROS_VM_HANDOFF_ERROR_STATE;
    }
    record = &state->mappings[process.slot];
    if (
        record->phase != expected_phase
        || record->mapping_generation != mapping_generation
    ) {
        return MICROS_VM_HANDOFF_ERROR_STATE;
    }
    record->phase = next_phase;
    return MICROS_VM_HANDOFF_OK;
}

enum micros_vm_handoff_error micros_vm_load_mark_prepared(
    struct micros_vm_handoff_state *state,
    struct micros_process_handle process,
    uint64_t process_reservation_token,
    uint64_t load_token,
    uint64_t mapping_generation
)
{
    return load_record_transition(
        state,
        process,
        process_reservation_token,
        load_token,
        mapping_generation,
        MICROS_VM_KERNEL_MAPPING_FROZEN,
        MICROS_VM_KERNEL_MAPPING_PREPARED
    );
}

enum micros_vm_handoff_error micros_vm_load_mark_unprepared(
    struct micros_vm_handoff_state *state,
    struct micros_process_handle process,
    uint64_t process_reservation_token,
    uint64_t load_token,
    uint64_t mapping_generation
)
{
    return load_record_transition(
        state,
        process,
        process_reservation_token,
        load_token,
        mapping_generation,
        MICROS_VM_KERNEL_MAPPING_PREPARED,
        MICROS_VM_KERNEL_MAPPING_FROZEN
    );
}

enum micros_vm_handoff_error micros_vm_load_mark_activated(
    struct micros_vm_handoff_state *state,
    struct micros_process_handle process,
    uint64_t process_reservation_token,
    uint64_t load_token,
    uint64_t mapping_generation
)
{
    return load_record_transition(
        state,
        process,
        process_reservation_token,
        load_token,
        mapping_generation,
        MICROS_VM_KERNEL_MAPPING_PREPARED,
        MICROS_VM_KERNEL_MAPPING_ACTIVATED_PENDING_COMMIT
    );
}

enum micros_vm_handoff_error micros_vm_load_commit_preflight(
    const struct micros_vm_handoff_state *state,
    uint64_t load_token,
    uint64_t mapping_generation
)
{
    const struct micros_vm_kernel_mapping_record *record;
    enum micros_vm_handoff_error error =
        micros_vm_handoff_state_validate(state);

    if (error != MICROS_VM_HANDOFF_OK) {
        return error;
    }
    if (
        state->phase != MICROS_VM_HANDOFF_PHASE_HANDED_OFF
        || state->load.phase != MICROS_VM_LOAD_PHASE_FROZEN
        || load_token != state->load.load_token
    ) {
        return MICROS_VM_HANDOFF_ERROR_STATE;
    }
    record = &state->mappings[state->load.process.slot];
    return (
        record->phase == MICROS_VM_KERNEL_MAPPING_ACTIVATED_PENDING_COMMIT
        && record->mapping_generation == mapping_generation
    )
        ? MICROS_VM_HANDOFF_OK
        : MICROS_VM_HANDOFF_ERROR_STATE;
}

void micros_vm_load_commit_commit_prevalidated(
    struct micros_vm_handoff_state *state
)
{
    state->mappings[state->load.process.slot].phase =
        MICROS_VM_KERNEL_MAPPING_ACTIVE;
    micros_vm_handoff_clear_bytes(&state->load, sizeof(state->load));
}

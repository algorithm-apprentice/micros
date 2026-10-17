#include "kernel/vm/vm_handoff_internal.h"

#include <stddef.h>
#include <stdint.h>

bool micros_vm_tty_mapping_matches(
    const struct micros_vm_tty_mapping_request *request,
    micros_endpoint_t tty_endpoint
)
{
    return (
        request != NULL
        && micros_vm_handoff_pointer_is_aligned(
            request,
            _Alignof(struct micros_vm_tty_mapping_request)
        )
        && tty_endpoint != MICROS_ENDPOINT_NONE
        && tty_endpoint != MICROS_ENDPOINT_ANY
        && request->command == MICROS_VM_HANDOFF_MAP_TTY_UART
        && request->version == MICROS_TTY_MAPPING_VERSION
        && request->service_id == MICROS_TTY_SERVICE_ID
        && request->endpoint == tty_endpoint
        && request->virtual_base == MICROS_TTY_UART_VIRTUAL_BASE
        && request->physical_base == MICROS_TTY_UART_PHYSICAL_BASE
        && request->irq_source == MICROS_TTY_UART_IRQ_SOURCE
        && request->mapped_length == MICROS_TTY_UART_MAPPED_LENGTH
    );
}

enum micros_vm_handoff_error micros_vm_handoff_state_prepare(
    struct micros_vm_handoff_state *state,
    uint32_t service_id,
    uint32_t endpoint,
    uint32_t image_id,
    uint32_t profile_id,
    struct micros_process_handle process,
    struct micros_thread_handle thread,
    uint64_t root_physical_address,
    uint64_t boot_info_address,
    uint32_t boot_info_size,
    const struct micros_vm_boot_summary *summary
)
{
    if (
        state == NULL
        || summary == NULL
        || !micros_vm_handoff_pointer_is_aligned(
            state,
            _Alignof(struct micros_vm_handoff_state)
        )
        || !micros_vm_handoff_pointer_is_aligned(
            summary,
            _Alignof(struct micros_vm_boot_summary)
        )
    ) {
        return MICROS_VM_HANDOFF_ERROR_ARGUMENT;
    }
    if (!micros_vm_handoff_bytes_are_zero(state, sizeof(*state))) {
        return MICROS_VM_HANDOFF_ERROR_STORAGE;
    }
    if (
        service_id == 0
        || service_id > 63
        || !micros_vm_handoff_endpoint_matches_process(endpoint, process)
        || image_id == 0
        || profile_id == 0
        || profile_id >= MICROS_PRIVILEGE_PROFILE_CAPACITY
        || !micros_vm_handoff_thread_is_valid(thread)
        || root_physical_address == 0
        || root_physical_address % MICROS_FRAME_SIZE != 0
        || boot_info_address == 0
        || boot_info_address % MICROS_VM_BOOT_INFO_ALIGNMENT != 0
        || boot_info_size != MICROS_VM_BOOT_INFO_SIZE
        || !micros_vm_handoff_summary_is_valid(summary)
    ) {
        return MICROS_VM_HANDOFF_ERROR_IDENTITY;
    }
    state->initialization_magic = MICROS_VM_HANDOFF_MAGIC;
    state->phase = MICROS_VM_HANDOFF_PHASE_PREPARED;
    state->service_id = service_id;
    state->endpoint = endpoint;
    state->image_id = image_id;
    state->profile_id = profile_id;
    state->process.slot = process.slot;
    state->process.generation = process.generation;
    state->thread.slot = thread.slot;
    state->thread.generation = thread.generation;
    state->root_physical_address = root_physical_address;
    state->boot_info_address = boot_info_address;
    state->boot_info_size = boot_info_size;
    micros_vm_handoff_copy_bytes(&state->summary, summary, sizeof(state->summary));
    return MICROS_VM_HANDOFF_OK;
}

bool micros_vm_handoff_summary_matches(
    const struct micros_vm_handoff_state *state,
    const struct micros_vm_handoff_request *request
)
{
    return (
        state != NULL
        && request != NULL
        && micros_vm_handoff_pointer_is_aligned(
            state,
            _Alignof(struct micros_vm_handoff_state)
        )
        && micros_vm_handoff_pointer_is_aligned(
            request,
            _Alignof(struct micros_vm_handoff_request)
        )
        && micros_vm_handoff_state_validate(state)
            == MICROS_VM_HANDOFF_OK
        && request->command == MICROS_VM_HANDOFF_READY
        && request->version == state->summary.version
        && request->managed_range_count
            == state->summary.managed_range_count
        && request->address_space_count
            == state->summary.address_space_count
        && request->managed_frame_count
            == state->summary.managed_frame_count
        && request->free_frame_count
            == state->summary.free_frame_count
        && request->mapping_count == state->summary.mapping_count
        && request->digest == state->summary.digest
    );
}

void micros_vm_handoff_commit_prevalidated(
    struct micros_vm_handoff_state *state
)
{
    state->phase = MICROS_VM_HANDOFF_PHASE_HANDED_OFF;
}

enum micros_vm_self_fault_action micros_vm_self_fault_classify(
    enum micros_bootstrap_phase bootstrap_phase,
    enum micros_bootstrap_service_state service_state
)
{
    if (bootstrap_phase == MICROS_BOOTSTRAP_PHASE_RUNNING) {
        if (service_state == MICROS_BOOTSTRAP_SERVICE_STARTING) {
            return MICROS_VM_SELF_FAULT_RUNNING_STARTING;
        }
        if (service_state == MICROS_BOOTSTRAP_SERVICE_READY) {
            return MICROS_VM_SELF_FAULT_RUNNING_READY;
        }
        return MICROS_VM_SELF_FAULT_INVALID;
    }
    return bootstrap_phase == MICROS_BOOTSTRAP_PHASE_SEALED
        ? MICROS_VM_SELF_FAULT_SEALED
        : MICROS_VM_SELF_FAULT_INVALID;
}

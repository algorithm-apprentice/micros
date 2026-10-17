#include "kernel/pm_control_internal.h"

#include <stddef.h>
#include <stdint.h>

#include "kernel/endpoint/endpoint_internal.h"
#include "kernel/vm_handoff_core.h"
#include "micros/endpoint.h"
#include "micros/scheduler_core.h"
#include "micros/spawn.h"

bool micros_pm_control_bytes_are_zero(
    const void *storage,
    size_t size
)
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

void micros_pm_control_clear_bytes(void *storage, size_t size)
{
    unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

void micros_pm_control_copy_bytes(
    void *destination,
    const void *source,
    size_t size
)
{
    unsigned char *output = destination;
    const unsigned char *input = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        output[index] = input[index];
    }
}

bool micros_pm_control_pointer_is_aligned(
    const void *pointer,
    size_t alignment
)
{
    return (uintptr_t)pointer % alignment == 0;
}

bool micros_pm_control_process_handle_is_valid(
    struct micros_process_handle process
)
{
    return (
        process.slot < MICROS_PROCESS_CAPACITY
        && process.generation != 0
        && process.generation <= MICROS_PROCESS_GENERATION_MAX
    );
}

bool micros_pm_control_process_handle_is_zero(
    struct micros_process_handle process
)
{
    return process.slot == 0 && process.generation == 0;
}

bool micros_pm_control_process_handles_equal(
    struct micros_process_handle left,
    struct micros_process_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

bool micros_pm_control_hart_handles_equal(
    struct micros_hart_handle left,
    struct micros_hart_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

bool micros_pm_control_thread_handle_is_valid(
    struct micros_thread_handle thread
)
{
    return (
        thread.slot < MICROS_THREAD_CAPACITY
        && thread.generation != 0
    );
}

bool micros_pm_control_thread_handle_is_zero(
    struct micros_thread_handle thread
)
{
    return thread.slot == 0 && thread.generation == 0;
}

bool micros_pm_control_hart_handle_is_valid(
    struct micros_hart_handle hart
)
{
    return (
        hart.slot < MICROS_HART_CAPACITY
        && hart.generation != 0
    );
}

bool micros_pm_control_endpoint_matches_process(
    uint32_t endpoint,
    struct micros_process_handle process
)
{
    return (
        micros_pm_control_process_handle_is_valid(process)
        && endpoint
            == (
                (process.generation << MICROS_ENDPOINT_SLOT_BITS)
                | process.slot
            )
        && endpoint != MICROS_ENDPOINT_NONE
        && endpoint != MICROS_ENDPOINT_ANY
    );
}

bool micros_pm_control_process_is_empty(
    const struct micros_process *process
)
{
    return (
        process->live_thread_count == 0
        && process->address_space_root == 0
        && process->primary_endpoint == MICROS_PROCESS_ENDPOINT_NONE
        && process->privilege_profile == 0
        && !process->endpoint_lifecycle_consumed
    );
}

enum micros_pm_control_error micros_pm_control_state_prepare(
    struct micros_pm_control_state *state,
    uint32_t service_id,
    uint32_t endpoint,
    uint32_t profile_id,
    struct micros_process_handle process,
    struct micros_thread_handle thread
)
{
    struct micros_pm_control_state candidate;

    if (
        state == NULL
        || !micros_pm_control_pointer_is_aligned(
            state,
            _Alignof(struct micros_pm_control_state)
        )
    ) {
        return MICROS_PM_CONTROL_ERROR_ARGUMENT;
    }
    if (!micros_pm_control_bytes_are_zero(state, sizeof(*state))) {
        return MICROS_PM_CONTROL_ERROR_STORAGE;
    }
    if (
        service_id != 3
        || process.slot != 2
        || profile_id != MICROS_PRIVILEGE_PROFILE_PM
        || !micros_pm_control_endpoint_matches_process(endpoint, process)
        || !micros_pm_control_thread_handle_is_valid(thread)
    ) {
        return MICROS_PM_CONTROL_ERROR_ARGUMENT;
    }
    micros_pm_control_clear_bytes(&candidate, sizeof(candidate));
    candidate.initialization_magic = MICROS_PM_CONTROL_MAGIC;
    candidate.service_id = service_id;
    candidate.endpoint = endpoint;
    candidate.profile_id = profile_id;
    candidate.process.slot = process.slot;
    candidate.process.generation = process.generation;
    candidate.thread.slot = thread.slot;
    candidate.thread.generation = thread.generation;
    micros_pm_control_copy_bytes(state, &candidate, sizeof(candidate));
    return MICROS_PM_CONTROL_OK;
}

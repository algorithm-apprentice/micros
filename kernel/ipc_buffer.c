#include "micros/ipc_buffer.h"

#include <stddef.h>

#include "kernel/ipc_buffer_internal.h"
#include "micros/sv39.h"
#include "micros/user_address_space.h"

static enum micros_ipc_buffer_error map_address_error(
    enum micros_user_address_space_error error
)
{
    switch (error) {
    case MICROS_USER_ADDRESS_SPACE_OK:
        return MICROS_IPC_BUFFER_OK;
    case MICROS_USER_ADDRESS_SPACE_ERROR_RANGE:
    case MICROS_USER_ADDRESS_SPACE_ERROR_NOT_MAPPED:
    case MICROS_USER_ADDRESS_SPACE_ERROR_PERMISSION:
        return MICROS_IPC_BUFFER_ERROR_MESSAGE_FAULT;
    default:
        return MICROS_IPC_BUFFER_ERROR_INVARIANT;
    }
}

static enum micros_ipc_buffer_error plan_buffer(
    struct micros_process_handle process,
    uint64_t user_address,
    uint32_t access,
    struct micros_ipc_buffer_plan *plan
)
{
    uint64_t current;
    size_t remaining = sizeof(struct micros_ipc_message);

    if (
        plan == NULL
        || user_address == 0
        || user_address % 8 != 0
        || access == 0
        || (
            access
            & ~(MICROS_IPC_BUFFER_READ | MICROS_IPC_BUFFER_WRITE)
        ) != 0
    ) {
        return MICROS_IPC_BUFFER_ERROR_ARGUMENT;
    }
    if (
        UINT64_MAX - user_address
            < sizeof(struct micros_ipc_message) - 1
        || user_address < MICROS_USER_VIRTUAL_BASE
        || user_address + sizeof(struct micros_ipc_message)
            > MICROS_USER_VIRTUAL_END
    ) {
        return MICROS_IPC_BUFFER_ERROR_MESSAGE_FAULT;
    }
    current = user_address;
    plan->chunk_count = 0;
    while (remaining != 0) {
        uint64_t physical_address;
        uint32_t permissions;
        size_t contiguous;
        size_t chunk_size;
        enum micros_user_address_space_error address_error;

        if (plan->chunk_count >= 2) {
            return MICROS_IPC_BUFFER_ERROR_INVARIANT;
        }
        address_error = micros_user_address_space_translate(
            process,
            current,
            &physical_address,
            &permissions,
            &contiguous
        );
        if (address_error != MICROS_USER_ADDRESS_SPACE_OK) {
            return map_address_error(address_error);
        }
        if (
            (
                (access & MICROS_IPC_BUFFER_READ) != 0
                && (
                    permissions & MICROS_SV39_PERMISSION_READ
                ) == 0
            )
            || (
                (access & MICROS_IPC_BUFFER_WRITE) != 0
                && (
                    permissions & MICROS_SV39_PERMISSION_WRITE
                ) == 0
            )
        ) {
            return MICROS_IPC_BUFFER_ERROR_MESSAGE_FAULT;
        }
        chunk_size = contiguous < remaining ? contiguous : remaining;
        plan->chunks[plan->chunk_count].physical_address =
            physical_address;
        plan->chunks[plan->chunk_count].size = chunk_size;
        ++plan->chunk_count;
        current += chunk_size;
        remaining -= chunk_size;
    }
    return MICROS_IPC_BUFFER_OK;
}

static void copy_from_plan(
    void *destination,
    const struct micros_ipc_buffer_plan *plan
)
{
    unsigned char *output = destination;
    size_t chunk_index;
    size_t output_offset = 0;

    for (chunk_index = 0; chunk_index < plan->chunk_count; ++chunk_index) {
        const unsigned char *input =
            (const unsigned char *)(uintptr_t)
                plan->chunks[chunk_index].physical_address;
        size_t index;

        for (index = 0; index < plan->chunks[chunk_index].size; ++index) {
            output[output_offset + index] = input[index];
        }
        output_offset += plan->chunks[chunk_index].size;
    }
}

static void copy_to_plan(
    const struct micros_ipc_buffer_plan *plan,
    const void *source
)
{
    const unsigned char *input = source;
    size_t chunk_index;
    size_t input_offset = 0;

    for (chunk_index = 0; chunk_index < plan->chunk_count; ++chunk_index) {
        unsigned char *output =
            (unsigned char *)(uintptr_t)
                plan->chunks[chunk_index].physical_address;
        size_t index;

        for (index = 0; index < plan->chunks[chunk_index].size; ++index) {
            output[index] = input[input_offset + index];
        }
        input_offset += plan->chunks[chunk_index].size;
    }
}

enum micros_ipc_buffer_error micros_ipc_buffer_validate(
    struct micros_process_handle process,
    uint64_t user_address,
    uint32_t access
)
{
    struct micros_ipc_buffer_plan plan;

    return plan_buffer(process, user_address, access, &plan);
}

enum micros_ipc_buffer_error micros_ipc_buffer_snapshot(
    struct micros_process_handle process,
    uint64_t user_address,
    uint32_t access,
    struct micros_ipc_message *message
)
{
    struct micros_ipc_buffer_plan plan;
    struct micros_ipc_message candidate;
    enum micros_ipc_buffer_error error;

    if (
        message == NULL
        || (access & MICROS_IPC_BUFFER_READ) == 0
    ) {
        return MICROS_IPC_BUFFER_ERROR_ARGUMENT;
    }
    error = plan_buffer(process, user_address, access, &plan);
    if (error != MICROS_IPC_BUFFER_OK) {
        return error;
    }
    copy_from_plan(&candidate, &plan);
    *message = candidate;
    return MICROS_IPC_BUFFER_OK;
}

enum micros_ipc_buffer_error micros_ipc_buffer_write(
    struct micros_process_handle process,
    uint64_t user_address,
    const struct micros_ipc_message *message
)
{
    struct micros_ipc_buffer_plan plan;
    enum micros_ipc_buffer_error error;

    if (message == NULL) {
        return MICROS_IPC_BUFFER_ERROR_ARGUMENT;
    }
    error = micros_ipc_buffer_prepare_write(
        process,
        user_address,
        &plan
    );
    if (error != MICROS_IPC_BUFFER_OK) {
        return error;
    }
    micros_ipc_buffer_commit_write(&plan, message);
    return MICROS_IPC_BUFFER_OK;
}

enum micros_ipc_buffer_error micros_ipc_buffer_prepare_write(
    struct micros_process_handle process,
    uint64_t user_address,
    struct micros_ipc_buffer_plan *plan
)
{
    return plan_buffer(
        process,
        user_address,
        MICROS_IPC_BUFFER_WRITE,
        plan
    );
}

void micros_ipc_buffer_commit_write(
    const struct micros_ipc_buffer_plan *plan,
    const struct micros_ipc_message *message
)
{
    copy_to_plan(plan, message);
}

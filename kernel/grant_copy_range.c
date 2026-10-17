#include "grant_copy_internal.h"

#include "kernel/tty/tty_handoff_runtime.h"
#include "kernel/memory/user_address_space_internal.h"
#include "micros/sv39.h"
#include "micros/user_address_space.h"

static enum micros_grant_error grant_copy_map_address_error(
    enum micros_user_address_space_error error
)
{
    switch (error) {
    case MICROS_USER_ADDRESS_SPACE_OK:
        return MICROS_GRANT_OK;
    case MICROS_USER_ADDRESS_SPACE_ERROR_RANGE:
    case MICROS_USER_ADDRESS_SPACE_ERROR_NOT_MAPPED:
    case MICROS_USER_ADDRESS_SPACE_ERROR_PERMISSION:
        return MICROS_GRANT_ERROR_FAULT;
    case MICROS_USER_ADDRESS_SPACE_ERROR_PHASE:
        return MICROS_GRANT_ERROR_PHASE;
    default:
        return MICROS_GRANT_ERROR_INVARIANT;
    }
}

static enum micros_grant_error grant_copy_plan_device_span(
    struct micros_process_handle process,
    const struct micros_tty_device_authority *device_authority,
    uintptr_t *current,
    size_t *remaining,
    bool *device_fault,
    enum micros_user_address_space_error *address_error,
    size_t *chunk_length
)
{
    *address_error =
        micros_user_address_space_validate_tty_uart_mapping(
            process,
            device_authority->root_physical_address
        );
    if (
        *address_error
            == MICROS_USER_ADDRESS_SPACE_ERROR_PHASE
    ) {
        return MICROS_GRANT_ERROR_PHASE;
    }
    if (
        *address_error
            != MICROS_USER_ADDRESS_SPACE_OK
    ) {
        return MICROS_GRANT_ERROR_INVARIANT;
    }
    *chunk_length =
        (
            (uintptr_t)MICROS_TTY_UART_VIRTUAL_BASE
            + MICROS_TTY_UART_MAPPED_LENGTH
        ) - *current;
    if (*chunk_length > *remaining) {
        *chunk_length = *remaining;
    }
    *current += *chunk_length;
    *remaining -= *chunk_length;
    *device_fault = true;
    return MICROS_GRANT_OK;
}

enum micros_grant_error grant_copy_plan_range(
    struct micros_process_handle process,
    uintptr_t address,
    size_t length,
    uint32_t required_permission,
    const struct micros_tty_device_authority *device_authority,
    struct micros_grant_copy_range_plan *plan
)
{
    struct micros_grant_copy_range_plan candidate = {0};
    uintptr_t current = address;
    size_t remaining = length;
    bool device_fault = false;

    while (remaining != 0) {
        uint64_t physical_address;
        uint32_t permissions;
        size_t contiguous;
        size_t chunk_length;
        enum micros_user_address_space_error address_error;

        if (
            device_authority != NULL
            && micros_tty_device_range_intersects(
                device_authority,
                process,
                current,
                1
            )
        ) {
            switch (grant_copy_plan_device_span(
                process, device_authority, &current, &remaining,
                &device_fault, &address_error, &chunk_length
            )) {
            case MICROS_GRANT_OK:
                continue;
            case MICROS_GRANT_ERROR_PHASE:
                return MICROS_GRANT_ERROR_PHASE;
            case MICROS_GRANT_ERROR_INVARIANT:
                return MICROS_GRANT_ERROR_INVARIANT;
            default:
                return MICROS_GRANT_ERROR_INVARIANT;
            }
        }
        if (candidate.chunk_count >= 2) {
            return MICROS_GRANT_ERROR_INVARIANT;
        }
        address_error = micros_user_address_space_translate(
            process,
            current,
            &physical_address,
            &permissions,
            &contiguous
        );
        if (address_error != MICROS_USER_ADDRESS_SPACE_OK) {
            return grant_copy_map_address_error(address_error);
        }
        if ((permissions & required_permission) == 0) {
            return MICROS_GRANT_ERROR_FAULT;
        }
        chunk_length = contiguous < remaining
            ? contiguous
            : remaining;
        candidate.chunks[candidate.chunk_count] =
            (struct micros_grant_copy_chunk){
                .physical_address = physical_address,
                .length = chunk_length,
            };
        ++candidate.chunk_count;
        current += chunk_length;
        remaining -= chunk_length;
    }
    if (device_fault) {
        return MICROS_GRANT_ERROR_FAULT;
    }
    *plan = candidate;
    return MICROS_GRANT_OK;
}

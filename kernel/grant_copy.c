#include "micros/grant_copy.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "tty_handoff_runtime.h"
#include "user_address_space_internal.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/sv39.h"
#include "micros/user_address_space.h"

_Static_assert(
    MICROS_GRANT_COPY_MAX == MICROS_SV39_PAGE_SIZE,
    "grant copy maximum must remain one page"
);

static enum micros_grant_error map_address_error(
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

static enum micros_grant_error plan_range(
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
            address_error =
                micros_user_address_space_validate_tty_uart_mapping(
                    process,
                    device_authority->root_physical_address
                );
            if (
                address_error
                    == MICROS_USER_ADDRESS_SPACE_ERROR_PHASE
            ) {
                return MICROS_GRANT_ERROR_PHASE;
            }
            if (
                address_error
                    != MICROS_USER_ADDRESS_SPACE_OK
            ) {
                return MICROS_GRANT_ERROR_INVARIANT;
            }
            chunk_length =
                (
                    (uintptr_t)MICROS_TTY_UART_VIRTUAL_BASE
                    + MICROS_TTY_UART_MAPPED_LENGTH
                ) - current;
            if (chunk_length > remaining) {
                chunk_length = remaining;
            }
            current += chunk_length;
            remaining -= chunk_length;
            device_fault = true;
            continue;
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
            return map_address_error(address_error);
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

static enum micros_grant_error copy_grant(
    const struct micros_grant_registry *grant_registry,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *objects,
    struct micros_process_handle grantee,
    micros_endpoint_t grantor_endpoint,
    micros_grant_t grant,
    size_t grant_offset,
    uintptr_t local_address,
    size_t length,
    uint32_t required_permission
)
{
    const struct micros_frame_ownership *ledger;
    struct micros_grant_copy_authority authority;
    struct micros_grant_copy_range_plan remote_plan = {0};
    struct micros_grant_copy_range_plan local_plan = {0};
    struct micros_tty_device_authority device_authority;
    const struct micros_grant_copy_range_plan *source_plan;
    const struct micros_grant_copy_range_plan *destination_plan;
    enum micros_grant_error remote_error;
    enum micros_grant_error local_error;
    enum micros_grant_error error;
    enum micros_tty_device_authority_status device_status;
    uintptr_t saved_status;

    if (
        grant_registry == NULL
        || endpoint_registry == NULL
        || objects == NULL
        || grantor_endpoint == MICROS_ENDPOINT_NONE
        || grantor_endpoint == MICROS_ENDPOINT_ANY
        || (
            required_permission != MICROS_GRANT_PERMISSION_READ
            && required_permission != MICROS_GRANT_PERMISSION_WRITE
        )
    ) {
        return MICROS_GRANT_ERROR_ARGUMENT;
    }
    if (length > MICROS_GRANT_COPY_MAX) {
        return MICROS_GRANT_ERROR_RANGE;
    }
    if (
        length != 0
        && (
            local_address == 0
            || local_address < MICROS_USER_VIRTUAL_BASE
            || local_address >= MICROS_USER_VIRTUAL_END
            || UINTPTR_MAX - local_address < length
            || length > MICROS_USER_VIRTUAL_END - local_address
        )
    ) {
        return MICROS_GRANT_ERROR_RANGE;
    }
    saved_status = riscv_irq_save();
    error = micros_grant_prepare_copy_authority(
        grant_registry,
        endpoint_registry,
        objects,
        grantee,
        grantor_endpoint,
        grant,
        grant_offset,
        length,
        required_permission,
        &authority
    );
    if (error != MICROS_GRANT_OK) {
        goto done;
    }
    ledger = micros_frame_ownership_runtime_ledger();
    if (ledger == NULL) {
        error = MICROS_GRANT_ERROR_INVARIANT;
        goto done;
    }
    if (
        ledger->phase
            != MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP
        && ledger->phase
            != MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
    ) {
        error = MICROS_GRANT_ERROR_PHASE;
        goto done;
    }
    if (length == 0) {
        error = MICROS_GRANT_OK;
        goto done;
    }
    device_status =
        micros_tty_handoff_runtime_device_authority(
            endpoint_registry,
            objects,
            &device_authority
        );
    if (
        device_status
            == MICROS_TTY_DEVICE_AUTHORITY_INVARIANT
    ) {
        error = MICROS_GRANT_ERROR_INVARIANT;
        goto done;
    }

    remote_error = plan_range(
        authority.grantor,
        authority.remote_address,
        length,
        required_permission == MICROS_GRANT_PERMISSION_READ
            ? MICROS_SV39_PERMISSION_READ
            : MICROS_SV39_PERMISSION_WRITE,
        device_status == MICROS_TTY_DEVICE_AUTHORITY_ACTIVE
            ? &device_authority
            : NULL,
        &remote_plan
    );
    local_error = plan_range(
        authority.grantee,
        local_address,
        length,
        required_permission == MICROS_GRANT_PERMISSION_READ
            ? MICROS_SV39_PERMISSION_WRITE
            : MICROS_SV39_PERMISSION_READ,
        device_status == MICROS_TTY_DEVICE_AUTHORITY_ACTIVE
            ? &device_authority
            : NULL,
        &local_plan
    );
    if (
        remote_error == MICROS_GRANT_ERROR_INVARIANT
        || local_error == MICROS_GRANT_ERROR_INVARIANT
    ) {
        error = MICROS_GRANT_ERROR_INVARIANT;
        goto done;
    }
    if (
        remote_error == MICROS_GRANT_ERROR_PHASE
        || local_error == MICROS_GRANT_ERROR_PHASE
    ) {
        error = MICROS_GRANT_ERROR_PHASE;
        goto done;
    }
    if (
        remote_error != MICROS_GRANT_OK
        || local_error != MICROS_GRANT_OK
    ) {
        error = MICROS_GRANT_ERROR_FAULT;
        goto done;
    }
    if (required_permission == MICROS_GRANT_PERMISSION_READ) {
        source_plan = &remote_plan;
        destination_plan = &local_plan;
    } else {
        source_plan = &local_plan;
        destination_plan = &remote_plan;
    }
    error = micros_grant_copy_plan_validate(
        source_plan,
        destination_plan,
        length
    );
    if (error != MICROS_GRANT_OK) {
        goto done;
    }
    micros_grant_copy_commit(
        source_plan,
        destination_plan,
        length
    );

done:
    riscv_irq_restore(saved_status);
    return error;
}

enum micros_grant_error micros_grant_validate_range(
    const struct micros_grant_registry *grant_registry,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *objects,
    struct micros_process_handle grantee,
    micros_endpoint_t grantor_endpoint,
    micros_grant_t grant,
    size_t grant_offset,
    size_t length,
    uint32_t required_permission
)
{
    const struct micros_frame_ownership *ledger;
    struct micros_grant_copy_authority authority;
    struct micros_grant_copy_range_plan remote_plan = {0};
    struct micros_tty_device_authority device_authority;
    enum micros_grant_error error;
    enum micros_tty_device_authority_status device_status;
    uintptr_t saved_status;

    if (
        grant_registry == NULL
        || endpoint_registry == NULL
        || objects == NULL
        || grantor_endpoint == MICROS_ENDPOINT_NONE
        || grantor_endpoint == MICROS_ENDPOINT_ANY
        || (
            required_permission != MICROS_GRANT_PERMISSION_READ
            && required_permission != MICROS_GRANT_PERMISSION_WRITE
        )
    ) {
        return MICROS_GRANT_ERROR_ARGUMENT;
    }
    if (length > MICROS_GRANT_COPY_MAX) {
        return MICROS_GRANT_ERROR_RANGE;
    }
    saved_status = riscv_irq_save();
    error = micros_grant_prepare_copy_authority(
        grant_registry,
        endpoint_registry,
        objects,
        grantee,
        grantor_endpoint,
        grant,
        grant_offset,
        length,
        required_permission,
        &authority
    );
    if (error != MICROS_GRANT_OK) {
        goto done;
    }
    ledger = micros_frame_ownership_runtime_ledger();
    if (ledger == NULL) {
        error = MICROS_GRANT_ERROR_INVARIANT;
        goto done;
    }
    if (
        ledger->phase != MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP
        && ledger->phase != MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
    ) {
        error = MICROS_GRANT_ERROR_PHASE;
        goto done;
    }
    if (length == 0) {
        error = MICROS_GRANT_OK;
        goto done;
    }
    device_status =
        micros_tty_handoff_runtime_device_authority(
            endpoint_registry,
            objects,
            &device_authority
        );
    if (
        device_status
            == MICROS_TTY_DEVICE_AUTHORITY_INVARIANT
    ) {
        error = MICROS_GRANT_ERROR_INVARIANT;
        goto done;
    }
    error = plan_range(
        authority.grantor,
        authority.remote_address,
        length,
        required_permission == MICROS_GRANT_PERMISSION_READ
            ? MICROS_SV39_PERMISSION_READ
            : MICROS_SV39_PERMISSION_WRITE,
        device_status == MICROS_TTY_DEVICE_AUTHORITY_ACTIVE
            ? &device_authority
            : NULL,
        &remote_plan
    );

done:
    riscv_irq_restore(saved_status);
    return error;
}

enum micros_grant_error micros_grant_copy_from(
    const struct micros_grant_registry *grant_registry,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *objects,
    struct micros_process_handle grantee,
    micros_endpoint_t grantor_endpoint,
    micros_grant_t grant,
    size_t grant_offset,
    uintptr_t local_address,
    size_t length
)
{
    return copy_grant(
        grant_registry,
        endpoint_registry,
        objects,
        grantee,
        grantor_endpoint,
        grant,
        grant_offset,
        local_address,
        length,
        MICROS_GRANT_PERMISSION_READ
    );
}

enum micros_grant_error micros_grant_copy_to(
    const struct micros_grant_registry *grant_registry,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *objects,
    struct micros_process_handle grantee,
    micros_endpoint_t grantor_endpoint,
    micros_grant_t grant,
    size_t grant_offset,
    uintptr_t local_address,
    size_t length
)
{
    return copy_grant(
        grant_registry,
        endpoint_registry,
        objects,
        grantee,
        grantor_endpoint,
        grant,
        grant_offset,
        local_address,
        length,
        MICROS_GRANT_PERMISSION_WRITE
    );
}

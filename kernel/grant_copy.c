#include "micros/grant_copy.h"

#include "grant_copy_internal.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "tty_handoff_runtime.h"
#include "kernel/memory/user_address_space_internal.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/sv39.h"
#include "micros/user_address_space.h"

_Static_assert(
    MICROS_GRANT_COPY_MAX == MICROS_SV39_PAGE_SIZE,
    "grant copy maximum must remain one page"
);

struct grant_copy_execution {
    const struct micros_frame_ownership *ledger;
    struct micros_grant_copy_authority authority;
    struct micros_grant_copy_range_plan remote_plan;
    struct micros_grant_copy_range_plan local_plan;
    struct micros_tty_device_authority device_authority;
    const struct micros_grant_copy_range_plan *source_plan;
    const struct micros_grant_copy_range_plan *destination_plan;
    enum micros_grant_error remote_error;
    enum micros_grant_error local_error;
    enum micros_grant_error error;
    enum micros_tty_device_authority_status device_status;
};

static bool grant_copy_local_range_is_valid(
    uintptr_t local_address,
    size_t length
)
{
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
        return false;
    }
    return true;
}

static bool grant_copy_prepare_participant_plans(
    struct grant_copy_execution *work,
    uintptr_t local_address,
    size_t length,
    uint32_t required_permission
)
{
    work->remote_error = grant_copy_plan_range(
        work->authority.grantor,
        work->authority.remote_address,
        length,
        required_permission == MICROS_GRANT_PERMISSION_READ
            ? MICROS_SV39_PERMISSION_READ
            : MICROS_SV39_PERMISSION_WRITE,
        work->device_status == MICROS_TTY_DEVICE_AUTHORITY_ACTIVE
            ? &work->device_authority
            : NULL,
        &work->remote_plan
    );
    work->local_error = grant_copy_plan_range(
        work->authority.grantee,
        local_address,
        length,
        required_permission == MICROS_GRANT_PERMISSION_READ
            ? MICROS_SV39_PERMISSION_WRITE
            : MICROS_SV39_PERMISSION_READ,
        work->device_status == MICROS_TTY_DEVICE_AUTHORITY_ACTIVE
            ? &work->device_authority
            : NULL,
        &work->local_plan
    );
    if (
        work->remote_error == MICROS_GRANT_ERROR_INVARIANT
        || work->local_error == MICROS_GRANT_ERROR_INVARIANT
    ) {
        work->error = MICROS_GRANT_ERROR_INVARIANT;
        return false;
    }
    if (
        work->remote_error == MICROS_GRANT_ERROR_PHASE
        || work->local_error == MICROS_GRANT_ERROR_PHASE
    ) {
        work->error = MICROS_GRANT_ERROR_PHASE;
        return false;
    }
    if (
        work->remote_error != MICROS_GRANT_OK
        || work->local_error != MICROS_GRANT_OK
    ) {
        work->error = MICROS_GRANT_ERROR_FAULT;
        return false;
    }
    return true;
}

static enum micros_grant_error grant_copy_execute(
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
    struct grant_copy_execution work;
    work.remote_plan = (struct micros_grant_copy_range_plan){0};
    work.local_plan = (struct micros_grant_copy_range_plan){0};
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
    if (!grant_copy_local_range_is_valid(local_address, length)) {
        return MICROS_GRANT_ERROR_RANGE;
    }
    saved_status = riscv_irq_save();
    work.error = micros_grant_prepare_copy_authority(
        grant_registry,
        endpoint_registry,
        objects,
        grantee,
        grantor_endpoint,
        grant,
        grant_offset,
        length,
        required_permission,
        &work.authority
    );
    if (work.error != MICROS_GRANT_OK) {
        goto done;
    }
    work.ledger = micros_frame_ownership_runtime_ledger();
    if (work.ledger == NULL) {
        work.error = MICROS_GRANT_ERROR_INVARIANT;
        goto done;
    }
    if (
        work.ledger->phase
            != MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP
        && work.ledger->phase
            != MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
    ) {
        work.error = MICROS_GRANT_ERROR_PHASE;
        goto done;
    }
    if (length == 0) {
        work.error = MICROS_GRANT_OK;
        goto done;
    }
    work.device_status =
        micros_tty_handoff_runtime_device_authority(
            endpoint_registry,
            objects,
            &work.device_authority
        );
    if (
        work.device_status
            == MICROS_TTY_DEVICE_AUTHORITY_INVARIANT
    ) {
        work.error = MICROS_GRANT_ERROR_INVARIANT;
        goto done;
    }

    if (!grant_copy_prepare_participant_plans(
        &work, local_address, length, required_permission
    )) {
        goto done;
    }
    if (required_permission == MICROS_GRANT_PERMISSION_READ) {
        work.source_plan = &work.remote_plan;
        work.destination_plan = &work.local_plan;
    } else {
        work.source_plan = &work.local_plan;
        work.destination_plan = &work.remote_plan;
    }
    work.error = micros_grant_copy_plan_validate(
        work.source_plan,
        work.destination_plan,
        length
    );
    if (work.error != MICROS_GRANT_OK) {
        goto done;
    }
    micros_grant_copy_commit(
        work.source_plan,
        work.destination_plan,
        length
    );

done:
    riscv_irq_restore(saved_status);
    return work.error;
}

struct grant_copy_validation {
    const struct micros_frame_ownership *ledger;
    struct micros_grant_copy_authority authority;
    struct micros_grant_copy_range_plan remote_plan;
    struct micros_tty_device_authority device_authority;
    enum micros_grant_error error;
    enum micros_tty_device_authority_status device_status;
};

static void grant_copy_prepare_remote_plan(
    struct grant_copy_validation *work,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *objects,
    size_t length,
    uint32_t required_permission
)
{
    work->device_status =
        micros_tty_handoff_runtime_device_authority(
            endpoint_registry,
            objects,
            &work->device_authority
        );
    if (
        work->device_status
            == MICROS_TTY_DEVICE_AUTHORITY_INVARIANT
    ) {
        work->error = MICROS_GRANT_ERROR_INVARIANT;
        return;
    }
    work->error = grant_copy_plan_range(
        work->authority.grantor,
        work->authority.remote_address,
        length,
        required_permission == MICROS_GRANT_PERMISSION_READ
            ? MICROS_SV39_PERMISSION_READ
            : MICROS_SV39_PERMISSION_WRITE,
        work->device_status == MICROS_TTY_DEVICE_AUTHORITY_ACTIVE
            ? &work->device_authority
            : NULL,
        &work->remote_plan
    );
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
    struct grant_copy_validation work;
    work.remote_plan = (struct micros_grant_copy_range_plan){0};
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
    work.error = micros_grant_prepare_copy_authority(
        grant_registry,
        endpoint_registry,
        objects,
        grantee,
        grantor_endpoint,
        grant,
        grant_offset,
        length,
        required_permission,
        &work.authority
    );
    if (work.error != MICROS_GRANT_OK) {
        goto done;
    }
    work.ledger = micros_frame_ownership_runtime_ledger();
    if (work.ledger == NULL) {
        work.error = MICROS_GRANT_ERROR_INVARIANT;
        goto done;
    }
    if (
        work.ledger->phase != MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP
        && work.ledger->phase != MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
    ) {
        work.error = MICROS_GRANT_ERROR_PHASE;
        goto done;
    }
    if (length == 0) {
        work.error = MICROS_GRANT_OK;
        goto done;
    }
    grant_copy_prepare_remote_plan(
        &work, endpoint_registry, objects, length, required_permission
    );

done:
    riscv_irq_restore(saved_status);
    return work.error;
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
    return grant_copy_execute(
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
    return grant_copy_execute(
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

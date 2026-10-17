#include "micros/endpoint.h"

#include <stdint.h>

#include "kernel/endpoint/endpoint_private.h"

enum micros_endpoint_error micros_endpoint_resolve_internal(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint,
    const struct micros_endpoint_record **record
)
{
    enum micros_endpoint_error error;

    if (registry == NULL || objects == NULL || record == NULL) {
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    error = micros_endpoint_registry_validate_objects(registry, objects);
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    return endpoint_record_resolve_validated(registry, endpoint, record);
}

enum micros_endpoint_error micros_endpoint_resolve_active(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint,
    const struct micros_endpoint_record **record
)
{
    enum micros_endpoint_error error;
    const struct micros_endpoint_record *resolved_record;

    if (record == NULL) {
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    error = micros_endpoint_resolve_internal(
        registry,
        objects,
        endpoint,
        &resolved_record
    );
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    if (resolved_record->state != MICROS_ENDPOINT_STATE_ACTIVE) {
        return resolved_record->state
                == MICROS_ENDPOINT_STATE_SOURCE_ONLY
            ? MICROS_ENDPOINT_ERROR_CLOSING
            : MICROS_ENDPOINT_ERROR_STATE;
    }
    *record = resolved_record;
    return MICROS_ENDPOINT_OK;
}

enum micros_endpoint_error micros_endpoint_authorize_operation(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t source,
    uint32_t operation
)
{
    const struct micros_endpoint_record *record;
    const struct micros_process *process;
    enum micros_endpoint_error error;

    error = micros_endpoint_resolve_active(
        registry,
        objects,
        source,
        &record
    );
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    process = &objects->processes[record->owner.slot];
    return micros_privilege_profile_allows_operation(
        registry,
        (uint8_t)process->privilege_profile,
        operation
    );
}

enum micros_endpoint_error micros_endpoint_authorize_target(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t source,
    uint32_t operation,
    micros_endpoint_t target
)
{
    const struct micros_endpoint_record *source_record;
    const struct micros_endpoint_record *target_record;
    const struct micros_process *source_process;
    const struct micros_process *target_process;
    enum micros_endpoint_error error;

    if (
        operation != MICROS_PRIVILEGE_OPERATION_CALL
        && operation != MICROS_PRIVILEGE_OPERATION_SEND
        && operation != MICROS_PRIVILEGE_OPERATION_NOTIFY
    ) {
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    error = micros_endpoint_resolve_active(
        registry,
        objects,
        source,
        &source_record
    );
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    error = micros_endpoint_resolve_active(
        registry,
        objects,
        target,
        &target_record
    );
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    source_process = &objects->processes[source_record->owner.slot];
    target_process = &objects->processes[target_record->owner.slot];
    return micros_privilege_profile_allows_target(
        registry,
        (uint8_t)source_process->privilege_profile,
        operation,
        (uint8_t)target_process->privilege_profile
    );
}

enum micros_endpoint_error
micros_endpoint_authorize_kernel_operation(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t source,
    uint8_t operation
)
{
    const struct micros_endpoint_record *record;
    const struct micros_process *process;
    enum micros_endpoint_error error;

    error = micros_endpoint_resolve_active(
        registry,
        objects,
        source,
        &record
    );
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    process = &objects->processes[record->owner.slot];
    return micros_privilege_profile_allows_kernel_operation(
        registry,
        (uint8_t)process->privilege_profile,
        operation
    );
}

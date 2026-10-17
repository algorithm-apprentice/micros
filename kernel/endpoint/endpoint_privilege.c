#include "kernel/endpoint/endpoint_private.h"

enum micros_endpoint_error micros_privilege_profile_resolve(
    const struct micros_endpoint_registry *registry,
    uint8_t profile_id,
    const struct micros_privilege_profile **profile
)
{
    enum micros_endpoint_error error;

    if (profile == NULL) {
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    error = micros_endpoint_registry_validate(registry);
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    if (
        profile_id == 0
        || profile_id >= MICROS_PRIVILEGE_PROFILE_CAPACITY
        || registry->profiles[profile_id].id != profile_id
    ) {
        return MICROS_ENDPOINT_ERROR_PROFILE;
    }
    *profile = &registry->profiles[profile_id];
    return MICROS_ENDPOINT_OK;
}

enum micros_endpoint_error micros_privilege_profile_allows_operation(
    const struct micros_endpoint_registry *registry,
    uint8_t profile_id,
    uint32_t operation
)
{
    const struct micros_privilege_profile *profile;
    enum micros_endpoint_error error;

    if (
        operation == 0
        || (operation & (operation - 1)) != 0
        || (
            operation
            & ~MICROS_PRIVILEGE_OPERATION_DEFINED_MASK
        ) != 0
    ) {
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    error = micros_privilege_profile_resolve(
        registry,
        profile_id,
        &profile
    );
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    return (profile->operations & operation) != 0
        ? MICROS_ENDPOINT_OK
        : MICROS_ENDPOINT_ERROR_UNAUTHORIZED;
}

enum micros_endpoint_error micros_privilege_profile_allows_target(
    const struct micros_endpoint_registry *registry,
    uint8_t source_profile_id,
    uint32_t operation,
    uint8_t destination_profile_id
)
{
    const struct micros_privilege_profile *source;
    const struct micros_privilege_profile *destination;
    uint32_t targets;
    enum micros_endpoint_error error;

    if (
        operation != MICROS_PRIVILEGE_OPERATION_CALL
        && operation != MICROS_PRIVILEGE_OPERATION_SEND
        && operation != MICROS_PRIVILEGE_OPERATION_NOTIFY
    ) {
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    error = micros_privilege_profile_allows_operation(
        registry,
        source_profile_id,
        operation
    );
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    error = micros_privilege_profile_resolve(
        registry,
        source_profile_id,
        &source
    );
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    error = micros_privilege_profile_resolve(
        registry,
        destination_profile_id,
        &destination
    );
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    (void)destination;
    switch (operation) {
    case MICROS_PRIVILEGE_OPERATION_CALL:
        targets = source->call_targets;
        break;
    case MICROS_PRIVILEGE_OPERATION_SEND:
        targets = source->send_targets;
        break;
    case MICROS_PRIVILEGE_OPERATION_NOTIFY:
        targets = source->notify_targets;
        break;
    default:
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    return (
        targets & (UINT32_C(1) << destination_profile_id)
    ) != 0
        ? MICROS_ENDPOINT_OK
        : MICROS_ENDPOINT_ERROR_UNAUTHORIZED;
}

enum micros_endpoint_error micros_privilege_profile_allows_kernel_operation(
    const struct micros_endpoint_registry *registry,
    uint8_t profile_id,
    uint8_t operation
)
{
    const struct micros_privilege_profile *profile;
    enum micros_endpoint_error error;

    if (operation >= 64) {
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    error = micros_privilege_profile_resolve(
        registry,
        profile_id,
        &profile
    );
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    return (
        profile->kernel_operations
        & (UINT64_C(1) << operation)
    ) != 0
        ? MICROS_ENDPOINT_OK
        : MICROS_ENDPOINT_ERROR_UNAUTHORIZED;
}

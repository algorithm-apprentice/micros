#include "endpoint_private.h"
#include "endpoint_internal.h"

enum micros_endpoint_error micros_endpoint_reserve(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_process_handle process_handle,
    micros_endpoint_t *endpoint
)
{
    const struct micros_process *resolved_process;
    struct micros_process *process;
    struct micros_endpoint_record *record;
    enum micros_kernel_object_error object_error;
    enum micros_endpoint_error error;
    micros_endpoint_t value;

    if (registry == NULL || objects == NULL || endpoint == NULL) {
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    error = micros_endpoint_registry_validate_objects(registry, objects);
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    object_error = micros_process_resolve(
        objects,
        process_handle,
        &resolved_process
    );
    if (object_error != MICROS_KERNEL_OBJECT_OK) {
        return endpoint_process_resolve_error(object_error);
    }
    (void)resolved_process;
    process = &objects->processes[process_handle.slot];
    record = &registry->endpoints[process_handle.slot];
    if (
        process->primary_endpoint != MICROS_PROCESS_ENDPOINT_NONE
        || process->privilege_profile != 0
        || process->endpoint_lifecycle_consumed
        || record->state != MICROS_ENDPOINT_STATE_FREE
    ) {
        return MICROS_ENDPOINT_ERROR_STATE;
    }
    error = micros_endpoint_pack(process_handle, &value);
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }

    record->state = MICROS_ENDPOINT_STATE_RESERVED;
    record->value = value;
    record->owner = process_handle;
    process->primary_endpoint = value;
    process->endpoint_lifecycle_consumed = true;
    *endpoint = value;
    return MICROS_ENDPOINT_OK;
}

enum micros_endpoint_error micros_endpoint_install_profile(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_process_handle process_handle,
    uint8_t profile_id
)
{
    const struct micros_process *resolved_process;
    const struct micros_privilege_profile *unused_profile;
    struct micros_process *process;
    const struct micros_endpoint_record *record;
    enum micros_kernel_object_error object_error;
    enum micros_endpoint_error error;

    if (registry == NULL || objects == NULL) {
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    error = micros_endpoint_registry_validate_objects(registry, objects);
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    object_error = micros_process_resolve(
        objects,
        process_handle,
        &resolved_process
    );
    if (object_error != MICROS_KERNEL_OBJECT_OK) {
        return endpoint_process_resolve_error(object_error);
    }
    (void)resolved_process;
    process = &objects->processes[process_handle.slot];
    if (
        process->primary_endpoint == MICROS_PROCESS_ENDPOINT_NONE
        || process->privilege_profile != 0
    ) {
        return MICROS_ENDPOINT_ERROR_STATE;
    }
    record = &registry->endpoints[process_handle.slot];
    if (
        record->state != MICROS_ENDPOINT_STATE_RESERVED
        || record->value != process->primary_endpoint
    ) {
        return MICROS_ENDPOINT_ERROR_STATE;
    }
    error = micros_privilege_profile_resolve(
        registry,
        profile_id,
        &unused_profile
    );
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    if (!endpoint_process_threads_are_held(objects, process_handle, true)) {
        return MICROS_ENDPOINT_ERROR_STATE;
    }

    process->privilege_profile = profile_id;
    return MICROS_ENDPOINT_OK;
}

enum micros_endpoint_error micros_endpoint_activate(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint
)
{
    const struct micros_endpoint_record *record;
    const struct micros_process *process;
    enum micros_endpoint_error error;

    if (registry == NULL || objects == NULL) {
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    error = micros_endpoint_registry_validate_objects(registry, objects);
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    error = endpoint_record_resolve_validated(
        registry,
        endpoint,
        &record
    );
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    if (record->state != MICROS_ENDPOINT_STATE_RESERVED) {
        return MICROS_ENDPOINT_ERROR_STATE;
    }
    process = &objects->processes[record->owner.slot];
    if (process->privilege_profile == 0) {
        return MICROS_ENDPOINT_ERROR_STATE;
    }

    registry->endpoints[record->owner.slot].state =
        MICROS_ENDPOINT_STATE_ACTIVE;
    return MICROS_ENDPOINT_OK;
}

enum micros_endpoint_error micros_endpoint_preflight_publish(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint,
    uint8_t profile_id
)
{
    const struct micros_endpoint_record *record;
    const struct micros_privilege_profile *profile;
    const struct micros_process *process;
    enum micros_endpoint_error error;

    if (registry == NULL || objects == NULL) {
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    error = micros_endpoint_registry_validate_objects(registry, objects);
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    error = endpoint_record_resolve_validated(
        registry,
        endpoint,
        &record
    );
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    error = micros_privilege_profile_resolve(
        registry,
        profile_id,
        &profile
    );
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    process = &objects->processes[record->owner.slot];
    if (
        record->state != MICROS_ENDPOINT_STATE_RESERVED
        || !endpoint_ipc_state_is_zero(record)
        || process->privilege_profile != 0
        || !endpoint_process_threads_are_held(objects, record->owner, true)
    ) {
        return MICROS_ENDPOINT_ERROR_STATE;
    }
    (void)profile;
    return MICROS_ENDPOINT_OK;
}

void micros_endpoint_commit_publish_prevalidated(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint,
    uint8_t profile_id
)
{
    struct micros_process_handle owner;

    (void)micros_endpoint_unpack(endpoint, &owner);
    objects->processes[owner.slot].privilege_profile = profile_id;
    registry->endpoints[owner.slot].state =
        MICROS_ENDPOINT_STATE_ACTIVE;
}

#include "kernel/endpoint/endpoint_private.h"
#include "kernel/endpoint/endpoint_internal.h"

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
    enum micros_endpoint_error error;
    micros_endpoint_t value;

    if (registry == NULL || objects == NULL || endpoint == NULL) {
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    error = endpoint_preflight_process(registry, objects, process_handle, &resolved_process);
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    process = &objects->processes[process_handle.slot];
    record = &registry->endpoints[process_handle.slot];
    if (
        process->primary_endpoint != MICROS_PROCESS_ENDPOINT_NONE
        || process->privilege_profile != 0
        || process->endpoint_lifecycle_consumed
    ) {
        return MICROS_ENDPOINT_ERROR_STATE;
    }
    if (!endpoint_record_is_zero(record)) {
        return MICROS_ENDPOINT_ERROR_INVARIANT;
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
    struct micros_endpoint_selection selected;
    enum micros_endpoint_error error;

    if (registry == NULL || objects == NULL) {
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    error = endpoint_preflight_process(registry, objects, process_handle, &resolved_process);
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    process = &objects->processes[process_handle.slot];
    if (
        process->primary_endpoint == MICROS_PROCESS_ENDPOINT_NONE
        || process->privilege_profile != 0
    ) {
        return MICROS_ENDPOINT_ERROR_STATE;
    }
    error = micros_endpoint_select(registry, objects, process->primary_endpoint, &selected);
    if (error != MICROS_ENDPOINT_OK || selected.process != resolved_process) {
        return MICROS_ENDPOINT_ERROR_INVARIANT;
    }
    error = endpoint_preflight_record(registry, objects, &selected);
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    if (selected.record->state != MICROS_ENDPOINT_STATE_RESERVED) {
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
    error = endpoint_preflight_owned_threads(registry, objects, process_handle, true);
    if (error != MICROS_ENDPOINT_OK) {
        return error;
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
    struct micros_endpoint_selection selected;
    enum micros_endpoint_error error;

    if (registry == NULL || objects == NULL) {
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    error = micros_endpoint_select(registry, objects, endpoint, &selected);
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    error = endpoint_preflight_record(registry, objects, &selected);
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    if (selected.record->state != MICROS_ENDPOINT_STATE_RESERVED) {
        return MICROS_ENDPOINT_ERROR_STATE;
    }
    if (selected.process->privilege_profile == 0) {
        return MICROS_ENDPOINT_ERROR_STATE;
    }

    registry->endpoints[selected.record->owner.slot].state =
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
    struct micros_endpoint_selection selected;
    const struct micros_privilege_profile *profile;
    enum micros_endpoint_error error;

    if (registry == NULL || objects == NULL) {
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    error = micros_endpoint_select(registry, objects, endpoint, &selected);
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    error = endpoint_preflight_record(registry, objects, &selected);
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
    if (
        selected.record->state != MICROS_ENDPOINT_STATE_RESERVED
        || selected.process->privilege_profile != 0
    ) {
        return MICROS_ENDPOINT_ERROR_STATE;
    }
    (void)profile;
    return endpoint_preflight_owned_threads(registry, objects, selected.record->owner, true);
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

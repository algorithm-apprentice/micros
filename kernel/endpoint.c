#include "micros/endpoint.h"

#include <stdint.h>

#define MICROS_ENDPOINT_REGISTRY_MAGIC UINT64_C(0x4d4943524f534550)

_Static_assert(
    MICROS_PROCESS_CAPACITY <= (1U << MICROS_ENDPOINT_SLOT_BITS),
    "implemented process capacity must fit the endpoint ABI"
);
_Static_assert(
    MICROS_PROCESS_GENERATION_MAX == MICROS_ENDPOINT_GENERATION_MAX,
    "process and endpoint generation limits must match"
);

static bool storage_is_zero(
    const struct micros_endpoint_registry *registry
)
{
    const unsigned char *bytes = (const unsigned char *)registry;
    size_t index;

    for (index = 0; index < sizeof(*registry); ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static void clear_bytes(void *storage, size_t size)
{
    unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static void copy_bytes(
    void *destination,
    const void *source,
    size_t size
)
{
    unsigned char *destination_bytes = destination;
    const unsigned char *source_bytes = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        destination_bytes[index] = source_bytes[index];
    }
}

static bool bytes_equal(
    const void *left,
    const void *right,
    size_t size
)
{
    const unsigned char *left_bytes = left;
    const unsigned char *right_bytes = right;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (left_bytes[index] != right_bytes[index]) {
            return false;
        }
    }
    return true;
}

static bool names_equal(
    const char *left,
    const char *right
)
{
    size_t index;

    for (index = 0; index < MICROS_PRIVILEGE_PROFILE_NAME_SIZE; ++index) {
        if (left[index] != right[index]) {
            return false;
        }
        if (left[index] == '\0') {
            return true;
        }
    }
    return false;
}

static bool profile_is_zero(
    const struct micros_privilege_profile *profile
)
{
    const unsigned char *bytes = (const unsigned char *)profile;
    size_t index;

    for (index = 0; index < sizeof(*profile); ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static bool endpoint_record_is_zero(
    const struct micros_endpoint_record *endpoint
)
{
    return (
        endpoint->state == MICROS_ENDPOINT_STATE_FREE
        && endpoint->owner.slot == 0
        && endpoint->owner.generation == 0
        && endpoint->value == 0
    );
}

static bool process_handles_equal(
    struct micros_process_handle left,
    struct micros_process_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

static enum micros_endpoint_error canonicalize_profile(
    const struct micros_privilege_profile *source,
    struct micros_privilege_profile *destination
)
{
    size_t index;
    bool terminated = false;

    if (
        source == NULL
        || destination == NULL
        || source->id == 0
        || source->id >= MICROS_PRIVILEGE_PROFILE_CAPACITY
        || source->operations == 0
        || (
            source->operations
            & ~MICROS_PRIVILEGE_OPERATION_DEFINED_MASK
        ) != 0
    ) {
        return MICROS_ENDPOINT_ERROR_PROFILE;
    }
    clear_bytes(destination, sizeof(*destination));
    destination->id = source->id;
    for (
        index = 0;
        index < MICROS_PRIVILEGE_PROFILE_NAME_SIZE;
        ++index
    ) {
        unsigned char character = (unsigned char)source->name[index];

        if (character == '\0') {
            terminated = true;
            break;
        }
        if (character < 0x20 || character > 0x7e) {
            return MICROS_ENDPOINT_ERROR_PROFILE;
        }
        destination->name[index] = (char)character;
    }
    if (!terminated || index == 0) {
        return MICROS_ENDPOINT_ERROR_PROFILE;
    }
    destination->operations = source->operations;
    destination->call_targets = source->call_targets;
    destination->send_targets = source->send_targets;
    destination->notify_targets = source->notify_targets;
    destination->kernel_operations = source->kernel_operations;

    if (
        (
            destination->call_targets != 0
            && (
                destination->operations
                & MICROS_PRIVILEGE_OPERATION_CALL
            ) == 0
        )
        || (
            destination->send_targets != 0
            && (
                destination->operations
                & MICROS_PRIVILEGE_OPERATION_SEND
            ) == 0
        )
        || (
            destination->notify_targets != 0
            && (
                destination->operations
                & MICROS_PRIVILEGE_OPERATION_NOTIFY
            ) == 0
        )
    ) {
        return MICROS_ENDPOINT_ERROR_PROFILE;
    }
    return MICROS_ENDPOINT_OK;
}

static enum micros_endpoint_error require_initialized(
    const struct micros_endpoint_registry *registry
)
{
    if (registry == NULL) {
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    if (
        registry->initialization_magic
            != MICROS_ENDPOINT_REGISTRY_MAGIC
    ) {
        return MICROS_ENDPOINT_ERROR_NOT_INITIALIZED;
    }
    return MICROS_ENDPOINT_OK;
}

enum micros_endpoint_error micros_endpoint_pack(
    struct micros_process_handle process,
    micros_endpoint_t *endpoint
)
{
    micros_endpoint_t candidate;

    if (endpoint == NULL) {
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    if (
        process.slot >= (1U << MICROS_ENDPOINT_SLOT_BITS)
        || process.generation == 0
        || process.generation > MICROS_ENDPOINT_GENERATION_MAX
    ) {
        return MICROS_ENDPOINT_ERROR_ENDPOINT;
    }
    candidate =
        (process.generation << MICROS_ENDPOINT_SLOT_BITS)
        | process.slot;
    if (
        candidate == MICROS_ENDPOINT_NONE
        || candidate == MICROS_ENDPOINT_ANY
    ) {
        return MICROS_ENDPOINT_ERROR_ENDPOINT;
    }
    *endpoint = candidate;
    return MICROS_ENDPOINT_OK;
}

enum micros_endpoint_error micros_endpoint_unpack(
    micros_endpoint_t endpoint,
    struct micros_process_handle *process
)
{
    struct micros_process_handle candidate;

    if (process == NULL) {
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    if (
        endpoint == MICROS_ENDPOINT_NONE
        || endpoint == MICROS_ENDPOINT_ANY
    ) {
        return MICROS_ENDPOINT_ERROR_STALE;
    }
    candidate.slot =
        (uint16_t)(endpoint & ((1U << MICROS_ENDPOINT_SLOT_BITS) - 1));
    candidate.generation =
        endpoint >> MICROS_ENDPOINT_SLOT_BITS;
    if (
        candidate.generation == 0
        || candidate.generation > MICROS_ENDPOINT_GENERATION_MAX
    ) {
        return MICROS_ENDPOINT_ERROR_STALE;
    }
    *process = candidate;
    return MICROS_ENDPOINT_OK;
}

enum micros_endpoint_error micros_endpoint_registry_initialize(
    struct micros_endpoint_registry *registry,
    const struct micros_privilege_profile *profiles,
    size_t profile_count
)
{
    struct micros_endpoint_registry candidate;
    uint32_t installed = 0;
    size_t source_index;
    size_t other_index;

    if (registry == NULL || profiles == NULL) {
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    if (!storage_is_zero(registry)) {
        if (
            registry->initialization_magic
                == MICROS_ENDPOINT_REGISTRY_MAGIC
        ) {
            return MICROS_ENDPOINT_ERROR_ALREADY_INITIALIZED;
        }
        return MICROS_ENDPOINT_ERROR_STORAGE;
    }
    if (
        profile_count == 0
        || profile_count >= MICROS_PRIVILEGE_PROFILE_CAPACITY
    ) {
        return MICROS_ENDPOINT_ERROR_CAPACITY;
    }

    clear_bytes(&candidate, sizeof(candidate));
    for (source_index = 0; source_index < profile_count; ++source_index) {
        struct micros_privilege_profile canonical;
        enum micros_endpoint_error error =
            canonicalize_profile(
                &profiles[source_index],
                &canonical
            );

        if (
            error != MICROS_ENDPOINT_OK
            || (
                installed
                & (UINT32_C(1) << canonical.id)
            ) != 0
        ) {
            return MICROS_ENDPOINT_ERROR_PROFILE;
        }
        for (other_index = 0; other_index < source_index; ++other_index) {
            uint8_t other_id = profiles[other_index].id;

            if (
                other_id < MICROS_PRIVILEGE_PROFILE_CAPACITY
                && names_equal(
                    canonical.name,
                    candidate.profiles[other_id].name
                )
            ) {
                return MICROS_ENDPOINT_ERROR_PROFILE;
            }
        }
        candidate.profiles[canonical.id] = canonical;
        installed |= UINT32_C(1) << canonical.id;
    }
    for (source_index = 1;
        source_index < MICROS_PRIVILEGE_PROFILE_CAPACITY;
        ++source_index) {
        const struct micros_privilege_profile *profile =
            &candidate.profiles[source_index];
        uint32_t targets;

        if (profile->id == 0) {
            continue;
        }
        targets =
            profile->call_targets
            | profile->send_targets
            | profile->notify_targets;
        if (
            (targets & UINT32_C(1)) != 0
            || (targets & ~installed) != 0
        ) {
            return MICROS_ENDPOINT_ERROR_PROFILE;
        }
    }

    candidate.initialization_magic =
        MICROS_ENDPOINT_REGISTRY_MAGIC;
    candidate.profile_count = profile_count;
    copy_bytes(registry, &candidate, sizeof(candidate));
    return MICROS_ENDPOINT_OK;
}

enum micros_endpoint_error micros_endpoint_registry_validate(
    const struct micros_endpoint_registry *registry
)
{
    uint32_t installed = 0;
    size_t observed_count = 0;
    size_t index;
    size_t other_index;
    enum micros_endpoint_error error;

    error = require_initialized(registry);
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    if (
        registry->profile_count == 0
        || registry->profile_count
            >= MICROS_PRIVILEGE_PROFILE_CAPACITY
        || !profile_is_zero(&registry->profiles[0])
    ) {
        return MICROS_ENDPOINT_ERROR_INVARIANT;
    }
    for (index = 1; index < MICROS_PRIVILEGE_PROFILE_CAPACITY; ++index) {
        const struct micros_privilege_profile *profile =
            &registry->profiles[index];
        struct micros_privilege_profile canonical;

        if (profile->id == 0) {
            if (!profile_is_zero(profile)) {
                return MICROS_ENDPOINT_ERROR_INVARIANT;
            }
            continue;
        }
        if (
            profile->id != index
            || canonicalize_profile(profile, &canonical)
                != MICROS_ENDPOINT_OK
            || !bytes_equal(profile, &canonical, sizeof(canonical))
        ) {
            return MICROS_ENDPOINT_ERROR_INVARIANT;
        }
        for (other_index = 1; other_index < index; ++other_index) {
            const struct micros_privilege_profile *other =
                &registry->profiles[other_index];

            if (
                other->id != 0
                && names_equal(profile->name, other->name)
            ) {
                return MICROS_ENDPOINT_ERROR_INVARIANT;
            }
        }
        installed |= UINT32_C(1) << profile->id;
        ++observed_count;
    }
    if (observed_count != registry->profile_count) {
        return MICROS_ENDPOINT_ERROR_INVARIANT;
    }
    for (index = 1; index < MICROS_PRIVILEGE_PROFILE_CAPACITY; ++index) {
        const struct micros_privilege_profile *profile =
            &registry->profiles[index];
        uint32_t targets;

        if (profile->id == 0) {
            continue;
        }
        targets =
            profile->call_targets
            | profile->send_targets
            | profile->notify_targets;
        if (
            (targets & UINT32_C(1)) != 0
            || (targets & ~installed) != 0
        ) {
            return MICROS_ENDPOINT_ERROR_INVARIANT;
        }
    }
    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        const struct micros_endpoint_record *endpoint =
            &registry->endpoints[index];
        micros_endpoint_t expected_value;

        if (endpoint->state == MICROS_ENDPOINT_STATE_FREE) {
            if (!endpoint_record_is_zero(endpoint)) {
                return MICROS_ENDPOINT_ERROR_INVARIANT;
            }
            continue;
        }
        if (
            endpoint->state != MICROS_ENDPOINT_STATE_RESERVED
            && endpoint->state != MICROS_ENDPOINT_STATE_ACTIVE
        ) {
            return MICROS_ENDPOINT_ERROR_INVARIANT;
        }
        if (endpoint->owner.slot != index) {
            return MICROS_ENDPOINT_ERROR_INVARIANT;
        }
        if (
            micros_endpoint_pack(endpoint->owner, &expected_value)
                != MICROS_ENDPOINT_OK
            || endpoint->value != expected_value
        ) {
            return MICROS_ENDPOINT_ERROR_INVARIANT;
        }
    }
    return MICROS_ENDPOINT_OK;
}

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

static enum micros_endpoint_error process_resolve_error(
    enum micros_kernel_object_error error
)
{
    if (error == MICROS_KERNEL_OBJECT_ERROR_STALE) {
        return MICROS_ENDPOINT_ERROR_STALE;
    }
    if (error == MICROS_KERNEL_OBJECT_ERROR_ARGUMENT) {
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    return MICROS_ENDPOINT_ERROR_INVARIANT;
}

static enum micros_endpoint_error endpoint_record_resolve_validated(
    const struct micros_endpoint_registry *registry,
    micros_endpoint_t endpoint,
    const struct micros_endpoint_record **record
)
{
    struct micros_process_handle owner;
    const struct micros_endpoint_record *candidate;
    enum micros_endpoint_error error;

    error = micros_endpoint_unpack(endpoint, &owner);
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    if (owner.slot >= MICROS_PROCESS_CAPACITY) {
        return MICROS_ENDPOINT_ERROR_STALE;
    }
    candidate = &registry->endpoints[owner.slot];
    if (
        candidate->state == MICROS_ENDPOINT_STATE_FREE
        || candidate->value != endpoint
        || !process_handles_equal(candidate->owner, owner)
    ) {
        return MICROS_ENDPOINT_ERROR_STALE;
    }
    *record = candidate;
    return MICROS_ENDPOINT_OK;
}

static bool process_has_current_thread(
    const struct micros_kernel_objects *objects,
    struct micros_process_handle process
)
{
    size_t index;

    for (index = 0; index < MICROS_HART_CAPACITY; ++index) {
        const struct micros_hart *hart = &objects->harts[index];
        const struct micros_thread *thread;

        if (
            hart->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || hart->current_thread.generation == 0
        ) {
            continue;
        }
        thread = &objects->threads[hart->current_thread.slot];
        if (process_handles_equal(thread->owner, process)) {
            return true;
        }
    }
    return false;
}

static bool process_threads_are_held(
    const struct micros_kernel_objects *objects,
    struct micros_process_handle process,
    bool require_unassigned
)
{
    size_t index;

    if (process_has_current_thread(objects, process)) {
        return false;
    }
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *thread = &objects->threads[index];

        if (
            thread->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || !process_handles_equal(thread->owner, process)
        ) {
            continue;
        }
        if (
            thread->runtime_flags != MICROS_THREAD_RTS_INACTIVE
            || (require_unassigned && thread->scheduler_assigned)
        ) {
            return false;
        }
    }
    return true;
}

enum micros_endpoint_error micros_endpoint_registry_validate_objects(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects
)
{
    enum micros_endpoint_error endpoint_error;
    size_t index;

    if (registry == NULL || objects == NULL) {
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    endpoint_error = micros_endpoint_registry_validate(registry);
    if (endpoint_error != MICROS_ENDPOINT_OK) {
        return endpoint_error;
    }
    if (
        micros_kernel_objects_validate(objects)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        return MICROS_ENDPOINT_ERROR_INVARIANT;
    }

    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        const struct micros_endpoint_record *endpoint =
            &registry->endpoints[index];
        const struct micros_process *process =
            &objects->processes[index];
        const struct micros_privilege_profile *unused_profile;

        if (endpoint->state == MICROS_ENDPOINT_STATE_FREE) {
            if (
                process->slot_state
                    == MICROS_KERNEL_OBJECT_SLOT_LIVE
                && (
                    process->primary_endpoint
                        != MICROS_PROCESS_ENDPOINT_NONE
                    || process->privilege_profile != 0
                )
            ) {
                return MICROS_ENDPOINT_ERROR_INVARIANT;
            }
            continue;
        }
        if (
            process->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || process->generation != endpoint->owner.generation
            || process->primary_endpoint != endpoint->value
            || !process->endpoint_lifecycle_consumed
        ) {
            return MICROS_ENDPOINT_ERROR_INVARIANT;
        }
        if (process->privilege_profile != 0) {
            if (
                process->privilege_profile
                    >= MICROS_PRIVILEGE_PROFILE_CAPACITY
                || micros_privilege_profile_resolve(
                    registry,
                    (uint8_t)process->privilege_profile,
                    &unused_profile
                ) != MICROS_ENDPOINT_OK
            ) {
                return MICROS_ENDPOINT_ERROR_INVARIANT;
            }
        } else if (endpoint->state == MICROS_ENDPOINT_STATE_ACTIVE) {
            return MICROS_ENDPOINT_ERROR_INVARIANT;
        }
    }
    return MICROS_ENDPOINT_OK;
}

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
        return process_resolve_error(object_error);
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
        return process_resolve_error(object_error);
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
    if (!process_threads_are_held(objects, process_handle, true)) {
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
        return MICROS_ENDPOINT_ERROR_STATE;
    }
    *record = resolved_record;
    return MICROS_ENDPOINT_OK;
}

enum micros_endpoint_error micros_endpoint_close(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint
)
{
    const struct micros_endpoint_record *resolved_record;
    struct micros_endpoint_record *record;
    struct micros_process *process;
    struct micros_process_handle owner;
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
        &resolved_record
    );
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    owner = resolved_record->owner;
    if (!process_threads_are_held(objects, owner, false)) {
        return MICROS_ENDPOINT_ERROR_STATE;
    }

    record = &registry->endpoints[owner.slot];
    process = &objects->processes[owner.slot];
    record->state = MICROS_ENDPOINT_STATE_FREE;
    record->value = 0;
    record->owner.slot = 0;
    record->owner.generation = 0;
    process->primary_endpoint = MICROS_PROCESS_ENDPOINT_NONE;
    process->privilege_profile = 0;
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

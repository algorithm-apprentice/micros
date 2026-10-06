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
        if (!endpoint_record_is_zero(&registry->endpoints[index])) {
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

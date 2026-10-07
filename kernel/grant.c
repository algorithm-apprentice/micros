#include "micros/grant.h"
#include "micros/grant_copy.h"

#include <stddef.h>
#include <stdint.h>

#include "micros/user_address_space.h"

#define MICROS_GRANT_REGISTRY_MAGIC UINT64_C(0x4d4943524f534752)

static bool bytes_are_zero(const void *storage, size_t size)
{
    const unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
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

static void copy_bytes(void *destination, const void *source, size_t size)
{
    unsigned char *destination_bytes = destination;
    const unsigned char *source_bytes = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        destination_bytes[index] = source_bytes[index];
    }
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

static enum micros_grant_error map_object_error(
    enum micros_kernel_object_error error
)
{
    if (error == MICROS_KERNEL_OBJECT_ERROR_STALE) {
        return MICROS_GRANT_ERROR_DEAD_ENDPOINT;
    }
    if (error == MICROS_KERNEL_OBJECT_ERROR_ARGUMENT) {
        return MICROS_GRANT_ERROR_ARGUMENT;
    }
    return MICROS_GRANT_ERROR_INVARIANT;
}

static enum micros_grant_error map_endpoint_error(
    enum micros_endpoint_error error
)
{
    switch (error) {
    case MICROS_ENDPOINT_OK:
        return MICROS_GRANT_OK;
    case MICROS_ENDPOINT_ERROR_ARGUMENT:
    case MICROS_ENDPOINT_ERROR_ENDPOINT:
        return MICROS_GRANT_ERROR_ARGUMENT;
    case MICROS_ENDPOINT_ERROR_STALE:
        return MICROS_GRANT_ERROR_DEAD_ENDPOINT;
    case MICROS_ENDPOINT_ERROR_STATE:
        return MICROS_GRANT_ERROR_STATE;
    case MICROS_ENDPOINT_ERROR_UNAUTHORIZED:
        return MICROS_GRANT_ERROR_UNAUTHORIZED;
    default:
        return MICROS_GRANT_ERROR_INVARIANT;
    }
}

static bool permissions_are_valid(uint32_t permissions)
{
    return (
        permissions != 0
        && (
            permissions & ~MICROS_GRANT_PERMISSION_DEFINED_MASK
        ) == 0
    );
}

static bool range_is_valid(uintptr_t base, size_t length)
{
    return (
        base >= MICROS_USER_VIRTUAL_BASE
        && base < MICROS_USER_VIRTUAL_END
        && length != 0
        && length <= MICROS_USER_VIRTUAL_END - base
    );
}

static bool authority_is_zero(
    const struct micros_grant_record *record
)
{
    return bytes_are_zero(
        (const unsigned char *)record
            + offsetof(struct micros_grant_record, grantor),
        sizeof(*record)
            - offsetof(struct micros_grant_record, grantor)
    );
}

static enum micros_grant_error resolve_active_process_endpoint(
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *objects,
    struct micros_process_handle process_handle,
    enum micros_grant_error inactive_error,
    const struct micros_process **process,
    const struct micros_endpoint_record **endpoint
)
{
    const struct micros_process *resolved_process;
    const struct micros_endpoint_record *resolved_endpoint;
    enum micros_kernel_object_error object_error;
    enum micros_endpoint_error endpoint_error;

    object_error = micros_process_resolve(
        objects,
        process_handle,
        &resolved_process
    );
    if (object_error != MICROS_KERNEL_OBJECT_OK) {
        return map_object_error(object_error);
    }
    if (
        resolved_process->primary_endpoint
            == MICROS_PROCESS_ENDPOINT_NONE
        || resolved_process->privilege_profile == 0
    ) {
        return inactive_error;
    }
    endpoint_error = micros_endpoint_resolve_active(
        endpoint_registry,
        objects,
        resolved_process->primary_endpoint,
        &resolved_endpoint
    );
    if (endpoint_error != MICROS_ENDPOINT_OK) {
        if (endpoint_error == MICROS_ENDPOINT_ERROR_STATE) {
            return inactive_error;
        }
        return map_endpoint_error(endpoint_error);
    }
    if (!process_handles_equal(
        resolved_endpoint->owner,
        process_handle
    )) {
        return MICROS_GRANT_ERROR_INVARIANT;
    }
    if (process != NULL) {
        *process = resolved_process;
    }
    if (endpoint != NULL) {
        *endpoint = resolved_endpoint;
    }
    return MICROS_GRANT_OK;
}

enum micros_grant_error micros_grant_pack(
    size_t slot,
    uint32_t generation,
    micros_grant_t *grant
)
{
    micros_grant_t candidate;

    if (
        grant == NULL
        || slot >= MICROS_GRANT_CAPACITY
        || generation == 0
        || generation > MICROS_GRANT_GENERATION_MAX
    ) {
        return MICROS_GRANT_ERROR_ARGUMENT;
    }
    candidate =
        (generation << MICROS_GRANT_SLOT_BITS)
        | (micros_grant_t)slot;
    if (candidate == MICROS_GRANT_NONE) {
        return MICROS_GRANT_ERROR_ARGUMENT;
    }
    *grant = candidate;
    return MICROS_GRANT_OK;
}

enum micros_grant_error micros_grant_unpack(
    micros_grant_t grant,
    size_t *slot,
    uint32_t *generation
)
{
    size_t decoded_slot;
    uint32_t decoded_generation;

    if (
        grant == MICROS_GRANT_NONE
        || slot == NULL
        || generation == NULL
    ) {
        return MICROS_GRANT_ERROR_ARGUMENT;
    }
    decoded_slot =
        grant & ((UINT32_C(1) << MICROS_GRANT_SLOT_BITS) - 1);
    decoded_generation = grant >> MICROS_GRANT_SLOT_BITS;
    if (
        decoded_slot >= MICROS_GRANT_CAPACITY
        || decoded_generation == 0
        || decoded_generation > MICROS_GRANT_GENERATION_MAX
    ) {
        return MICROS_GRANT_ERROR_ARGUMENT;
    }
    *slot = decoded_slot;
    *generation = decoded_generation;
    return MICROS_GRANT_OK;
}

enum micros_grant_error micros_grant_registry_initialize(
    struct micros_grant_registry *registry
)
{
    size_t index;

    if (registry == NULL) {
        return MICROS_GRANT_ERROR_ARGUMENT;
    }
    if (
        registry->initialization_magic
            == MICROS_GRANT_REGISTRY_MAGIC
    ) {
        return MICROS_GRANT_ERROR_ALREADY_INITIALIZED;
    }
    if (!bytes_are_zero(registry, sizeof(*registry))) {
        return MICROS_GRANT_ERROR_STATE;
    }
    registry->initialization_magic =
        MICROS_GRANT_REGISTRY_MAGIC;
    for (index = 0; index < MICROS_GRANT_CAPACITY; ++index) {
        registry->grants[index].state = MICROS_GRANT_SLOT_FREE;
        registry->grants[index].generation = 1;
    }
    return MICROS_GRANT_OK;
}

static enum micros_grant_error validate_record_storage(
    const struct micros_grant_record *record,
    size_t slot
)
{
    micros_grant_t grant;

    if (
        record->generation == 0
        || record->generation > MICROS_GRANT_GENERATION_MAX
    ) {
        return MICROS_GRANT_ERROR_INVARIANT;
    }
    if (record->state == MICROS_GRANT_SLOT_FREE) {
        return (
            authority_is_zero(record)
            && micros_grant_pack(
                slot,
                record->generation,
                &grant
            ) == MICROS_GRANT_OK
        ) ? MICROS_GRANT_OK : MICROS_GRANT_ERROR_INVARIANT;
    }
    if (record->state == MICROS_GRANT_SLOT_QUARANTINED) {
        uint32_t next_generation = record->generation + 1;
        bool next_is_packable = (
            record->generation < MICROS_GRANT_GENERATION_MAX
            && micros_grant_pack(
                slot,
                next_generation,
                &grant
            ) == MICROS_GRANT_OK
        );

        return (
            authority_is_zero(record)
            && !next_is_packable
        ) ? MICROS_GRANT_OK : MICROS_GRANT_ERROR_INVARIANT;
    }
    if (
        record->state != MICROS_GRANT_SLOT_ACTIVE
        || micros_grant_pack(slot, record->generation, &grant)
            != MICROS_GRANT_OK
        || !permissions_are_valid(record->permissions)
        || !range_is_valid(record->base, record->length)
    ) {
        return MICROS_GRANT_ERROR_INVARIANT;
    }
    return MICROS_GRANT_OK;
}

static enum micros_grant_error validate_registry_storage(
    const struct micros_grant_registry *registry
)
{
    size_t active_count = 0;
    size_t index;

    if (registry == NULL) {
        return MICROS_GRANT_ERROR_ARGUMENT;
    }
    if (
        registry->initialization_magic
            != MICROS_GRANT_REGISTRY_MAGIC
    ) {
        return MICROS_GRANT_ERROR_NOT_INITIALIZED;
    }
    for (index = 0; index < MICROS_GRANT_CAPACITY; ++index) {
        enum micros_grant_error error =
            validate_record_storage(
                &registry->grants[index],
                index
            );

        if (error != MICROS_GRANT_OK) {
            return error;
        }
        if (
            registry->grants[index].state
                == MICROS_GRANT_SLOT_ACTIVE
        ) {
            ++active_count;
        }
    }
    return active_count == registry->active_count
        ? MICROS_GRANT_OK
        : MICROS_GRANT_ERROR_INVARIANT;
}

enum micros_grant_error micros_grant_registry_validate(
    const struct micros_grant_registry *registry,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *objects
)
{
    enum micros_grant_error error;
    size_t index;

    if (endpoint_registry == NULL || objects == NULL) {
        return MICROS_GRANT_ERROR_ARGUMENT;
    }
    error = validate_registry_storage(registry);
    if (error != MICROS_GRANT_OK) {
        return error;
    }
    if (
        micros_endpoint_registry_validate_objects(
            endpoint_registry,
            objects
        ) != MICROS_ENDPOINT_OK
    ) {
        return MICROS_GRANT_ERROR_INVARIANT;
    }
    for (index = 0; index < MICROS_GRANT_CAPACITY; ++index) {
        const struct micros_grant_record *record =
            &registry->grants[index];
        const struct micros_process *grantor_process;
        const struct micros_endpoint_record *grantor_endpoint;
        const struct micros_endpoint_record *grantee_endpoint;

        if (record->state != MICROS_GRANT_SLOT_ACTIVE) {
            continue;
        }
        error = resolve_active_process_endpoint(
            endpoint_registry,
            objects,
            record->grantor,
            MICROS_GRANT_ERROR_STATE,
            &grantor_process,
            &grantor_endpoint
        );
        if (
            error != MICROS_GRANT_OK
            || grantor_process->primary_endpoint
                != record->grantor_endpoint
            || grantor_endpoint->value
                != record->grantor_endpoint
            || micros_endpoint_resolve_active(
                endpoint_registry,
                objects,
                record->grantee_endpoint,
                &grantee_endpoint
            ) != MICROS_ENDPOINT_OK
            || grantee_endpoint->value
                == record->grantor_endpoint
        ) {
            return MICROS_GRANT_ERROR_INVARIANT;
        }
    }
    return MICROS_GRANT_OK;
}

static void clear_record_authority(
    struct micros_grant_record *record
)
{
    uint32_t generation = record->generation;

    clear_bytes(record, sizeof(*record));
    record->generation = generation;
}

static void retire_record(
    struct micros_grant_record *record,
    size_t slot
)
{
    micros_grant_t grant;
    uint32_t next_generation = record->generation + 1;
    bool next_is_packable = (
        record->generation < MICROS_GRANT_GENERATION_MAX
        && micros_grant_pack(
            slot,
            next_generation,
            &grant
        ) == MICROS_GRANT_OK
    );

    clear_record_authority(record);
    if (next_is_packable) {
        record->state = MICROS_GRANT_SLOT_FREE;
        record->generation = next_generation;
    } else {
        record->state = MICROS_GRANT_SLOT_QUARANTINED;
    }
}

enum micros_grant_error micros_grant_create(
    struct micros_grant_registry *registry,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *objects,
    struct micros_process_handle grantor,
    micros_endpoint_t grantee,
    uintptr_t base,
    size_t length,
    uint32_t permissions,
    micros_grant_t *grant
)
{
    const struct micros_process *grantor_process;
    const struct micros_endpoint_record *grantor_endpoint;
    const struct micros_endpoint_record *grantee_endpoint;
    micros_grant_t candidate;
    enum micros_grant_error error;
    size_t slot;

    if (
        registry == NULL
        || endpoint_registry == NULL
        || objects == NULL
        || grant == NULL
        || grantee == MICROS_ENDPOINT_NONE
        || grantee == MICROS_ENDPOINT_ANY
        || !permissions_are_valid(permissions)
    ) {
        return MICROS_GRANT_ERROR_ARGUMENT;
    }
    if (!range_is_valid(base, length)) {
        return MICROS_GRANT_ERROR_RANGE;
    }
    error = micros_grant_registry_validate(
        registry,
        endpoint_registry,
        objects
    );
    if (error != MICROS_GRANT_OK) {
        return error;
    }
    error = resolve_active_process_endpoint(
        endpoint_registry,
        objects,
        grantor,
        MICROS_GRANT_ERROR_STATE,
        &grantor_process,
        &grantor_endpoint
    );
    if (error != MICROS_GRANT_OK) {
        return error;
    }
    error = map_endpoint_error(
        micros_endpoint_resolve_active(
            endpoint_registry,
            objects,
            grantee,
            &grantee_endpoint
        )
    );
    if (error != MICROS_GRANT_OK) {
        return error;
    }
    if (grantee_endpoint->value == grantor_endpoint->value) {
        return MICROS_GRANT_ERROR_UNAUTHORIZED;
    }
    for (slot = 0; slot < MICROS_GRANT_CAPACITY; ++slot) {
        if (
            registry->grants[slot].state
                == MICROS_GRANT_SLOT_FREE
        ) {
            break;
        }
    }
    if (slot == MICROS_GRANT_CAPACITY) {
        return MICROS_GRANT_ERROR_CAPACITY;
    }
    if (
        micros_grant_pack(
            slot,
            registry->grants[slot].generation,
            &candidate
        ) != MICROS_GRANT_OK
    ) {
        return MICROS_GRANT_ERROR_INVARIANT;
    }
    registry->grants[slot] = (struct micros_grant_record){
        .state = MICROS_GRANT_SLOT_ACTIVE,
        .generation = registry->grants[slot].generation,
        .grantor = grantor,
        .grantor_endpoint = grantor_process->primary_endpoint,
        .grantee_endpoint = grantee,
        .base = base,
        .length = length,
        .permissions = permissions,
    };
    ++registry->active_count;
    *grant = candidate;
    return MICROS_GRANT_OK;
}

static enum micros_grant_error resolve_owned_record(
    const struct micros_grant_registry *registry,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *objects,
    struct micros_process_handle grantor,
    micros_grant_t grant,
    size_t *slot,
    const struct micros_grant_record **record
)
{
    const struct micros_process *grantor_process;
    const struct micros_endpoint_record *grantor_endpoint;
    uint32_t generation;
    enum micros_grant_error error;

    error = micros_grant_unpack(grant, slot, &generation);
    if (error != MICROS_GRANT_OK) {
        return error;
    }
    error = micros_grant_registry_validate(
        registry,
        endpoint_registry,
        objects
    );
    if (error != MICROS_GRANT_OK) {
        return error;
    }
    error = resolve_active_process_endpoint(
        endpoint_registry,
        objects,
        grantor,
        MICROS_GRANT_ERROR_DEAD_ENDPOINT,
        &grantor_process,
        &grantor_endpoint
    );
    if (error != MICROS_GRANT_OK) {
        return error;
    }
    *record = &registry->grants[*slot];
    if (
        (*record)->state != MICROS_GRANT_SLOT_ACTIVE
        || (*record)->generation != generation
    ) {
        return MICROS_GRANT_ERROR_STALE_GRANT;
    }
    if (
        !process_handles_equal((*record)->grantor, grantor)
        || (*record)->grantor_endpoint
            != grantor_process->primary_endpoint
        || (*record)->grantor_endpoint
            != grantor_endpoint->value
    ) {
        return MICROS_GRANT_ERROR_UNAUTHORIZED;
    }
    return MICROS_GRANT_OK;
}

enum micros_grant_error micros_grant_inspect(
    const struct micros_grant_registry *registry,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *objects,
    struct micros_process_handle grantor,
    micros_grant_t grant,
    struct micros_grant_record *record
)
{
    const struct micros_grant_record *resolved;
    enum micros_grant_error error;
    size_t slot;

    if (record == NULL) {
        return MICROS_GRANT_ERROR_ARGUMENT;
    }
    error = resolve_owned_record(
        registry,
        endpoint_registry,
        objects,
        grantor,
        grant,
        &slot,
        &resolved
    );
    if (error != MICROS_GRANT_OK) {
        return error;
    }
    (void)slot;
    copy_bytes(record, resolved, sizeof(*record));
    return MICROS_GRANT_OK;
}

enum micros_grant_error micros_grant_revoke(
    struct micros_grant_registry *registry,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *objects,
    struct micros_process_handle grantor,
    micros_grant_t grant
)
{
    const struct micros_grant_record *resolved;
    enum micros_grant_error error;
    size_t slot;

    error = resolve_owned_record(
        registry,
        endpoint_registry,
        objects,
        grantor,
        grant,
        &slot,
        &resolved
    );
    if (error != MICROS_GRANT_OK) {
        return error;
    }
    (void)resolved;
    retire_record(&registry->grants[slot], slot);
    --registry->active_count;
    return MICROS_GRANT_OK;
}

enum micros_grant_error micros_grant_prepare_endpoint_cancel(
    const struct micros_grant_registry *registry,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint,
    struct micros_grant_cancel_plan *plan
)
{
    const struct micros_endpoint_record *resolved_endpoint;
    struct micros_grant_cancel_plan candidate;
    enum micros_endpoint_error endpoint_error;
    enum micros_grant_error error;
    size_t index;

    if (
        registry == NULL
        || endpoint_registry == NULL
        || objects == NULL
        || plan == NULL
        || endpoint == MICROS_ENDPOINT_NONE
        || endpoint == MICROS_ENDPOINT_ANY
    ) {
        return MICROS_GRANT_ERROR_ARGUMENT;
    }
    error = micros_grant_registry_validate(
        registry,
        endpoint_registry,
        objects
    );
    if (error != MICROS_GRANT_OK) {
        return error;
    }
    endpoint_error = micros_endpoint_resolve_active(
        endpoint_registry,
        objects,
        endpoint,
        &resolved_endpoint
    );
    if (endpoint_error != MICROS_ENDPOINT_OK) {
        return map_endpoint_error(endpoint_error);
    }
    clear_bytes(&candidate, sizeof(candidate));
    candidate.active = true;
    candidate.registry_identity = (uintptr_t)registry;
    candidate.endpoint = resolved_endpoint->value;
    for (index = 0; index < MICROS_GRANT_CAPACITY; ++index) {
        const struct micros_grant_record *record =
            &registry->grants[index];

        if (
            record->state == MICROS_GRANT_SLOT_ACTIVE
            && (
                record->grantor_endpoint == endpoint
                || record->grantee_endpoint == endpoint
            )
        ) {
            candidate.selected_slots |= UINT64_C(1) << index;
            candidate.expected_generation[index] =
                record->generation;
        }
    }
    copy_bytes(plan, &candidate, sizeof(*plan));
    return MICROS_GRANT_OK;
}

enum micros_grant_error micros_grant_commit_endpoint_cancel(
    struct micros_grant_registry *registry,
    struct micros_grant_cancel_plan *plan
)
{
    struct micros_process_handle endpoint_owner;
    size_t index;
    size_t selected_count = 0;

    if (registry == NULL || plan == NULL) {
        return MICROS_GRANT_ERROR_ARGUMENT;
    }
    if (
        !plan->active
        || plan->registry_identity != (uintptr_t)registry
    ) {
        return MICROS_GRANT_ERROR_STATE;
    }
    if (
        plan->endpoint == MICROS_ENDPOINT_NONE
        || plan->endpoint == MICROS_ENDPOINT_ANY
        || micros_endpoint_unpack(
            plan->endpoint,
            &endpoint_owner
        ) != MICROS_ENDPOINT_OK
    ) {
        return MICROS_GRANT_ERROR_INVARIANT;
    }
    if (
        validate_registry_storage(registry)
            != MICROS_GRANT_OK
    ) {
        return MICROS_GRANT_ERROR_INVARIANT;
    }
    for (index = 0; index < MICROS_GRANT_CAPACITY; ++index) {
        const struct micros_grant_record *record =
            &registry->grants[index];
        bool selected =
            (
                plan->selected_slots
                & (UINT64_C(1) << index)
            ) != 0;
        bool references_endpoint = (
            record->state == MICROS_GRANT_SLOT_ACTIVE
            && (
                record->grantor_endpoint == plan->endpoint
                || record->grantee_endpoint == plan->endpoint
            )
        );

        if (selected != references_endpoint) {
            return MICROS_GRANT_ERROR_INVARIANT;
        }
        if (
            selected
            && record->generation
                != plan->expected_generation[index]
        ) {
            return MICROS_GRANT_ERROR_INVARIANT;
        }
        if (
            !selected
            && plan->expected_generation[index] != 0
        ) {
            return MICROS_GRANT_ERROR_INVARIANT;
        }
        if (selected) {
            ++selected_count;
        }
    }
    plan->active = false;
    for (index = 0; index < MICROS_GRANT_CAPACITY; ++index) {
        if (
            (
                plan->selected_slots
                & (UINT64_C(1) << index)
            ) != 0
        ) {
            retire_record(&registry->grants[index], index);
        }
    }
    registry->active_count -= selected_count;
    return MICROS_GRANT_OK;
}

enum micros_grant_error micros_grant_prepare_copy_authority(
    const struct micros_grant_registry *grant_registry,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *objects,
    struct micros_process_handle grantee,
    micros_endpoint_t grantor_endpoint,
    micros_grant_t grant,
    size_t grant_offset,
    size_t length,
    uint32_t required_permission,
    struct micros_grant_copy_authority *authority
)
{
    const struct micros_process *grantee_process;
    const struct micros_endpoint_record *grantee_record;
    const struct micros_endpoint_record *grantor_record;
    const struct micros_grant_record *record;
    size_t slot;
    uint32_t generation;
    enum micros_grant_error error;

    if (
        grant_registry == NULL
        || endpoint_registry == NULL
        || objects == NULL
        || authority == NULL
        || (
            required_permission != MICROS_GRANT_PERMISSION_READ
            && required_permission != MICROS_GRANT_PERMISSION_WRITE
        )
        || grantor_endpoint == MICROS_ENDPOINT_NONE
        || grantor_endpoint == MICROS_ENDPOINT_ANY
    ) {
        return MICROS_GRANT_ERROR_ARGUMENT;
    }
    if (
        length > MICROS_GRANT_COPY_MAX
        || SIZE_MAX - grant_offset < length
    ) {
        return MICROS_GRANT_ERROR_RANGE;
    }
    error = micros_grant_unpack(grant, &slot, &generation);
    if (error != MICROS_GRANT_OK) {
        return error;
    }
    error = micros_grant_registry_validate(
        grant_registry,
        endpoint_registry,
        objects
    );
    if (error != MICROS_GRANT_OK) {
        return error == MICROS_GRANT_ERROR_NOT_INITIALIZED
            ? MICROS_GRANT_ERROR_INVARIANT
            : error;
    }
    error = resolve_active_process_endpoint(
        endpoint_registry,
        objects,
        grantee,
        MICROS_GRANT_ERROR_DEAD_ENDPOINT,
        &grantee_process,
        &grantee_record
    );
    if (error != MICROS_GRANT_OK) {
        return error;
    }
    record = &grant_registry->grants[slot];
    if (
        record->state != MICROS_GRANT_SLOT_ACTIVE
        || record->generation != generation
    ) {
        return MICROS_GRANT_ERROR_STALE_GRANT;
    }
    if (record->grantee_endpoint != grantee_process->primary_endpoint) {
        return MICROS_GRANT_ERROR_UNAUTHORIZED;
    }
    error = map_endpoint_error(
        micros_endpoint_resolve_active(
            endpoint_registry,
            objects,
            grantor_endpoint,
            &grantor_record
        )
    );
    if (error != MICROS_GRANT_OK) {
        return error;
    }
    if (
        record->grantor_endpoint != grantor_record->value
        || !process_handles_equal(
            record->grantor,
            grantor_record->owner
        )
    ) {
        return MICROS_GRANT_ERROR_UNAUTHORIZED;
    }
    if (
        (record->permissions & required_permission)
            != required_permission
    ) {
        return MICROS_GRANT_ERROR_UNAUTHORIZED;
    }
    if (grant_offset + length > record->length) {
        return MICROS_GRANT_ERROR_RANGE;
    }
    if (UINTPTR_MAX - record->base < grant_offset) {
        return MICROS_GRANT_ERROR_RANGE;
    }
    *authority = (struct micros_grant_copy_authority){
        .grantor = record->grantor,
        .grantee = grantee,
        .remote_address = record->base + grant_offset,
        .length = length,
        .required_permission = required_permission,
    };
    (void)grantee_record;
    return MICROS_GRANT_OK;
}

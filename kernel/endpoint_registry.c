#include "endpoint_private.h"

enum micros_endpoint_error micros_endpoint_registry_initialize(
    struct micros_endpoint_registry *registry,
    const struct micros_privilege_profile *profiles,
    size_t profile_count
)
{
    struct micros_privilege_profile
        candidate_profiles[MICROS_PRIVILEGE_PROFILE_CAPACITY];
    uint32_t installed = 0;
    size_t source_index;
    size_t other_index;

    if (registry == NULL || profiles == NULL) {
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
    if (!endpoint_storage_is_zero(registry)) {
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

    endpoint_clear_bytes(candidate_profiles, sizeof(candidate_profiles));
    for (source_index = 0; source_index < profile_count; ++source_index) {
        struct micros_privilege_profile canonical;
        enum micros_endpoint_error error =
            endpoint_canonicalize_profile(
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
                && endpoint_names_equal(
                    canonical.name,
                    candidate_profiles[other_id].name
                )
            ) {
                return MICROS_ENDPOINT_ERROR_PROFILE;
            }
        }
        candidate_profiles[canonical.id] = canonical;
        installed |= UINT32_C(1) << canonical.id;
    }
    for (source_index = 1;
        source_index < MICROS_PRIVILEGE_PROFILE_CAPACITY;
        ++source_index) {
        const struct micros_privilege_profile *profile =
            &candidate_profiles[source_index];
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

    endpoint_copy_bytes(
        registry->profiles,
        candidate_profiles,
        sizeof(candidate_profiles)
    );
    registry->profile_count = profile_count;
    registry->initialization_magic =
        MICROS_ENDPOINT_REGISTRY_MAGIC;
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

    error = endpoint_require_initialized(registry);
    if (error != MICROS_ENDPOINT_OK) {
        return error;
    }
    if (
        registry->profile_count == 0
        || registry->profile_count
            >= MICROS_PRIVILEGE_PROFILE_CAPACITY
        || !endpoint_profile_is_zero(&registry->profiles[0])
    ) {
        return MICROS_ENDPOINT_ERROR_INVARIANT;
    }
    for (index = 1; index < MICROS_PRIVILEGE_PROFILE_CAPACITY; ++index) {
        const struct micros_privilege_profile *profile =
            &registry->profiles[index];
        struct micros_privilege_profile canonical;

        if (profile->id == 0) {
            if (!endpoint_profile_is_zero(profile)) {
                return MICROS_ENDPOINT_ERROR_INVARIANT;
            }
            continue;
        }
        if (
            profile->id != index
            || endpoint_canonicalize_profile(profile, &canonical)
                != MICROS_ENDPOINT_OK
            || !endpoint_bytes_equal(profile, &canonical, sizeof(canonical))
        ) {
            return MICROS_ENDPOINT_ERROR_INVARIANT;
        }
        for (other_index = 1; other_index < index; ++other_index) {
            const struct micros_privilege_profile *other =
                &registry->profiles[other_index];

            if (
                other->id != 0
                && endpoint_names_equal(profile->name, other->name)
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
            && endpoint->state != MICROS_ENDPOINT_STATE_SOURCE_ONLY
        ) {
            return MICROS_ENDPOINT_ERROR_INVARIANT;
        }
        if (
            endpoint->state == MICROS_ENDPOINT_STATE_RESERVED
            && !endpoint_ipc_state_is_zero(endpoint)
        ) {
            return MICROS_ENDPOINT_ERROR_INVARIANT;
        }
        if (
            (
                endpoint->state == MICROS_ENDPOINT_STATE_ACTIVE
                || endpoint->state
                    == MICROS_ENDPOINT_STATE_SOURCE_ONLY
            )
            && (
                !endpoint_queue_pair_is_shallow_valid(
                    endpoint->sender_head,
                    endpoint->sender_tail
                )
                || !endpoint_queue_pair_is_shallow_valid(
                    endpoint->receiver_head,
                    endpoint->receiver_tail
                )
                || !endpoint_pending_notification_state_is_shallow_valid(
                    endpoint
                )
            )
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

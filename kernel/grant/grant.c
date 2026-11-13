#include "micros/grant.h"
#include "micros/grant_copy.h"

#include <stddef.h>
#include <stdint.h>

#include "kernel/endpoint/endpoint_internal.h"
#include "kernel/grant/grant_internal.h"
#include "kernel/scheduler/scheduler_core_internal.h"
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
    case MICROS_ENDPOINT_ERROR_CLOSING:
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

static size_t grant_slot(micros_grant_t grant)
{
    return grant & ((UINT32_C(1) << MICROS_GRANT_SLOT_BITS) - 1);
}

static bool link_token_has_shape(micros_grant_t token)
{
    size_t slot;
    uint32_t generation;

    return token == MICROS_GRANT_NONE || micros_grant_unpack(token, &slot, &generation) == MICROS_GRANT_OK;
}

static bool links_are_null(const struct micros_grant_link *link)
{
    return link->previous == MICROS_GRANT_NONE && link->next == MICROS_GRANT_NONE;
}

static bool links_have_shape(const struct micros_grant_link *link, micros_grant_t self)
{
    return link_token_has_shape(link->previous) && link_token_has_shape(link->next)
        && link->previous != self && link->next != self
        && (link->previous == MICROS_GRANT_NONE || link->previous != link->next);
}

static void clear_record_links(struct micros_grant_record *record)
{
    record->free_next = MICROS_GRANT_NONE;
    record->grantor_link = (struct micros_grant_link){MICROS_GRANT_NONE, MICROS_GRANT_NONE};
    record->grantee_link = (struct micros_grant_link){MICROS_GRANT_NONE, MICROS_GRANT_NONE};
}

static enum micros_grant_error preflight_grant_metadata(const struct micros_grant_registry *registry)
{
    if (registry == NULL) {
        return MICROS_GRANT_ERROR_ARGUMENT;
    }
    if (registry->initialization_magic != MICROS_GRANT_REGISTRY_MAGIC) {
        return MICROS_GRANT_ERROR_NOT_INITIALIZED;
    }
    if (registry->active_count > MICROS_GRANT_CAPACITY || !link_token_has_shape(registry->free_head)) {
        return MICROS_GRANT_ERROR_INVARIANT;
    }
    return MICROS_GRANT_OK;
}

enum micros_grant_error micros_grant_preflight_context(
    const struct micros_grant_registry *registry,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *objects
)
{
    enum micros_grant_error error;

    if (registry == NULL || endpoint_registry == NULL || objects == NULL) {
        return MICROS_GRANT_ERROR_ARGUMENT;
    }
    error = preflight_grant_metadata(registry);
    if (error != MICROS_GRANT_OK) {
        return error;
    }
    if (endpoint_preflight_registry(endpoint_registry) != MICROS_ENDPOINT_OK
        || scheduler_core_preflight_registry(objects) != MICROS_KERNEL_OBJECT_OK) {
        return MICROS_GRANT_ERROR_INVARIANT;
    }
    return MICROS_GRANT_OK;
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
    struct micros_endpoint_selection selected;
    enum micros_endpoint_error endpoint_error;

    endpoint_error = endpoint_preflight_process(endpoint_registry, objects, process_handle, &resolved_process);
    if (endpoint_error != MICROS_ENDPOINT_OK) {
        return map_endpoint_error(endpoint_error);
    }
    if (resolved_process->primary_endpoint == MICROS_PROCESS_ENDPOINT_NONE) {
        return resolved_process->privilege_profile == 0 ? inactive_error : MICROS_GRANT_ERROR_INVARIANT;
    }
    if (micros_endpoint_select(
        endpoint_registry, objects, resolved_process->primary_endpoint, &selected
    ) != MICROS_ENDPOINT_OK || selected.process != resolved_process
        || !process_handles_equal(selected.record->owner, process_handle)) {
        return MICROS_GRANT_ERROR_INVARIANT;
    }
    if (selected.record->state != MICROS_ENDPOINT_STATE_ACTIVE) {
        return selected.record->state == MICROS_ENDPOINT_STATE_SOURCE_ONLY
            ? MICROS_GRANT_ERROR_STATE : inactive_error;
    }
    if (process != NULL) {
        *process = resolved_process;
    }
    if (endpoint != NULL) {
        *endpoint = selected.record;
    }
    return MICROS_GRANT_OK;
}

static enum micros_grant_error resolve_selected_active_participant(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    struct micros_process_handle handle,
    enum micros_grant_error inactive_error,
    const struct micros_endpoint_selection *selected,
    const struct micros_process **process,
    const struct micros_endpoint_record **endpoint
)
{
    if (selected->record == NULL || !process_handles_equal(selected->record->owner, handle)) {
        return resolve_active_process_endpoint(registry, objects, handle, inactive_error, process, endpoint);
    }
    if (selected->record->state != MICROS_ENDPOINT_STATE_ACTIVE) {
        return selected->record->state == MICROS_ENDPOINT_STATE_SOURCE_ONLY
            ? MICROS_GRANT_ERROR_STATE : inactive_error;
    }
    *process = selected->process;
    *endpoint = selected->record;
    return MICROS_GRANT_OK;
}

static enum micros_grant_error resolve_live_process_endpoint(
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *objects,
    struct micros_process_handle process_handle,
    const struct micros_process **process,
    const struct micros_endpoint_record **endpoint
)
{
    const struct micros_process *resolved_process;
    const struct micros_endpoint_record *resolved_endpoint;

    if (
        micros_process_resolve(
            objects,
            process_handle,
            &resolved_process
        ) != MICROS_KERNEL_OBJECT_OK
        || resolved_process->primary_endpoint
            == MICROS_PROCESS_ENDPOINT_NONE
        || resolved_process->privilege_profile == 0
        || micros_endpoint_resolve_internal(
            endpoint_registry,
            objects,
            resolved_process->primary_endpoint,
            &resolved_endpoint
        ) != MICROS_ENDPOINT_OK
        || (
            resolved_endpoint->state
                != MICROS_ENDPOINT_STATE_ACTIVE
            && resolved_endpoint->state
                != MICROS_ENDPOINT_STATE_SOURCE_ONLY
        )
        || !process_handles_equal(
            resolved_endpoint->owner,
            process_handle
        )
    ) {
        return MICROS_GRANT_ERROR_INVARIANT;
    }
    *process = resolved_process;
    *endpoint = resolved_endpoint;
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
    registry->free_head = UINT32_C(1) << MICROS_GRANT_SLOT_BITS;
    for (index = 0; index < MICROS_GRANT_CAPACITY; ++index) {
        registry->grants[index].state = MICROS_GRANT_SLOT_FREE;
        registry->grants[index].generation = 1;
        clear_record_links(&registry->grants[index]);
        registry->grants[index].free_next = index + 1 == MICROS_GRANT_CAPACITY ? MICROS_GRANT_NONE
            : (UINT32_C(1) << MICROS_GRANT_SLOT_BITS) | (uint32_t)(index + 1);
    }
    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        registry->participants[index].endpoint = MICROS_ENDPOINT_NONE;
        registry->participants[index].head = MICROS_GRANT_NONE;
    }
    return MICROS_GRANT_OK;
}

static enum micros_grant_error validate_record_storage(
    const struct micros_grant_record *record,
    size_t slot
)
{
    micros_grant_t grant;
    struct micros_process_handle grantor;
    struct micros_process_handle grantee;

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
            && link_token_has_shape(record->free_next)
            && record->free_next != grant
            && links_are_null(&record->grantor_link)
            && links_are_null(&record->grantee_link)
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
            && record->free_next == MICROS_GRANT_NONE
            && links_are_null(&record->grantor_link)
            && links_are_null(&record->grantee_link)
        ) ? MICROS_GRANT_OK : MICROS_GRANT_ERROR_INVARIANT;
    }
    if (
        record->state != MICROS_GRANT_SLOT_ACTIVE
        || micros_grant_pack(slot, record->generation, &grant)
            != MICROS_GRANT_OK
        || !permissions_are_valid(record->permissions)
        || !range_is_valid(record->base, record->length)
        || record->free_next != MICROS_GRANT_NONE
        || !links_have_shape(&record->grantor_link, grant)
        || !links_have_shape(&record->grantee_link, grant)
        || micros_endpoint_unpack(record->grantor_endpoint, &grantor) != MICROS_ENDPOINT_OK
        || micros_endpoint_unpack(record->grantee_endpoint, &grantee) != MICROS_ENDPOINT_OK
        || grantor.slot >= MICROS_PROCESS_CAPACITY || grantee.slot >= MICROS_PROCESS_CAPACITY
        || grantor.slot == grantee.slot
        || !process_handles_equal(grantor, record->grantor)
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

    enum micros_grant_error metadata = preflight_grant_metadata(registry);

    if (metadata != MICROS_GRANT_OK) {
        return metadata;
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

struct grant_member_view {
    const struct micros_grant_record *record;
    const struct micros_grant_link *link;
    bool grantor;
};

_Static_assert(sizeof(struct grant_member_view) <= 128, "grant membership admission is bounded");

static bool select_indexed_record(
    const struct micros_grant_registry *registry, micros_grant_t token,
    enum micros_grant_slot_state state, const struct micros_grant_record **record
)
{
    size_t slot;
    uint32_t generation;
    const struct micros_grant_record *candidate;

    if (micros_grant_unpack(token, &slot, &generation) != MICROS_GRANT_OK) {
        return false;
    }
    candidate = &registry->grants[slot];
    if (candidate->state != state || candidate->generation != generation
        || validate_record_storage(candidate, slot) != MICROS_GRANT_OK
        || (state == MICROS_GRANT_SLOT_ACTIVE && registry->active_count == 0)) {
        return false;
    }
    *record = candidate;
    return true;
}

static bool select_member(
    const struct micros_grant_registry *registry, micros_endpoint_t endpoint,
    micros_grant_t token, struct grant_member_view *view
)
{
    struct grant_member_view candidate;

    if (!select_indexed_record(registry, token, MICROS_GRANT_SLOT_ACTIVE, &candidate.record)
        || ((candidate.record->grantor_endpoint == endpoint) == (candidate.record->grantee_endpoint == endpoint))) {
        return false;
    }
    candidate.grantor = candidate.record->grantor_endpoint == endpoint;
    candidate.link = candidate.grantor ? &candidate.record->grantor_link : &candidate.record->grantee_link;
    *view = candidate;
    return true;
}

static bool select_participant_root(
    const struct micros_grant_registry *registry, micros_endpoint_t endpoint,
    const struct micros_grant_endpoint_index **root
)
{
    struct micros_process_handle owner;
    const struct micros_grant_endpoint_index *candidate;

    if (micros_endpoint_unpack(endpoint, &owner) != MICROS_ENDPOINT_OK || owner.slot >= MICROS_PROCESS_CAPACITY) {
        return false;
    }
    candidate = &registry->participants[owner.slot];
    if (candidate->head == MICROS_GRANT_NONE ? candidate->endpoint != MICROS_ENDPOINT_NONE
        : candidate->endpoint != endpoint || !link_token_has_shape(candidate->head)) {
        return false;
    }
    *root = candidate;
    return true;
}

static bool preflight_insertion(
    const struct micros_grant_registry *registry, micros_endpoint_t endpoint,
    const struct micros_grant_endpoint_index **root
)
{
    struct grant_member_view head;

    return select_participant_root(registry, endpoint, root)
        && ((*root)->head == MICROS_GRANT_NONE
            || (select_member(registry, endpoint, (*root)->head, &head)
                && head.link->previous == MICROS_GRANT_NONE));
}

static bool preflight_unlink(
    const struct micros_grant_registry *registry, micros_endpoint_t endpoint,
    micros_grant_t token, const struct micros_grant_link *link
)
{
    const struct micros_grant_endpoint_index *root;
    struct grant_member_view neighbor;

    if (!select_participant_root(registry, endpoint, &root) || root->head == MICROS_GRANT_NONE
        || ((link->previous == MICROS_GRANT_NONE) != (root->head == token))) {
        return false;
    }
    if (link->previous != MICROS_GRANT_NONE
        && (!select_member(registry, endpoint, link->previous, &neighbor) || neighbor.link->next != token)) {
        return false;
    }
    if (link->next != MICROS_GRANT_NONE
        && (!select_member(registry, endpoint, link->next, &neighbor) || neighbor.link->previous != token)) {
        return false;
    }
    return true;
}

static bool preflight_free_head(const struct micros_grant_registry *registry)
{
    const struct micros_grant_record *head;

    return registry->free_head == MICROS_GRANT_NONE
        || select_indexed_record(registry, registry->free_head, MICROS_GRANT_SLOT_FREE, &head);
}

static bool preflight_record_unlink(
    const struct micros_grant_registry *registry, const struct micros_grant_record *record,
    micros_grant_t token
)
{
    return preflight_unlink(registry, record->grantor_endpoint, token, &record->grantor_link)
        && preflight_unlink(registry, record->grantee_endpoint, token, &record->grantee_link);
}

static bool validate_registry_indexes(const struct micros_grant_registry *registry)
{
    uint64_t free_seen = 0, grantor_seen = 0, grantee_seen = 0;
    micros_grant_t cursor = registry->free_head;

    while (cursor != MICROS_GRANT_NONE) {
        const struct micros_grant_record *record;
        uint64_t bit;

        if (!select_indexed_record(registry, cursor, MICROS_GRANT_SLOT_FREE, &record)) {
            return false;
        }
        bit = UINT64_C(1) << grant_slot(cursor);
        if ((free_seen & bit) != 0) {
            return false;
        }
        free_seen |= bit;
        cursor = record->free_next;
    }
    for (size_t slot = 0; slot < MICROS_PROCESS_CAPACITY; ++slot) {
        const struct micros_grant_endpoint_index *root = &registry->participants[slot];
        struct micros_process_handle owner;
        micros_grant_t previous = MICROS_GRANT_NONE;

        if (root->head == MICROS_GRANT_NONE) {
            if (root->endpoint != MICROS_ENDPOINT_NONE) {
                return false;
            }
            continue;
        }
        if (micros_endpoint_unpack(root->endpoint, &owner) != MICROS_ENDPOINT_OK || owner.slot != slot) {
            return false;
        }
        cursor = root->head;
        while (cursor != MICROS_GRANT_NONE) {
            struct grant_member_view member;
            uint64_t bit;
            uint64_t *seen;

            if (!select_member(registry, root->endpoint, cursor, &member) || member.link->previous != previous) {
                return false;
            }
            bit = UINT64_C(1) << grant_slot(cursor);
            seen = member.grantor ? &grantor_seen : &grantee_seen;
            if ((*seen & bit) != 0) {
                return false;
            }
            *seen |= bit;
            previous = cursor;
            cursor = member.link->next;
        }
    }
    for (size_t slot = 0; slot < MICROS_GRANT_CAPACITY; ++slot) {
        const struct micros_grant_record *record = &registry->grants[slot];
        uint64_t bit = UINT64_C(1) << slot;

        if (((free_seen & bit) != 0) != (record->state == MICROS_GRANT_SLOT_FREE)
            || ((grantor_seen & bit) != 0) != (record->state == MICROS_GRANT_SLOT_ACTIVE)
            || ((grantee_seen & bit) != 0) != (record->state == MICROS_GRANT_SLOT_ACTIVE)) {
            return false;
        }
    }
    return true;
}

struct selected_grant_record {
    const struct micros_grant_record *record;
    struct micros_endpoint_selection grantor;
    struct micros_endpoint_selection grantee;
};

_Static_assert(sizeof(struct selected_grant_record) <= 64, "selected grant storage is bounded");

static bool select_stored_participant(
    const struct micros_endpoint_registry *endpoints,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint,
    const struct micros_endpoint_selection *known,
    struct micros_endpoint_selection *selection
)
{
    if (known != NULL && known->record->value == endpoint) {
        *selection = *known;
        return true;
    }
    return micros_endpoint_select(endpoints, objects, endpoint, selection) == MICROS_ENDPOINT_OK;
}

static enum micros_grant_error preflight_selected_record(
    const struct micros_grant_registry *registry,
    const struct micros_endpoint_registry *endpoints,
    const struct micros_kernel_objects *objects,
    size_t slot,
    const struct micros_endpoint_selection *known,
    struct selected_grant_record *selection
)
{
    struct selected_grant_record candidate = {.record = &registry->grants[slot]};
    enum micros_grant_error error = validate_record_storage(candidate.record, slot);

    if (error != MICROS_GRANT_OK) {
        return error;
    }
    if (candidate.record->state == MICROS_GRANT_SLOT_ACTIVE) {
        if (registry->active_count == 0
            || !select_stored_participant(
                endpoints, objects, candidate.record->grantor_endpoint, known, &candidate.grantor
            )
            || (candidate.grantor.record->state != MICROS_ENDPOINT_STATE_ACTIVE
                && candidate.grantor.record->state != MICROS_ENDPOINT_STATE_SOURCE_ONLY)
            || !process_handles_equal(candidate.grantor.record->owner, candidate.record->grantor)
            || !select_stored_participant(
                endpoints, objects, candidate.record->grantee_endpoint, known, &candidate.grantee
            )
            || (candidate.grantee.record->state != MICROS_ENDPOINT_STATE_ACTIVE
                && candidate.grantee.record->state != MICROS_ENDPOINT_STATE_SOURCE_ONLY)
            || candidate.grantee.record->value == candidate.grantor.record->value) {
            return MICROS_GRANT_ERROR_INVARIANT;
        }
    }
    *selection = candidate;
    return MICROS_GRANT_OK;
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
        error = resolve_live_process_endpoint(
            endpoint_registry,
            objects,
            record->grantor,
            &grantor_process,
            &grantor_endpoint
        );
        if (
            error != MICROS_GRANT_OK
            || grantor_process->primary_endpoint
                != record->grantor_endpoint
            || grantor_endpoint->value
                != record->grantor_endpoint
            || micros_endpoint_resolve_internal(
                endpoint_registry,
                objects,
                record->grantee_endpoint,
                &grantee_endpoint
            ) != MICROS_ENDPOINT_OK
            || (
                grantee_endpoint->state
                    != MICROS_ENDPOINT_STATE_ACTIVE
                && grantee_endpoint->state
                    != MICROS_ENDPOINT_STATE_SOURCE_ONLY
            )
            || grantee_endpoint->value
                == record->grantor_endpoint
        ) {
            return MICROS_GRANT_ERROR_INVARIANT;
        }
    }
    return validate_registry_indexes(registry) ? MICROS_GRANT_OK : MICROS_GRANT_ERROR_INVARIANT;
}

static void clear_record_authority(
    struct micros_grant_record *record
)
{
    uint32_t generation = record->generation;

    clear_bytes(record, sizeof(*record));
    record->generation = generation;
    clear_record_links(record);
}

static struct micros_grant_endpoint_index *participant_root_prevalidated(
    struct micros_grant_registry *registry, micros_endpoint_t endpoint
)
{
    return &registry->participants[endpoint & ((UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS) - 1)];
}

static struct micros_grant_link *member_link_prevalidated(
    struct micros_grant_registry *registry, micros_endpoint_t endpoint, micros_grant_t token
)
{
    struct micros_grant_record *record = &registry->grants[grant_slot(token)];

    return record->grantor_endpoint == endpoint ? &record->grantor_link : &record->grantee_link;
}

static void unlink_member_prevalidated(
    struct micros_grant_registry *registry, micros_endpoint_t endpoint, const struct micros_grant_link *link
)
{
    struct micros_grant_endpoint_index *root = participant_root_prevalidated(registry, endpoint);

    if (link->previous == MICROS_GRANT_NONE) {
        root->head = link->next;
    } else {
        member_link_prevalidated(registry, endpoint, link->previous)->next = link->next;
    }
    if (link->next != MICROS_GRANT_NONE) {
        member_link_prevalidated(registry, endpoint, link->next)->previous = link->previous;
    }
    if (root->head == MICROS_GRANT_NONE) {
        root->endpoint = MICROS_ENDPOINT_NONE;
    }
}

static void insert_member_prevalidated(
    struct micros_grant_registry *registry, micros_endpoint_t endpoint, micros_grant_t token,
    const struct micros_grant_link *link
)
{
    struct micros_grant_endpoint_index *root = participant_root_prevalidated(registry, endpoint);

    if (link->next != MICROS_GRANT_NONE) {
        member_link_prevalidated(registry, endpoint, link->next)->previous = token;
    }
    root->endpoint = endpoint;
    root->head = token;
}

static void retire_record_prevalidated(struct micros_grant_registry *registry, size_t slot)
{
    struct micros_grant_record *record = &registry->grants[slot];
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

    unlink_member_prevalidated(registry, record->grantor_endpoint, &record->grantor_link);
    unlink_member_prevalidated(registry, record->grantee_endpoint, &record->grantee_link);
    clear_record_authority(record);
    if (next_is_packable) {
        record->state = MICROS_GRANT_SLOT_FREE;
        record->generation = next_generation;
        record->free_next = registry->free_head;
        registry->free_head = grant;
    } else {
        record->state = MICROS_GRANT_SLOT_QUARANTINED;
    }
    --registry->active_count;
}

enum micros_grant_error micros_grant_prepare_create(
    const struct micros_grant_registry *registry,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *objects,
    struct micros_process_handle grantor,
    micros_endpoint_t grantee,
    uintptr_t base,
    size_t length,
    uint32_t permissions,
    struct micros_grant_create_plan *plan
)
{
    const struct micros_process *grantor_process;
    const struct micros_endpoint_record *grantor_endpoint;
    const struct micros_endpoint_record *grantee_endpoint;
    const struct micros_grant_record *free_record;
    const struct micros_grant_record *successor;
    const struct micros_grant_endpoint_index *grantor_root;
    const struct micros_grant_endpoint_index *grantee_root;
    struct micros_grant_create_plan candidate_plan;
    micros_grant_t candidate;
    enum micros_grant_error error;
    size_t slot;

    if (
        registry == NULL
        || endpoint_registry == NULL
        || objects == NULL
        || plan == NULL
        || grantee == MICROS_ENDPOINT_NONE
        || grantee == MICROS_ENDPOINT_ANY
        || !permissions_are_valid(permissions)
    ) {
        return MICROS_GRANT_ERROR_ARGUMENT;
    }
    if (!range_is_valid(base, length)) {
        return MICROS_GRANT_ERROR_RANGE;
    }
    error = micros_grant_preflight_context(
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
    candidate = registry->free_head;
    if (candidate == MICROS_GRANT_NONE) {
        return MICROS_GRANT_ERROR_CAPACITY;
    }
    if (!select_indexed_record(registry, candidate, MICROS_GRANT_SLOT_FREE, &free_record)
        || registry->active_count == MICROS_GRANT_CAPACITY
        || (free_record->free_next != MICROS_GRANT_NONE
            && (!select_indexed_record(registry, free_record->free_next, MICROS_GRANT_SLOT_FREE, &successor)
                || successor->free_next == candidate))
        || !preflight_insertion(registry, grantor_endpoint->value, &grantor_root)
        || !preflight_insertion(registry, grantee_endpoint->value, &grantee_root)) {
        return MICROS_GRANT_ERROR_INVARIANT;
    }
    slot = grant_slot(candidate);
    clear_bytes(&candidate_plan, sizeof(candidate_plan));
    candidate_plan.active = true;
    candidate_plan.slot = slot;
    candidate_plan.grant = candidate;
    candidate_plan.free_next = free_record->free_next;
    /* A whole-record assignment would emit an unavailable RV64 memcpy. */
    candidate_plan.record.state = MICROS_GRANT_SLOT_ACTIVE;
    candidate_plan.record.generation = free_record->generation;
    candidate_plan.record.free_next = MICROS_GRANT_NONE;
    candidate_plan.record.grantor_link.previous = MICROS_GRANT_NONE;
    candidate_plan.record.grantor_link.next = grantor_root->head;
    candidate_plan.record.grantee_link.previous = MICROS_GRANT_NONE;
    candidate_plan.record.grantee_link.next = grantee_root->head;
    candidate_plan.record.grantor = grantor;
    candidate_plan.record.grantor_endpoint = grantor_process->primary_endpoint;
    candidate_plan.record.grantee_endpoint = grantee;
    candidate_plan.record.base = base;
    candidate_plan.record.length = length;
    candidate_plan.record.permissions = permissions;
    copy_bytes(plan, &candidate_plan, sizeof(*plan));
    return MICROS_GRANT_OK;
}

void micros_grant_commit_create_prevalidated(
    struct micros_grant_registry *registry,
    struct micros_grant_create_plan *plan,
    micros_grant_t *grant
)
{
    registry->free_head = plan->free_next;
    copy_bytes(
        &registry->grants[plan->slot],
        &plan->record,
        sizeof(plan->record)
    );
    insert_member_prevalidated(registry, plan->record.grantor_endpoint, plan->grant, &plan->record.grantor_link);
    insert_member_prevalidated(registry, plan->record.grantee_endpoint, plan->grant, &plan->record.grantee_link);
    ++registry->active_count;
    *grant = plan->grant;
    clear_bytes(plan, sizeof(*plan));
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
    struct micros_grant_create_plan plan;
    enum micros_grant_error error;

    if (grant == NULL) {
        return MICROS_GRANT_ERROR_ARGUMENT;
    }
    error = micros_grant_prepare_create(
        registry,
        endpoint_registry,
        objects,
        grantor,
        grantee,
        base,
        length,
        permissions,
        &plan
    );

    if (error != MICROS_GRANT_OK) {
        return error;
    }
    micros_grant_commit_create_prevalidated(
        registry,
        &plan,
        grant
    );
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
    struct selected_grant_record selected;
    uint32_t generation;
    enum micros_grant_error error;

    error = micros_grant_unpack(grant, slot, &generation);
    if (error != MICROS_GRANT_OK) {
        return error;
    }
    error = micros_grant_preflight_context(
        registry,
        endpoint_registry,
        objects
    );
    if (error != MICROS_GRANT_OK) {
        return error;
    }
    error = preflight_selected_record(registry, endpoint_registry, objects, *slot, NULL, &selected);
    if (error != MICROS_GRANT_OK) {
        return error;
    }
    error = resolve_selected_active_participant(
        endpoint_registry,
        objects,
        grantor,
        MICROS_GRANT_ERROR_DEAD_ENDPOINT,
        &selected.grantor,
        &grantor_process,
        &grantor_endpoint
    );
    if (error != MICROS_GRANT_OK) {
        return error;
    }
    *record = selected.record;
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
    if (!preflight_record_unlink(registry, resolved, grant) || !preflight_free_head(registry)) {
        return MICROS_GRANT_ERROR_INVARIANT;
    }
    retire_record_prevalidated(registry, slot);
    return MICROS_GRANT_OK;
}

struct grant_incident_walk {
    micros_grant_t current;
    micros_grant_t previous;
    uint64_t seen;
    size_t count;
};

_Static_assert(sizeof(struct grant_incident_walk) <= 128, "grant incident traversal is bounded");

static bool advance_incident_walk(
    const struct micros_grant_registry *registry, micros_endpoint_t endpoint,
    struct grant_incident_walk *walk, struct grant_member_view *member
)
{
    uint64_t bit;

    if (!select_member(registry, endpoint, walk->current, member)
        || member->link->previous != walk->previous) {
        return false;
    }
    bit = UINT64_C(1) << grant_slot(walk->current);
    if ((walk->seen & bit) != 0 || walk->count >= MICROS_GRANT_CAPACITY
        || walk->count >= registry->active_count) {
        return false;
    }
    walk->seen |= bit;
    ++walk->count;
    walk->previous = walk->current;
    walk->current = member->link->next;
    return true;
}

static size_t lowest_selected_slot(uint64_t mask)
{
    size_t slot = 0;

    if ((mask & UINT64_C(0xffffffff)) == 0) {
        slot += 32;
        mask >>= 32;
    }
    if ((mask & UINT64_C(0xffff)) == 0) {
        slot += 16;
        mask >>= 16;
    }
    if ((mask & UINT64_C(0xff)) == 0) {
        slot += 8;
        mask >>= 8;
    }
    if ((mask & UINT64_C(0xf)) == 0) {
        slot += 4;
        mask >>= 4;
    }
    if ((mask & UINT64_C(0x3)) == 0) {
        slot += 2;
        mask >>= 2;
    }
    return slot + ((mask & 1) == 0);
}

static void publish_cancel_plan(
    struct micros_grant_cancel_plan *plan, const struct micros_grant_cancel_plan *candidate
)
{
    uint64_t remaining = candidate->selected_slots;

    plan->registry_identity = candidate->registry_identity;
    plan->endpoint = candidate->endpoint;
    plan->selected_slots = candidate->selected_slots;
    while (remaining != 0) {
        size_t slot = lowest_selected_slot(remaining);

        copy_bytes(&plan->expected_generation[slot], &candidate->expected_generation[slot],
                   sizeof(plan->expected_generation[slot]));
        remaining &= remaining - 1;
    }
    plan->active = true;
}

enum micros_grant_error micros_grant_prepare_endpoint_cancel(
    const struct micros_grant_registry *registry,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint,
    struct micros_grant_cancel_plan *plan
)
{
    struct micros_endpoint_selection closing;
    const struct micros_grant_endpoint_index *root;
    struct grant_incident_walk walk;
    struct micros_grant_cancel_plan candidate;
    enum micros_endpoint_error endpoint_error;
    enum micros_grant_error error;

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
    error = micros_grant_preflight_context(
        registry,
        endpoint_registry,
        objects
    );
    if (error != MICROS_GRANT_OK) {
        return error;
    }
    endpoint_error = micros_endpoint_select(
        endpoint_registry,
        objects,
        endpoint,
        &closing
    );
    if (endpoint_error != MICROS_ENDPOINT_OK) {
        return map_endpoint_error(endpoint_error);
    }
    if (
        closing.record->state != MICROS_ENDPOINT_STATE_ACTIVE
        && closing.record->state
            != MICROS_ENDPOINT_STATE_SOURCE_ONLY
    ) {
        return MICROS_GRANT_ERROR_STATE;
    }
    if (!select_participant_root(registry, endpoint, &root)) {
        return MICROS_GRANT_ERROR_INVARIANT;
    }
    walk = (struct grant_incident_walk){.current = root->head, .previous = MICROS_GRANT_NONE};
    while (walk.current != MICROS_GRANT_NONE) {
        micros_grant_t token = walk.current;
        size_t slot = grant_slot(token);
        struct grant_member_view member;
        struct selected_grant_record selected;

        if (!advance_incident_walk(registry, endpoint, &walk, &member)
            || preflight_selected_record(registry, endpoint_registry, objects, slot, &closing, &selected)
                != MICROS_GRANT_OK
            || !preflight_record_unlink(registry, selected.record, token)) {
            return MICROS_GRANT_ERROR_INVARIANT;
        }
        candidate.expected_generation[slot] = selected.record->generation;
    }
    if (walk.seen != 0 && !preflight_free_head(registry)) {
        return MICROS_GRANT_ERROR_INVARIANT;
    }
    candidate.registry_identity = (uintptr_t)registry;
    candidate.endpoint = endpoint;
    candidate.selected_slots = walk.seen;
    publish_cancel_plan(plan, &candidate);
    return MICROS_GRANT_OK;
}

enum micros_grant_error micros_grant_commit_endpoint_cancel(
    struct micros_grant_registry *registry,
    struct micros_grant_cancel_plan *plan
)
{
    const struct micros_grant_endpoint_index *root;
    struct grant_incident_walk walk;
    uint64_t remaining;

    if (registry == NULL || plan == NULL) {
        return MICROS_GRANT_ERROR_ARGUMENT;
    }
    if (
        !plan->active
        || plan->registry_identity != (uintptr_t)registry
    ) {
        return MICROS_GRANT_ERROR_STATE;
    }
    if (preflight_grant_metadata(registry) != MICROS_GRANT_OK
        || !select_participant_root(registry, plan->endpoint, &root)) {
        return MICROS_GRANT_ERROR_INVARIANT;
    }
    walk = (struct grant_incident_walk){.current = root->head, .previous = MICROS_GRANT_NONE};
    while (walk.current != MICROS_GRANT_NONE) {
        micros_grant_t token = walk.current;
        size_t slot = grant_slot(token);
        struct grant_member_view member;

        if (!advance_incident_walk(registry, plan->endpoint, &walk, &member)
            || (plan->selected_slots & (UINT64_C(1) << slot)) == 0
            || member.record->generation != plan->expected_generation[slot]
            || !preflight_record_unlink(registry, member.record, token)) {
            return MICROS_GRANT_ERROR_INVARIANT;
        }
    }
    if (walk.seen != plan->selected_slots || (walk.seen != 0 && !preflight_free_head(registry))) {
        return MICROS_GRANT_ERROR_INVARIANT;
    }
    plan->active = false;
    remaining = plan->selected_slots;
    while (remaining != 0) {
        retire_record_prevalidated(registry, lowest_selected_slot(remaining));
        remaining &= remaining - 1;
    }
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
    struct selected_grant_record selected;
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
    error = micros_grant_preflight_context(
        grant_registry,
        endpoint_registry,
        objects
    );
    if (error != MICROS_GRANT_OK) {
        return error == MICROS_GRANT_ERROR_NOT_INITIALIZED
            ? MICROS_GRANT_ERROR_INVARIANT
            : error;
    }
    error = preflight_selected_record(grant_registry, endpoint_registry, objects, slot, NULL, &selected);
    if (error != MICROS_GRANT_OK) {
        return error;
    }
    error = resolve_selected_active_participant(
        endpoint_registry,
        objects,
        grantee,
        MICROS_GRANT_ERROR_DEAD_ENDPOINT,
        &selected.grantee,
        &grantee_process,
        &grantee_record
    );
    if (error != MICROS_GRANT_OK) {
        return error;
    }
    record = selected.record;
    if (
        record->state != MICROS_GRANT_SLOT_ACTIVE
        || record->generation != generation
    ) {
        return MICROS_GRANT_ERROR_STALE_GRANT;
    }
    if (record->grantee_endpoint != grantee_process->primary_endpoint) {
        return MICROS_GRANT_ERROR_UNAUTHORIZED;
    }
    if (grantor_endpoint == record->grantor_endpoint) {
        grantor_record = selected.grantor.record;
        error = grantor_record->state == MICROS_ENDPOINT_STATE_ACTIVE
            ? MICROS_GRANT_OK : MICROS_GRANT_ERROR_STATE;
    } else {
        error = map_endpoint_error(micros_endpoint_resolve_active(
            endpoint_registry, objects, grantor_endpoint, &grantor_record
        ));
    }
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

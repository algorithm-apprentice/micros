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
    size_t index;

    if (
        endpoint->state != MICROS_ENDPOINT_STATE_FREE
        || endpoint->owner.slot != 0
        || endpoint->owner.generation != 0
        || endpoint->value != 0
        || endpoint->sender_head.slot != 0
        || endpoint->sender_head.generation != 0
        || endpoint->sender_tail.slot != 0
        || endpoint->sender_tail.generation != 0
        || endpoint->receiver_head.slot != 0
        || endpoint->receiver_head.generation != 0
        || endpoint->receiver_tail.slot != 0
        || endpoint->receiver_tail.generation != 0
        || endpoint->pending_notification_sources != 0
    ) {
        return false;
    }
    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        if (endpoint->pending_events[index] != 0) {
            return false;
        }
    }
    return true;
}

static bool endpoint_ipc_state_is_zero(
    const struct micros_endpoint_record *endpoint
)
{
    size_t index;

    if (
        endpoint->sender_head.slot != 0
        || endpoint->sender_head.generation != 0
        || endpoint->sender_tail.slot != 0
        || endpoint->sender_tail.generation != 0
        || endpoint->receiver_head.slot != 0
        || endpoint->receiver_head.generation != 0
        || endpoint->receiver_tail.slot != 0
        || endpoint->receiver_tail.generation != 0
        || endpoint->pending_notification_sources != 0
    ) {
        return false;
    }
    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        if (endpoint->pending_events[index] != 0) {
            return false;
        }
    }
    return true;
}

static bool thread_handle_is_zero(
    struct micros_thread_handle handle
)
{
    return handle.slot == 0 && handle.generation == 0;
}

static bool thread_handle_is_shallow_valid(
    struct micros_thread_handle handle
)
{
    return (
        handle.slot < MICROS_THREAD_CAPACITY
        && handle.generation != 0
    );
}

static bool queue_pair_is_shallow_valid(
    struct micros_thread_handle head,
    struct micros_thread_handle tail
)
{
    if (thread_handle_is_zero(head)) {
        return thread_handle_is_zero(tail);
    }
    return (
        thread_handle_is_shallow_valid(head)
        && thread_handle_is_shallow_valid(tail)
    );
}

static bool pending_notification_state_is_zero(
    const struct micros_endpoint_record *endpoint
)
{
    size_t index;

    if (endpoint->pending_notification_sources != 0) {
        return false;
    }
    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        if (endpoint->pending_events[index] != 0) {
            return false;
        }
    }
    return true;
}

static void clear_endpoint_record(
    struct micros_endpoint_record *endpoint
)
{
    size_t index;

    endpoint->state = MICROS_ENDPOINT_STATE_FREE;
    endpoint->owner.slot = 0;
    endpoint->owner.generation = 0;
    endpoint->value = 0;
    endpoint->sender_head.slot = 0;
    endpoint->sender_head.generation = 0;
    endpoint->sender_tail.slot = 0;
    endpoint->sender_tail.generation = 0;
    endpoint->receiver_head.slot = 0;
    endpoint->receiver_head.generation = 0;
    endpoint->receiver_tail.slot = 0;
    endpoint->receiver_tail.generation = 0;
    endpoint->pending_notification_sources = 0;
    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        endpoint->pending_events[index] = 0;
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

static bool thread_handles_equal(
    struct micros_thread_handle left,
    struct micros_thread_handle right
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
    struct micros_privilege_profile
        candidate_profiles[MICROS_PRIVILEGE_PROFILE_CAPACITY];
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

    clear_bytes(candidate_profiles, sizeof(candidate_profiles));
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

    copy_bytes(
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
        if (
            endpoint->state == MICROS_ENDPOINT_STATE_RESERVED
            && !endpoint_ipc_state_is_zero(endpoint)
        ) {
            return MICROS_ENDPOINT_ERROR_INVARIANT;
        }
        if (
            endpoint->state == MICROS_ENDPOINT_STATE_ACTIVE
            && (
                !queue_pair_is_shallow_valid(
                    endpoint->sender_head,
                    endpoint->sender_tail
                )
                || !queue_pair_is_shallow_valid(
                    endpoint->receiver_head,
                    endpoint->receiver_tail
                )
                || !pending_notification_state_is_zero(endpoint)
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

static bool delivery_state_is_clear(
    const struct micros_thread *thread
)
{
    return (
        !thread->ipc_delivery_pending
        && bytes_are_zero(
            &thread->ipc_inbound_message,
            sizeof(thread->ipc_inbound_message)
        )
        && thread->ipc_staged_result == MICROS_IPC_OK
    );
}

static bool sender_queue_state_is_valid(
    const struct micros_endpoint_registry *registry,
    const struct micros_thread *thread,
    micros_endpoint_t destination
)
{
    const struct micros_endpoint_record *source =
        &registry->endpoints[thread->owner.slot];
    bool is_call =
        (
            thread->runtime_flags
            & MICROS_THREAD_RTS_IPC_REPLY
        ) != 0;

    if (
        thread->ipc_queue_kind != MICROS_IPC_QUEUE_SENDER
        || (
            thread->runtime_flags
            & MICROS_THREAD_RTS_IPC_SEND
        ) == 0
        || (
            thread->runtime_flags
            & MICROS_THREAD_RTS_IPC_RECEIVE
        ) != 0
        || source->state != MICROS_ENDPOINT_STATE_ACTIVE
        || !process_handles_equal(source->owner, thread->owner)
        || thread->ipc_send_destination != destination
        || thread->ipc_outbound_message.source != source->value
        || (
            thread->ipc_outbound_message.type
            & MICROS_IPC_TYPE_KERNEL_MASK
        ) != 0
        || thread->ipc_receive_source != 0
        || !delivery_state_is_clear(thread)
    ) {
        return false;
    }
    if (!is_call) {
        return (
            thread->ipc_outbound_message.reply_token == 0
            && thread->ipc_receive_buffer == 0
            && thread->ipc_reply_token == 0
            && thread->ipc_reply_callee == 0
        );
    }
    return (
        thread->ipc_reply_token != 0
        && thread->ipc_reply_callee == destination
        && thread->ipc_outbound_message.reply_token
            == thread->ipc_reply_token
        && thread->ipc_receive_buffer != 0
        && thread->ipc_receive_buffer % 8 == 0
    );
}

static bool reply_wait_state_is_valid(
    const struct micros_endpoint_registry *registry,
    const struct micros_thread *thread
)
{
    const struct micros_endpoint_record *caller =
        &registry->endpoints[thread->owner.slot];
    const struct micros_endpoint_record *callee;

    if (
        thread->ipc_queue_kind != MICROS_IPC_QUEUE_NONE
        || !thread_handle_is_zero(thread->ipc_next)
        || (
            thread->runtime_flags
            & MICROS_THREAD_RTS_IPC_MASK
        ) != MICROS_THREAD_RTS_IPC_REPLY
        || !bytes_are_zero(
            &thread->ipc_outbound_message,
            sizeof(thread->ipc_outbound_message)
        )
        || thread->ipc_send_destination != 0
        || thread->ipc_receive_source != 0
        || thread->ipc_receive_buffer == 0
        || thread->ipc_receive_buffer % 8 != 0
        || !delivery_state_is_clear(thread)
        || thread->ipc_reply_token == 0
        || caller->state != MICROS_ENDPOINT_STATE_ACTIVE
        || !process_handles_equal(caller->owner, thread->owner)
        || endpoint_record_resolve_validated(
            registry,
            thread->ipc_reply_callee,
            &callee
        ) != MICROS_ENDPOINT_OK
    ) {
        return false;
    }
    return callee->state == MICROS_ENDPOINT_STATE_ACTIVE;
}

static bool staged_reply_token_binding_is_valid(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    const struct micros_thread *receiver,
    const struct micros_endpoint_record *receiver_endpoint
)
{
    const struct micros_thread *caller = NULL;
    const struct micros_endpoint_record *caller_endpoint;
    uint64_t reply_token =
        receiver->ipc_inbound_message.reply_token;
    size_t index;

    if (reply_token == 0) {
        return true;
    }
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *candidate =
            &objects->threads[index];

        if (
            candidate->slot_state
                != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || candidate->ipc_reply_token != reply_token
        ) {
            continue;
        }
        if (caller != NULL) {
            return false;
        }
        caller = candidate;
    }
    if (
        caller == NULL
        || !reply_wait_state_is_valid(registry, caller)
    ) {
        return false;
    }
    caller_endpoint = &registry->endpoints[caller->owner.slot];
    return (
        caller_endpoint->state == MICROS_ENDPOINT_STATE_ACTIVE
        && process_handles_equal(
            caller_endpoint->owner,
            caller->owner
        )
        && receiver->ipc_inbound_message.source
            == caller_endpoint->value
        && caller->ipc_reply_callee == receiver_endpoint->value
    );
}

static bool staged_delivery_state_is_valid(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    const struct micros_thread *thread
)
{
    const struct micros_endpoint_record *owner =
        &registry->endpoints[thread->owner.slot];
    const struct micros_endpoint_record *source;

    if (
        thread->ipc_queue_kind != MICROS_IPC_QUEUE_NONE
        || !thread_handle_is_zero(thread->ipc_next)
        || (
            thread->runtime_flags
            & MICROS_THREAD_RTS_IPC_MASK
        ) != 0
        || !bytes_are_zero(
            &thread->ipc_outbound_message,
            sizeof(thread->ipc_outbound_message)
        )
        || thread->ipc_send_destination != 0
        || thread->ipc_receive_source != 0
        || thread->ipc_receive_buffer == 0
        || thread->ipc_receive_buffer % 8 != 0
        || !thread->ipc_delivery_pending
        || thread->ipc_staged_result != MICROS_IPC_OK
        || thread->ipc_reply_token != 0
        || thread->ipc_reply_callee != 0
        || (
            thread->ipc_inbound_message.type
            & MICROS_IPC_TYPE_KERNEL_MASK
        ) != 0
        || owner->state != MICROS_ENDPOINT_STATE_ACTIVE
        || !process_handles_equal(owner->owner, thread->owner)
        || endpoint_record_resolve_validated(
            registry,
            thread->ipc_inbound_message.source,
            &source
        ) != MICROS_ENDPOINT_OK
    ) {
        return false;
    }
    return (
        source->state == MICROS_ENDPOINT_STATE_ACTIVE
        && staged_reply_token_binding_is_valid(
            registry,
            objects,
            thread,
            owner
        )
    );
}

static bool thread_has_no_ipc_flags(
    const struct micros_thread *thread
)
{
    return (
        thread->runtime_flags
        & MICROS_THREAD_RTS_IPC_MASK
    ) == 0;
}

static bool receiver_queue_state_is_valid(
    const struct micros_endpoint_registry *registry,
    const struct micros_thread *thread,
    const struct micros_endpoint_record *endpoint
)
{
    const struct micros_endpoint_record *source;

    if (
        thread->ipc_queue_kind != MICROS_IPC_QUEUE_RECEIVER
        || (
            thread->runtime_flags
            & MICROS_THREAD_RTS_IPC_MASK
        ) != MICROS_THREAD_RTS_IPC_RECEIVE
        || !process_handles_equal(thread->owner, endpoint->owner)
        || !bytes_are_zero(
            &thread->ipc_outbound_message,
            sizeof(thread->ipc_outbound_message)
        )
        || thread->ipc_send_destination != 0
        || thread->ipc_receive_buffer == 0
        || thread->ipc_receive_buffer % 8 != 0
        || !delivery_state_is_clear(thread)
        || thread->ipc_reply_token != 0
        || thread->ipc_reply_callee != 0
    ) {
        return false;
    }
    if (thread->ipc_receive_source == MICROS_ENDPOINT_ANY) {
        return true;
    }
    if (
        endpoint_record_resolve_validated(
            registry,
            thread->ipc_receive_source,
            &source
        ) != MICROS_ENDPOINT_OK
    ) {
        return false;
    }
    return source->state == MICROS_ENDPOINT_STATE_ACTIVE;
}

static bool endpoint_queue_is_valid(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_record *endpoint,
    struct micros_thread_handle head,
    struct micros_thread_handle tail,
    enum micros_ipc_queue_kind kind,
    bool seen[MICROS_THREAD_CAPACITY]
)
{
    struct micros_thread_handle current = head;
    size_t steps;

    if (thread_handle_is_zero(head)) {
        return thread_handle_is_zero(tail);
    }
    for (steps = 0; steps < MICROS_THREAD_CAPACITY; ++steps) {
        const struct micros_thread *thread;

        if (
            current.slot >= MICROS_THREAD_CAPACITY
            || current.generation == 0
            || seen[current.slot]
            || micros_thread_resolve(objects, current, &thread)
                != MICROS_KERNEL_OBJECT_OK
        ) {
            return false;
        }
        seen[current.slot] = true;
        if (
            kind == MICROS_IPC_QUEUE_SENDER
                ? !sender_queue_state_is_valid(
                    registry,
                    thread,
                    endpoint->value
                )
                : !receiver_queue_state_is_valid(
                    registry,
                    thread,
                    endpoint
                )
        ) {
            return false;
        }
        if (thread_handles_equal(current, tail)) {
            return thread_handle_is_zero(thread->ipc_next);
        }
        if (thread_handle_is_zero(thread->ipc_next)) {
            return false;
        }
        current = thread->ipc_next;
    }
    return false;
}

static bool endpoint_queues_have_match(
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_record *endpoint
)
{
    struct micros_thread_handle receiver_handle =
        endpoint->receiver_head;
    size_t receiver_steps;

    if (
        thread_handle_is_zero(endpoint->sender_head)
        || thread_handle_is_zero(endpoint->receiver_head)
    ) {
        return false;
    }
    for (
        receiver_steps = 0;
        receiver_steps < MICROS_THREAD_CAPACITY;
        ++receiver_steps
    ) {
        const struct micros_thread *receiver =
            &objects->threads[receiver_handle.slot];
        struct micros_thread_handle sender_handle =
            endpoint->sender_head;
        size_t sender_steps;

        for (
            sender_steps = 0;
            sender_steps < MICROS_THREAD_CAPACITY;
            ++sender_steps
        ) {
            const struct micros_thread *sender =
                &objects->threads[sender_handle.slot];

            if (
                receiver->ipc_receive_source
                    == MICROS_ENDPOINT_ANY
                || receiver->ipc_receive_source
                    == sender->ipc_outbound_message.source
            ) {
                return true;
            }
            if (thread_handles_equal(
                sender_handle,
                endpoint->sender_tail
            )) {
                break;
            }
            sender_handle = sender->ipc_next;
        }
        if (thread_handles_equal(
            receiver_handle,
            endpoint->receiver_tail
        )) {
            break;
        }
        receiver_handle = receiver->ipc_next;
    }
    return false;
}

enum micros_endpoint_error micros_endpoint_registry_validate_objects(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects
)
{
    enum micros_endpoint_error endpoint_error;
    bool queued_threads[MICROS_THREAD_CAPACITY];
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
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        queued_threads[index] = false;
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
        if (
            endpoint->state == MICROS_ENDPOINT_STATE_ACTIVE
            && (
                !endpoint_queue_is_valid(
                    registry,
                    objects,
                    endpoint,
                    endpoint->sender_head,
                    endpoint->sender_tail,
                    MICROS_IPC_QUEUE_SENDER,
                    queued_threads
                )
                || !endpoint_queue_is_valid(
                    registry,
                    objects,
                    endpoint,
                    endpoint->receiver_head,
                    endpoint->receiver_tail,
                    MICROS_IPC_QUEUE_RECEIVER,
                    queued_threads
                )
                || endpoint_queues_have_match(objects, endpoint)
            )
        ) {
            return MICROS_ENDPOINT_ERROR_INVARIANT;
        }
    }
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *thread = &objects->threads[index];
        size_t other_index;

        if (thread->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE) {
            continue;
        }
        if (thread->ipc_queue_kind == MICROS_IPC_QUEUE_NONE) {
            if (
                queued_threads[index]
                || (
                    micros_thread_ipc_state_is_clear(thread)
                        ? !thread_has_no_ipc_flags(thread)
                        : (
                            !reply_wait_state_is_valid(
                                registry,
                                thread
                            )
                            && !staged_delivery_state_is_valid(
                                registry,
                                objects,
                                thread
                            )
                        )
                )
            ) {
                return MICROS_ENDPOINT_ERROR_INVARIANT;
            }
        } else if (
            (
                thread->ipc_queue_kind != MICROS_IPC_QUEUE_SENDER
                && thread->ipc_queue_kind
                    != MICROS_IPC_QUEUE_RECEIVER
            )
            || !queued_threads[index]
        ) {
            return MICROS_ENDPOINT_ERROR_INVARIANT;
        }
        if (thread->ipc_reply_token == 0) {
            continue;
        }
        if (
            registry->last_reply_token == 0
            || thread->ipc_reply_token
                > registry->last_reply_token
        ) {
            return MICROS_ENDPOINT_ERROR_INVARIANT;
        }
        for (other_index = 0; other_index < index; ++other_index) {
            const struct micros_thread *other =
                &objects->threads[other_index];

            if (
                other->slot_state
                    == MICROS_KERNEL_OBJECT_SLOT_LIVE
                && other->ipc_reply_token
                    == thread->ipc_reply_token
            ) {
                return MICROS_ENDPOINT_ERROR_INVARIANT;
            }
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

static bool endpoint_has_foreign_waiters(
    const struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint
)
{
    size_t index;

    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *thread = &objects->threads[index];

        if (thread->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE) {
            continue;
        }
        if (
            (
                thread->ipc_queue_kind
                    == MICROS_IPC_QUEUE_RECEIVER
                && thread->ipc_receive_source == endpoint
            )
            || (
                thread->ipc_reply_token != 0
                && thread->ipc_reply_callee == endpoint
            )
            || (
                thread->ipc_delivery_pending
                && thread->ipc_staged_result == MICROS_IPC_OK
                && thread->ipc_inbound_message.source == endpoint
            )
        ) {
            return true;
        }
    }
    return false;
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
    if (!endpoint_ipc_state_is_zero(resolved_record)) {
        return MICROS_ENDPOINT_ERROR_STATE;
    }
    if (endpoint_has_foreign_waiters(objects, endpoint)) {
        return MICROS_ENDPOINT_ERROR_STATE;
    }
    owner = resolved_record->owner;
    if (!process_threads_are_held(objects, owner, false)) {
        return MICROS_ENDPOINT_ERROR_STATE;
    }

    record = &registry->endpoints[owner.slot];
    process = &objects->processes[owner.slot];
    clear_endpoint_record(record);
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

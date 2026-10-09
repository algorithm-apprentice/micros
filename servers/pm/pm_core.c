#include "servers/pm/pm_core.h"

#include <stddef.h>

#define MICROS_PM_INITIALIZATION_MAGIC UINT64_C(0x4d4943524f53504d)

static void zero_bytes(void *value, size_t size)
{
    uint8_t *bytes = value;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static bool bytes_are_zero(const uint8_t *bytes, size_t size)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static uint32_t read_u32_le(const uint8_t *bytes)
{
    return (
        (uint32_t)bytes[0]
        | (uint32_t)bytes[1] << 8
        | (uint32_t)bytes[2] << 16
        | (uint32_t)bytes[3] << 24
    );
}

static uint64_t read_u64_le(const uint8_t *bytes)
{
    uint64_t value = 0;
    size_t index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8);
    }
    return value;
}

static void write_u32_le(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void write_u64_le(uint8_t *bytes, uint64_t value)
{
    size_t index;

    for (index = 0; index < 8; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}

static bool handle_is_none(struct micros_pm_process_handle handle)
{
    return (
        handle.slot == 0
        && handle.reserved == 0
        && handle.generation == 0
    );
}

static bool handles_equal(
    struct micros_pm_process_handle left,
    struct micros_pm_process_handle right
)
{
    return (
        left.slot == right.slot
        && left.reserved == right.reserved
        && left.generation == right.generation
    );
}

static bool endpoint_is_concrete(micros_endpoint_t endpoint)
{
    uint32_t slot_mask =
        (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS) - 1;
    uint32_t slot = endpoint & slot_mask;
    uint32_t generation = endpoint >> MICROS_ENDPOINT_SLOT_BITS;

    return (
        endpoint != MICROS_ENDPOINT_NONE
        && endpoint != MICROS_ENDPOINT_ANY
        && slot < MICROS_PROCESS_CAPACITY
        && generation != 0
        && generation <= MICROS_ENDPOINT_GENERATION_MAX
    );
}

static bool endpoint_is_published_elsewhere(
    const struct micros_pm_table *table,
    micros_endpoint_t endpoint,
    uint16_t excluded_slot
)
{
    uint16_t index;

    for (index = 0; index < MICROS_PM_PROCESS_CAPACITY; ++index) {
        if (
            index != excluded_slot
            && table->records[index].endpoint == endpoint
            && (
                table->records[index].state
                    == MICROS_PM_PROCESS_RUNNING
                || table->records[index].state
                    == MICROS_PM_PROCESS_EXITING
            )
        ) {
            return true;
        }
    }
    return false;
}

static bool result_is_defined(enum micros_pm_result result)
{
    return (
        result >= MICROS_PM_RESULT_NO_CHILD
        && result <= MICROS_PM_RESULT_OK
    );
}

static bool exit_kind_is_defined(enum micros_pm_exit_kind kind)
{
    return (
        kind == MICROS_PM_EXIT_NONE
        || kind == MICROS_PM_EXIT_NORMAL
        || kind == MICROS_PM_EXIT_FAULT
    );
}

static bool spawn_resources_are_valid(
    enum micros_pm_spawn_stage stage,
    const struct micros_pm_spawn_resources *resources
)
{
    bool kernel_process;
    bool image;
    bool mappings;
    bool descriptors;
    bool execution;
    bool committed;

    if (
        stage < MICROS_PM_SPAWN_PM_RECORD_RESERVED
        || stage > MICROS_PM_SPAWN_PM_VFS_COMMITTED
        || resources == NULL
        || resources->reserved0 != 0
        || resources->reserved != 0
        || (
            resources->rollback_parent_lost
            && !resources->rollback_active
        )
    ) {
        return false;
    }

    kernel_process = stage >= MICROS_PM_SPAWN_KERNEL_PROCESS_RESERVED;
    image = stage >= MICROS_PM_SPAWN_VFS_IMAGE_PREPARED;
    mappings = stage >= MICROS_PM_SPAWN_VM_MAPPINGS_FROZEN;
    descriptors = stage >= MICROS_PM_SPAWN_VFS_DESCRIPTORS_PREPARED;
    execution = stage >= MICROS_PM_SPAWN_KERNEL_EXECUTION_PREPARED;
    committed = stage >= MICROS_PM_SPAWN_PM_VFS_COMMITTED;

    return (
        (kernel_process == (resources->kernel_transaction != 0))
        && (image == (resources->vfs_image_token != 0))
        && (mappings == (resources->vm_load_token != 0))
        && (mappings == (resources->mapping_generation != 0))
        && (descriptors == (resources->vfs_descriptor_token != 0))
        && (execution == (resources->kernel_prepared_token != 0))
        && (committed == (resources->pm_vfs_commit_token != 0))
        && (
            execution
                ? endpoint_is_concrete(resources->prepared_endpoint)
                : resources->prepared_endpoint == MICROS_ENDPOINT_NONE
        )
    );
}

static void clear_record(
    struct micros_pm_process_record *record,
    enum micros_pm_process_state state
)
{
    uint32_t generation = record->generation;

    zero_bytes(record, sizeof(*record));
    record->state = state;
    record->generation = generation;
    record->endpoint = MICROS_ENDPOINT_NONE;
    record->spawn.prepared_endpoint = MICROS_ENDPOINT_NONE;
}

static struct micros_pm_process_handle record_handle(
    uint16_t slot,
    const struct micros_pm_process_record *record
)
{
    struct micros_pm_process_handle handle;

    zero_bytes(&handle, sizeof(handle));
    handle.slot = slot;
    handle.generation = record->generation;
    return handle;
}

static enum micros_pm_model_error resolve_record(
    const struct micros_pm_table *table,
    struct micros_pm_process_handle handle,
    const struct micros_pm_process_record **record
)
{
    const struct micros_pm_process_record *candidate;

    if (
        table == NULL
        || record == NULL
        || handle.reserved != 0
        || handle.generation == 0
        || handle.slot >= MICROS_PM_PROCESS_CAPACITY
    ) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }

    candidate = &table->records[handle.slot];
    if (
        candidate->state == MICROS_PM_PROCESS_FREE
        || candidate->state == MICROS_PM_PROCESS_QUARANTINED
        || candidate->generation != handle.generation
    ) {
        return MICROS_PM_MODEL_ERROR_STALE;
    }

    *record = candidate;
    return MICROS_PM_MODEL_OK;
}

static enum micros_pm_model_error resolve_spawning_record(
    struct micros_pm_table *table,
    struct micros_pm_process_handle handle,
    enum micros_pm_spawn_stage expected_stage,
    struct micros_pm_process_record **record
)
{
    const struct micros_pm_process_record *resolved;
    enum micros_pm_model_error error;

    error = resolve_record(table, handle, &resolved);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    if (
        resolved->state != MICROS_PM_PROCESS_SPAWNING
        || resolved->spawn_stage != expected_stage
    ) {
        return MICROS_PM_MODEL_ERROR_STATE;
    }
    *record = &table->records[handle.slot];
    return MICROS_PM_MODEL_OK;
}

static enum micros_pm_model_error resolve_forward_spawning_record(
    struct micros_pm_table *table,
    struct micros_pm_process_handle handle,
    enum micros_pm_spawn_stage expected_stage,
    struct micros_pm_process_record **record
)
{
    enum micros_pm_model_error error;

    error = resolve_spawning_record(
        table,
        handle,
        expected_stage,
        record
    );
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    if ((*record)->spawn.rollback_active) {
        return MICROS_PM_MODEL_ERROR_STATE;
    }
    return MICROS_PM_MODEL_OK;
}

static bool table_has_spawning_record(
    const struct micros_pm_table *table
)
{
    size_t index;

    for (index = 0; index < MICROS_PM_PROCESS_CAPACITY; ++index) {
        if (
            table->records[index].state
                == MICROS_PM_PROCESS_SPAWNING
        ) {
            return true;
        }
    }
    return false;
}

static bool find_free_slot(
    const struct micros_pm_table *table,
    uint16_t *slot
)
{
    uint16_t index;

    for (index = 0; index < MICROS_PM_PROCESS_CAPACITY; ++index) {
        if (
            table->records[index].state == MICROS_PM_PROCESS_FREE
            && table->records[index].generation < UINT32_MAX
        ) {
            *slot = index;
            return true;
        }
    }
    return false;
}

static enum micros_pm_spawn_rollback_resource
rollback_resource_for_stage(enum micros_pm_spawn_stage stage)
{
    switch (stage) {
    case MICROS_PM_SPAWN_PM_VFS_COMMITTED:
        return MICROS_PM_ROLLBACK_PM_VFS_COMMIT;
    case MICROS_PM_SPAWN_KERNEL_EXECUTION_PREPARED:
        return MICROS_PM_ROLLBACK_KERNEL_EXECUTION;
    case MICROS_PM_SPAWN_VFS_DESCRIPTORS_PREPARED:
        return MICROS_PM_ROLLBACK_VFS_DESCRIPTORS;
    case MICROS_PM_SPAWN_VM_MAPPINGS_FROZEN:
        return MICROS_PM_ROLLBACK_VM_MAPPINGS;
    case MICROS_PM_SPAWN_VFS_IMAGE_PREPARED:
        return MICROS_PM_ROLLBACK_VFS_IMAGE;
    case MICROS_PM_SPAWN_KERNEL_PROCESS_RESERVED:
        return MICROS_PM_ROLLBACK_KERNEL_PROCESS;
    case MICROS_PM_SPAWN_PM_RECORD_RESERVED:
    case MICROS_PM_SPAWN_NONE:
    default:
        return MICROS_PM_ROLLBACK_NONE;
    }
}

static void populate_rollback_action(
    const struct micros_pm_process_record *record,
    struct micros_pm_spawn_rollback_action *action
)
{
    zero_bytes(action, sizeof(*action));
    action->resource = rollback_resource_for_stage(
        record->spawn_stage
    );
    action->expected_stage = record->spawn_stage;
    action->parent_lost = record->spawn.rollback_parent_lost;
    action->endpoint = MICROS_ENDPOINT_NONE;

    switch (action->resource) {
    case MICROS_PM_ROLLBACK_PM_VFS_COMMIT:
        action->token = record->spawn.pm_vfs_commit_token;
        break;
    case MICROS_PM_ROLLBACK_KERNEL_EXECUTION:
        action->token = record->spawn.kernel_prepared_token;
        action->endpoint = record->spawn.prepared_endpoint;
        break;
    case MICROS_PM_ROLLBACK_VFS_DESCRIPTORS:
        action->token = record->spawn.vfs_descriptor_token;
        break;
    case MICROS_PM_ROLLBACK_VM_MAPPINGS:
        action->token = record->spawn.vm_load_token;
        action->secondary_token = record->spawn.mapping_generation;
        break;
    case MICROS_PM_ROLLBACK_VFS_IMAGE:
        action->token = record->spawn.vfs_image_token;
        break;
    case MICROS_PM_ROLLBACK_KERNEL_PROCESS:
        action->token = record->spawn.kernel_transaction;
        break;
    case MICROS_PM_ROLLBACK_NONE:
    default:
        break;
    }
}

static void populate_completed_rollback(
    uint64_t parent_reply_token,
    bool parent_lost,
    struct micros_pm_spawn_rollback_action *action
)
{
    zero_bytes(action, sizeof(*action));
    action->complete = true;
    action->parent_lost = parent_lost;
    action->reply_token = (
        parent_lost ? 0 : parent_reply_token
    );
    action->endpoint = MICROS_ENDPOINT_NONE;
}

static bool spawn_is_clear(
    const struct micros_pm_process_record *record
)
{
    return (
        record->spawn_stage == MICROS_PM_SPAWN_NONE
        && record->spawn.parent_reply_token == 0
        && record->spawn.kernel_transaction == 0
        && record->spawn.vfs_image_token == 0
        && record->spawn.vm_load_token == 0
        && record->spawn.mapping_generation == 0
        && record->spawn.vfs_descriptor_token == 0
        && record->spawn.kernel_prepared_token == 0
        && record->spawn.pm_vfs_commit_token == 0
        && record->spawn.prepared_endpoint == MICROS_ENDPOINT_NONE
        && !record->spawn.rollback_active
        && !record->spawn.rollback_parent_lost
        && record->spawn.reserved0 == 0
        && record->spawn.reserved == 0
    );
}

static bool exit_is_clear(
    const struct micros_pm_process_record *record
)
{
    return (
        record->exit_stage == MICROS_PM_EXIT_STAGE_NONE
        && record->exit_kind == MICROS_PM_EXIT_NONE
        && record->exit_code == 0
        && record->reserved1 == 0
        && record->exit_transaction == 0
    );
}

static bool wait_is_clear(const struct micros_pm_wait_state *wait)
{
    return (
        !wait->active
        && !wait->result_ready
        && wait->reserved == 0
        && wait->flags == 0
        && wait->selector == 0
        && wait->reply_token == 0
        && handle_is_none(wait->pending_child)
    );
}

static void clear_wait(struct micros_pm_wait_state *wait)
{
    zero_bytes(wait, sizeof(*wait));
}

static void clear_wait_outcome(struct micros_pm_wait_outcome *outcome)
{
    zero_bytes(outcome, sizeof(*outcome));
    outcome->result = MICROS_PM_RESULT_OK;
}

static bool wait_matches_pid(
    const struct micros_pm_wait_state *wait,
    uint64_t pid
)
{
    return wait->selector == 0 || wait->selector == pid;
}

static bool record_is_child_of(
    const struct micros_pm_process_record *record,
    struct micros_pm_process_handle parent
)
{
    return (
        record->state != MICROS_PM_PROCESS_FREE
        && record->state != MICROS_PM_PROCESS_QUARANTINED
        && handles_equal(record->parent, parent)
    );
}

static bool find_lowest_matching_zombie(
    const struct micros_pm_table *table,
    struct micros_pm_process_handle parent,
    uint64_t selector,
    uint16_t *slot
)
{
    uint64_t lowest_pid = UINT64_MAX;
    uint16_t selected_slot = 0;
    uint16_t index;
    bool found = false;

    for (index = 0; index < MICROS_PM_PROCESS_CAPACITY; ++index) {
        const struct micros_pm_process_record *record
            = &table->records[index];

        if (
            record->state == MICROS_PM_PROCESS_ZOMBIE
            && !record->reap_pending
            && record_is_child_of(record, parent)
            && (selector == 0 || selector == record->pid)
            && (!found || record->pid < lowest_pid)
        ) {
            found = true;
            lowest_pid = record->pid;
            selected_slot = index;
        }
    }
    if (found) {
        *slot = selected_slot;
    }
    return found;
}

static bool has_matching_child(
    const struct micros_pm_table *table,
    struct micros_pm_process_handle parent,
    uint64_t selector
)
{
    size_t index;

    for (index = 0; index < MICROS_PM_PROCESS_CAPACITY; ++index) {
        const struct micros_pm_process_record *record
            = &table->records[index];

        if (
            record_is_child_of(record, parent)
            && (selector == 0 || selector == record->pid)
        ) {
            return true;
        }
    }
    return false;
}

static void populate_wait_outcome(
    const struct micros_pm_process_record *parent,
    const struct micros_pm_process_record *child,
    struct micros_pm_process_handle child_handle,
    struct micros_pm_wait_outcome *outcome
)
{
    clear_wait_outcome(outcome);
    outcome->action = MICROS_PM_WAIT_ACTION_REPLY;
    outcome->reply_token = parent->wait.reply_token;
    outcome->child_pid = child->pid;
    outcome->exit_kind = child->exit_kind;
    outcome->exit_code = child->exit_code;
    outcome->child = child_handle;
}

static bool stage_wait_result(
    struct micros_pm_table *table,
    struct micros_pm_process_handle parent_handle,
    struct micros_pm_wait_outcome *outcome
)
{
    struct micros_pm_process_record *parent;
    struct micros_pm_process_record *child;
    struct micros_pm_process_handle child_handle;
    uint16_t child_slot;

    parent = &table->records[parent_handle.slot];
    if (
        !parent->wait.active
        || parent->wait.result_ready
        || !find_lowest_matching_zombie(
            table,
            parent_handle,
            parent->wait.selector,
            &child_slot
        )
    ) {
        return false;
    }

    child = &table->records[child_slot];
    child_handle = record_handle(child_slot, child);
    child->reap_pending = true;
    parent->wait.result_ready = true;
    parent->wait.pending_child = child_handle;
    populate_wait_outcome(parent, child, child_handle, outcome);
    return true;
}

static void release_record(struct micros_pm_process_record *record)
{
    if (record->generation == UINT32_MAX) {
        clear_record(record, MICROS_PM_PROCESS_QUARANTINED);
    } else {
        clear_record(record, MICROS_PM_PROCESS_FREE);
    }
}

static enum micros_pm_model_error allocate_record(
    struct micros_pm_table *table,
    struct micros_pm_process_handle parent,
    bool init_candidate,
    uint64_t parent_reply_token,
    struct micros_pm_process_handle *child,
    uint64_t *pid
)
{
    struct micros_pm_process_record *record;
    struct micros_pm_process_handle new_handle;
    uint64_t new_pid;
    uint16_t slot;

    if (table->pid_exhausted || table->last_pid == UINT64_MAX) {
        return MICROS_PM_MODEL_ERROR_CAPACITY;
    }
    if (!find_free_slot(table, &slot)) {
        return MICROS_PM_MODEL_ERROR_CAPACITY;
    }

    new_pid = table->last_pid + 1;
    record = &table->records[slot];
    ++record->generation;
    clear_record(record, MICROS_PM_PROCESS_SPAWNING);
    record->pid = new_pid;
    record->parent = parent;
    record->init_candidate = init_candidate;
    record->spawn.parent_reply_token = parent_reply_token;
    record->spawn_stage = MICROS_PM_SPAWN_PM_RECORD_RESERVED;

    table->last_pid = new_pid;
    if (new_pid == UINT64_MAX) {
        table->pid_exhausted = true;
    }
    new_handle = record_handle(slot, record);
    *child = new_handle;
    *pid = new_pid;
    return MICROS_PM_MODEL_OK;
}

enum micros_pm_protocol_status micros_pm_decode_request(
    const struct micros_ipc_message *message,
    struct micros_pm_request *request
)
{
    struct micros_pm_request decoded;
    uint32_t version;
    uint32_t flags;

    if (message == NULL || request == NULL) {
        return MICROS_PM_PROTOCOL_INVARIANT;
    }
    if (
        message->type != MICROS_PM_MESSAGE_EXIT
        && message->type != MICROS_PM_MESSAGE_WAIT
    ) {
        return MICROS_PM_PROTOCOL_BAD_TYPE;
    }

    version = read_u32_le(&message->payload[0]);
    if (version != MICROS_PM_PROTOCOL_VERSION) {
        return MICROS_PM_PROTOCOL_BAD_VERSION;
    }
    flags = read_u32_le(&message->payload[4]);
    if (message->type == MICROS_PM_MESSAGE_EXIT) {
        if (
            flags != 0
            || !bytes_are_zero(&message->payload[12], 36)
        ) {
            return MICROS_PM_PROTOCOL_MALFORMED;
        }
    } else if (
        (flags & ~MICROS_PM_WAIT_NOHANG) != 0
        || !bytes_are_zero(&message->payload[16], 32)
    ) {
        return MICROS_PM_PROTOCOL_MALFORMED;
    }
    if (message->reply_token == 0) {
        return MICROS_PM_PROTOCOL_INVARIANT;
    }

    zero_bytes(&decoded, sizeof(decoded));
    decoded.type = message->type;
    decoded.flags = flags;
    decoded.source = message->source;
    decoded.reply_token = message->reply_token;
    if (message->type == MICROS_PM_MESSAGE_EXIT) {
        decoded.exit_code = (int32_t)read_u32_le(
            &message->payload[8]
        );
    } else {
        decoded.child_pid = read_u64_le(&message->payload[8]);
    }
    *request = decoded;
    return MICROS_PM_PROTOCOL_OK;
}

enum micros_pm_model_error micros_pm_build_result(
    uint32_t request_type,
    enum micros_pm_result result,
    uint64_t child_pid,
    enum micros_pm_exit_kind exit_kind,
    int32_t exit_code,
    struct micros_ipc_message *message
)
{
    struct micros_ipc_message built;

    if (
        message == NULL
        || (request_type & MICROS_IPC_TYPE_KERNEL_MASK) != 0
        || !result_is_defined(result)
        || !exit_kind_is_defined(exit_kind)
    ) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    if (result == MICROS_PM_RESULT_OK) {
        if (
            request_type != MICROS_PM_MESSAGE_WAIT
            || (
                child_pid == 0
                && (
                    exit_kind != MICROS_PM_EXIT_NONE
                    || exit_code != 0
                )
            )
            || (
                child_pid != 0
                && exit_kind != MICROS_PM_EXIT_NORMAL
                && exit_kind != MICROS_PM_EXIT_FAULT
            )
        ) {
            return MICROS_PM_MODEL_ERROR_ARGUMENT;
        }
    } else if (
        child_pid != 0
        || exit_kind != MICROS_PM_EXIT_NONE
        || exit_code != 0
    ) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }

    zero_bytes(&built, sizeof(built));
    built.type = MICROS_PM_MESSAGE_RESULT;
    write_u32_le(&built.payload[0], MICROS_PM_PROTOCOL_VERSION);
    write_u32_le(&built.payload[4], request_type);
    write_u32_le(&built.payload[8], (uint32_t)(int32_t)result);
    write_u64_le(&built.payload[16], child_pid);
    write_u32_le(&built.payload[24], (uint32_t)exit_kind);
    write_u32_le(&built.payload[28], (uint32_t)exit_code);
    *message = built;
    return MICROS_PM_MODEL_OK;
}

enum micros_pm_model_error micros_pm_table_initialize(
    struct micros_pm_table *table
)
{
    size_t index;

    if (table == NULL) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }

    zero_bytes(table, sizeof(*table));
    table->initialization_magic = MICROS_PM_INITIALIZATION_MAGIC;
    for (index = 0; index < MICROS_PM_PROCESS_CAPACITY; ++index) {
        table->records[index].endpoint = MICROS_ENDPOINT_NONE;
        table->records[index].spawn.prepared_endpoint
            = MICROS_ENDPOINT_NONE;
    }
    return MICROS_PM_MODEL_OK;
}

enum micros_pm_model_error micros_pm_table_validate(
    const struct micros_pm_table *table
)
{
    size_t index;
    size_t other;
    size_t spawning_count = 0;
    size_t reaper_count = 0;
    size_t non_init_count = 0;
    size_t active_count = 0;

    if (
        table == NULL
        || table->initialization_magic
            != MICROS_PM_INITIALIZATION_MAGIC
        || !bytes_are_zero(table->reserved0, sizeof(table->reserved0))
        || table->reaper.reserved != 0
        || table->pid_exhausted != (table->last_pid == UINT64_MAX)
        || (!table->init_attempted && table->last_pid != 0)
    ) {
        return MICROS_PM_MODEL_ERROR_INVARIANT;
    }

    for (index = 0; index < MICROS_PM_PROCESS_CAPACITY; ++index) {
        const struct micros_pm_process_record *record
            = &table->records[index];

        if (
            record->reserved0 != 0
            || record->parent.reserved != 0
            || record->spawn.reserved != 0
            || record->reserved1 != 0
            || record->wait.reserved != 0
            || record->wait.pending_child.reserved != 0
            || !bytes_are_zero(
                record->reserved2,
                sizeof(record->reserved2)
            )
        ) {
            return MICROS_PM_MODEL_ERROR_INVARIANT;
        }
        if (
            record->state == MICROS_PM_PROCESS_FREE
            || record->state == MICROS_PM_PROCESS_QUARANTINED
        ) {
            if (
                record->pid != 0
                || !handle_is_none(record->parent)
                || record->init_candidate
                || record->reaper
                || record->endpoint != MICROS_ENDPOINT_NONE
                || !spawn_is_clear(record)
                || !exit_is_clear(record)
                || !wait_is_clear(&record->wait)
                || record->reap_pending
                || (
                    record->state == MICROS_PM_PROCESS_FREE
                    && record->generation == UINT32_MAX
                )
                || (
                    record->state
                        == MICROS_PM_PROCESS_QUARANTINED
                    && record->generation != UINT32_MAX
                )
            ) {
                return MICROS_PM_MODEL_ERROR_INVARIANT;
            }
            continue;
        }
        if (
            record->generation == 0
            || record->pid == 0
            || record->pid > table->last_pid
        ) {
            return MICROS_PM_MODEL_ERROR_INVARIANT;
        }
        ++active_count;
        for (other = index + 1; other < MICROS_PM_PROCESS_CAPACITY;
             ++other) {
            const struct micros_pm_process_record *other_record
                = &table->records[other];

            if (
                other_record->state != MICROS_PM_PROCESS_FREE
                && other_record->state
                    != MICROS_PM_PROCESS_QUARANTINED
                && other_record->pid == record->pid
            ) {
                return MICROS_PM_MODEL_ERROR_INVARIANT;
            }
            if (
                endpoint_is_concrete(record->endpoint)
                && endpoint_is_concrete(other_record->endpoint)
                && other_record->endpoint == record->endpoint
            ) {
                return MICROS_PM_MODEL_ERROR_INVARIANT;
            }
        }

        if (record->state == MICROS_PM_PROCESS_SPAWNING) {
            ++spawning_count;
            if (
                record->reaper
                || record->endpoint != MICROS_ENDPOINT_NONE
                || !exit_is_clear(record)
                || !wait_is_clear(&record->wait)
                || record->reap_pending
                || !spawn_resources_are_valid(
                    record->spawn_stage,
                    &record->spawn
                )
                || (
                    record->spawn_stage
                        >= MICROS_PM_SPAWN_KERNEL_EXECUTION_PREPARED
                    && endpoint_is_published_elsewhere(
                        table,
                        record->spawn.prepared_endpoint,
                        (uint16_t)index
                    )
                )
                || (
                    record->init_candidate
                    && (
                        record->pid != 1
                        || !handle_is_none(record->parent)
                        || record->spawn.parent_reply_token != 0
                    )
                )
                || (
                    !record->init_candidate
                    && (
                        handle_is_none(record->parent)
                        || record->spawn.parent_reply_token == 0
                    )
                )
                || (
                    record->spawn.rollback_active
                    && record->spawn_stage
                        == MICROS_PM_SPAWN_PM_RECORD_RESERVED
                )
            ) {
                return MICROS_PM_MODEL_ERROR_INVARIANT;
            }
            if (!record->init_candidate) {
                ++non_init_count;
            }
        } else if (record->state == MICROS_PM_PROCESS_RUNNING) {
            if (
                record->init_candidate
                || !endpoint_is_concrete(record->endpoint)
                || !spawn_is_clear(record)
                || !exit_is_clear(record)
                || record->reap_pending
                || (
                    record->reaper
                    && (
                        record->pid != 1
                        || !handle_is_none(record->parent)
                    )
                )
                || (
                    !record->reaper
                    && handle_is_none(record->parent)
                )
            ) {
                return MICROS_PM_MODEL_ERROR_INVARIANT;
            }
            if (record->wait.active) {
                const struct micros_pm_process_record *pending;
                enum micros_pm_model_error error;

                if (
                    record->wait.reply_token == 0
                    || (
                        record->wait.flags
                        & ~MICROS_PM_WAIT_NOHANG
                    ) != 0
                ) {
                    return MICROS_PM_MODEL_ERROR_INVARIANT;
                }
                if (record->wait.result_ready) {
                    error = resolve_record(
                        table,
                        record->wait.pending_child,
                        &pending
                    );
                    if (
                        error != MICROS_PM_MODEL_OK
                        || pending->state
                            != MICROS_PM_PROCESS_ZOMBIE
                        || !pending->reap_pending
                        || !handles_equal(
                            pending->parent,
                            record_handle(
                                (uint16_t)index,
                                record
                            )
                        )
                        || !wait_matches_pid(
                            &record->wait,
                            pending->pid
                        )
                    ) {
                        return MICROS_PM_MODEL_ERROR_INVARIANT;
                    }
                } else if (
                    !handle_is_none(record->wait.pending_child)
                    || !has_matching_child(
                        table,
                        record_handle((uint16_t)index, record),
                        record->wait.selector
                    )
                ) {
                    return MICROS_PM_MODEL_ERROR_INVARIANT;
                }
            } else if (!wait_is_clear(&record->wait)) {
                return MICROS_PM_MODEL_ERROR_INVARIANT;
            }
            if (record->reaper) {
                ++reaper_count;
                if (
                    !handles_equal(
                        table->reaper,
                        record_handle((uint16_t)index, record)
                    )
                ) {
                    return MICROS_PM_MODEL_ERROR_INVARIANT;
                }
            } else {
                ++non_init_count;
            }
        } else if (record->state == MICROS_PM_PROCESS_EXITING) {
            if (
                record->init_candidate
                || record->reaper
                || handle_is_none(record->parent)
                || !spawn_is_clear(record)
                || !wait_is_clear(&record->wait)
                || record->reap_pending
                || record->exit_transaction == 0
                || (
                    record->exit_kind != MICROS_PM_EXIT_NORMAL
                    && record->exit_kind != MICROS_PM_EXIT_FAULT
                )
                || (
                    record->exit_stage
                        == MICROS_PM_EXIT_STAGE_STOPPED
                    && !endpoint_is_concrete(record->endpoint)
                )
                || (
                    record->exit_stage
                        != MICROS_PM_EXIT_STAGE_STOPPED
                    && record->endpoint != MICROS_ENDPOINT_NONE
                )
                || (
                    record->exit_stage
                        != MICROS_PM_EXIT_STAGE_STOPPED
                    && record->exit_stage
                        != MICROS_PM_EXIT_STAGE_VM_RELEASE_PENDING
                    && record->exit_stage
                        != MICROS_PM_EXIT_STAGE_KERNEL_PROCESS_EMPTY
                )
            ) {
                return MICROS_PM_MODEL_ERROR_INVARIANT;
            }
            ++non_init_count;
        } else if (record->state == MICROS_PM_PROCESS_ZOMBIE) {
            if (
                record->init_candidate
                || record->reaper
                || handle_is_none(record->parent)
                || record->endpoint != MICROS_ENDPOINT_NONE
                || !spawn_is_clear(record)
                || record->exit_stage != MICROS_PM_EXIT_STAGE_NONE
                || (
                    record->exit_kind != MICROS_PM_EXIT_NORMAL
                    && record->exit_kind != MICROS_PM_EXIT_FAULT
                )
                || record->reserved1 != 0
                || record->exit_transaction != 0
                || !wait_is_clear(&record->wait)
            ) {
                return MICROS_PM_MODEL_ERROR_INVARIANT;
            }
            ++non_init_count;
        } else {
            return MICROS_PM_MODEL_ERROR_INVARIANT;
        }

        if (!handle_is_none(record->parent)) {
            const struct micros_pm_process_record *parent;
            enum micros_pm_model_error error;

            if (
                handles_equal(
                    record->parent,
                    record_handle((uint16_t)index, record)
                )
            ) {
                return MICROS_PM_MODEL_ERROR_INVARIANT;
            }
            error = resolve_record(table, record->parent, &parent);
            if (
                error != MICROS_PM_MODEL_OK
                || parent->state != MICROS_PM_PROCESS_RUNNING
            ) {
                return MICROS_PM_MODEL_ERROR_INVARIANT;
            }
        }
        if (record->reap_pending) {
            const struct micros_pm_process_record *parent;
            enum micros_pm_model_error error;

            if (record->state != MICROS_PM_PROCESS_ZOMBIE) {
                return MICROS_PM_MODEL_ERROR_INVARIANT;
            }
            error = resolve_record(table, record->parent, &parent);
            if (
                error != MICROS_PM_MODEL_OK
                || !parent->wait.active
                || !parent->wait.result_ready
                || !handles_equal(
                    parent->wait.pending_child,
                    record_handle((uint16_t)index, record)
                )
            ) {
                return MICROS_PM_MODEL_ERROR_INVARIANT;
            }
        }
    }

    if (
        spawning_count > 1
        || reaper_count > 1
        || (active_count != 0 && !table->runtime_enabled)
        || (reaper_count == 0 && non_init_count != 0)
        || (
            reaper_count == 0
            && !handle_is_none(table->reaper)
        )
        || (
            reaper_count == 1
            && handle_is_none(table->reaper)
        )
    ) {
        return MICROS_PM_MODEL_ERROR_INVARIANT;
    }
    return MICROS_PM_MODEL_OK;
}

enum micros_pm_model_error micros_pm_enable_runtime(
    struct micros_pm_table *table,
    const struct micros_ipc_message *message
)
{
    enum micros_pm_model_error error;

    if (table == NULL || message == NULL) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = micros_pm_table_validate(table);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    if (table->runtime_enabled) {
        return MICROS_PM_MODEL_ERROR_STATE;
    }
    if (
        message->source != MICROS_ENDPOINT_NONE
        || message->type != MICROS_IPC_TYPE_KERNEL_NOTIFICATION
        || message->reply_token != 0
        || read_u64_le(&message->payload[0])
            != MICROS_KERNEL_EVENT_BOOTSTRAP_SEALED
        || !bytes_are_zero(&message->payload[8], 40)
    ) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }

    table->runtime_enabled = true;
    return MICROS_PM_MODEL_OK;
}

enum micros_pm_model_error micros_pm_begin_init(
    struct micros_pm_table *table,
    struct micros_pm_process_handle *child,
    uint64_t *pid
)
{
    struct micros_pm_process_handle parent;
    enum micros_pm_model_error error;

    if (table == NULL || child == NULL || pid == NULL) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = micros_pm_table_validate(table);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    if (
        !table->runtime_enabled
        || table->init_attempted
        || table_has_spawning_record(table)
        || !handle_is_none(table->reaper)
        || table->last_pid != 0
    ) {
        return MICROS_PM_MODEL_ERROR_STATE;
    }

    zero_bytes(&parent, sizeof(parent));
    error = allocate_record(table, parent, true, 0, child, pid);
    if (error == MICROS_PM_MODEL_OK) {
        table->init_attempted = true;
    }
    return error;
}

enum micros_pm_model_error micros_pm_begin_spawn(
    struct micros_pm_table *table,
    struct micros_pm_process_handle parent,
    uint64_t reply_token,
    struct micros_pm_process_handle *child,
    uint64_t *pid
)
{
    const struct micros_pm_process_record *parent_record;
    enum micros_pm_model_error error;

    if (
        table == NULL
        || reply_token == 0
        || child == NULL
        || pid == NULL
    ) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = micros_pm_table_validate(table);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    if (
        !table->runtime_enabled
        || handle_is_none(table->reaper)
        || table_has_spawning_record(table)
    ) {
        return MICROS_PM_MODEL_ERROR_STATE;
    }
    error = resolve_record(table, parent, &parent_record);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    if (parent_record->state != MICROS_PM_PROCESS_RUNNING) {
        return MICROS_PM_MODEL_ERROR_STATE;
    }
    return allocate_record(
        table,
        parent,
        false,
        reply_token,
        child,
        pid
    );
}

enum micros_pm_model_error micros_pm_spawn_reserve_kernel(
    struct micros_pm_table *table,
    struct micros_pm_process_handle child,
    uint64_t transaction
)
{
    struct micros_pm_process_record *record;
    enum micros_pm_model_error error;

    if (table == NULL || transaction == 0) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = micros_pm_table_validate(table);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    error = resolve_forward_spawning_record(
        table,
        child,
        MICROS_PM_SPAWN_PM_RECORD_RESERVED,
        &record
    );
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }

    record->spawn.kernel_transaction = transaction;
    record->spawn_stage = MICROS_PM_SPAWN_KERNEL_PROCESS_RESERVED;
    return MICROS_PM_MODEL_OK;
}

enum micros_pm_model_error micros_pm_spawn_prepare_image(
    struct micros_pm_table *table,
    struct micros_pm_process_handle child,
    uint64_t image_token
)
{
    struct micros_pm_process_record *record;
    enum micros_pm_model_error error;

    if (table == NULL || image_token == 0) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = micros_pm_table_validate(table);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    error = resolve_forward_spawning_record(
        table,
        child,
        MICROS_PM_SPAWN_KERNEL_PROCESS_RESERVED,
        &record
    );
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }

    record->spawn.vfs_image_token = image_token;
    record->spawn_stage = MICROS_PM_SPAWN_VFS_IMAGE_PREPARED;
    return MICROS_PM_MODEL_OK;
}

enum micros_pm_model_error micros_pm_spawn_freeze_mappings(
    struct micros_pm_table *table,
    struct micros_pm_process_handle child,
    uint64_t load_token,
    uint64_t mapping_generation
)
{
    struct micros_pm_process_record *record;
    enum micros_pm_model_error error;

    if (
        table == NULL
        || load_token == 0
        || mapping_generation == 0
    ) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = micros_pm_table_validate(table);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    error = resolve_forward_spawning_record(
        table,
        child,
        MICROS_PM_SPAWN_VFS_IMAGE_PREPARED,
        &record
    );
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }

    record->spawn.vm_load_token = load_token;
    record->spawn.mapping_generation = mapping_generation;
    record->spawn_stage = MICROS_PM_SPAWN_VM_MAPPINGS_FROZEN;
    return MICROS_PM_MODEL_OK;
}

enum micros_pm_model_error micros_pm_spawn_prepare_descriptors(
    struct micros_pm_table *table,
    struct micros_pm_process_handle child,
    uint64_t descriptor_token
)
{
    struct micros_pm_process_record *record;
    enum micros_pm_model_error error;

    if (table == NULL || descriptor_token == 0) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = micros_pm_table_validate(table);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    error = resolve_forward_spawning_record(
        table,
        child,
        MICROS_PM_SPAWN_VM_MAPPINGS_FROZEN,
        &record
    );
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }

    record->spawn.vfs_descriptor_token = descriptor_token;
    record->spawn_stage
        = MICROS_PM_SPAWN_VFS_DESCRIPTORS_PREPARED;
    return MICROS_PM_MODEL_OK;
}

enum micros_pm_model_error micros_pm_spawn_prepare_execution(
    struct micros_pm_table *table,
    struct micros_pm_process_handle child,
    uint64_t prepared_token,
    micros_endpoint_t endpoint
)
{
    struct micros_pm_process_record *record;
    enum micros_pm_model_error error;

    if (
        table == NULL
        || prepared_token == 0
        || !endpoint_is_concrete(endpoint)
    ) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = micros_pm_table_validate(table);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    error = resolve_forward_spawning_record(
        table,
        child,
        MICROS_PM_SPAWN_VFS_DESCRIPTORS_PREPARED,
        &record
    );
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    if (endpoint_is_published_elsewhere(table, endpoint, child.slot)) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }

    record->spawn.kernel_prepared_token = prepared_token;
    record->spawn.prepared_endpoint = endpoint;
    record->spawn_stage
        = MICROS_PM_SPAWN_KERNEL_EXECUTION_PREPARED;
    return MICROS_PM_MODEL_OK;
}

enum micros_pm_model_error micros_pm_spawn_commit_hidden(
    struct micros_pm_table *table,
    struct micros_pm_process_handle child,
    uint64_t commit_token
)
{
    struct micros_pm_process_record *record;
    enum micros_pm_model_error error;

    if (table == NULL || commit_token == 0) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = micros_pm_table_validate(table);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    error = resolve_forward_spawning_record(
        table,
        child,
        MICROS_PM_SPAWN_KERNEL_EXECUTION_PREPARED,
        &record
    );
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }

    record->spawn.pm_vfs_commit_token = commit_token;
    record->spawn_stage = MICROS_PM_SPAWN_PM_VFS_COMMITTED;
    return MICROS_PM_MODEL_OK;
}

enum micros_pm_model_error micros_pm_spawn_activate(
    struct micros_pm_table *table,
    struct micros_pm_process_handle child,
    micros_endpoint_t endpoint,
    uint64_t *reply_token
)
{
    struct micros_pm_process_record *record;
    bool init_candidate;
    uint64_t parent_reply_token;
    enum micros_pm_model_error error;

    if (
        table == NULL
        || reply_token == NULL
        || !endpoint_is_concrete(endpoint)
    ) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = micros_pm_table_validate(table);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    error = resolve_forward_spawning_record(
        table,
        child,
        MICROS_PM_SPAWN_PM_VFS_COMMITTED,
        &record
    );
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    if (record->spawn.prepared_endpoint != endpoint) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    if (endpoint_is_published_elsewhere(table, endpoint, child.slot)) {
        return MICROS_PM_MODEL_ERROR_STATE;
    }

    init_candidate = record->init_candidate;
    parent_reply_token = record->spawn.parent_reply_token;
    record->state = MICROS_PM_PROCESS_RUNNING;
    record->init_candidate = false;
    record->endpoint = endpoint;
    record->spawn_stage = MICROS_PM_SPAWN_NONE;
    zero_bytes(&record->spawn, sizeof(record->spawn));
    record->spawn.prepared_endpoint = MICROS_ENDPOINT_NONE;
    if (init_candidate) {
        record->reaper = true;
        table->reaper = child;
    }
    *reply_token = parent_reply_token;
    return MICROS_PM_MODEL_OK;
}

enum micros_pm_model_error micros_pm_spawn_rollback_begin(
    struct micros_pm_table *table,
    struct micros_pm_process_handle child,
    enum micros_pm_spawn_stage expected_stage,
    uint64_t parent_reply_token,
    bool parent_lost,
    struct micros_pm_spawn_rollback_action *action
)
{
    struct micros_pm_spawn_rollback_action committed;
    struct micros_pm_process_record *record;
    enum micros_pm_model_error error;

    if (
        table == NULL
        || action == NULL
        || expected_stage < MICROS_PM_SPAWN_PM_RECORD_RESERVED
        || expected_stage > MICROS_PM_SPAWN_PM_VFS_COMMITTED
    ) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = micros_pm_table_validate(table);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    error = resolve_spawning_record(
        table,
        child,
        expected_stage,
        &record
    );
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    if (record->spawn.rollback_active) {
        return MICROS_PM_MODEL_ERROR_STATE;
    }
    if (record->spawn.parent_reply_token != parent_reply_token) {
        return MICROS_PM_MODEL_ERROR_STALE;
    }

    if (expected_stage == MICROS_PM_SPAWN_PM_RECORD_RESERVED) {
        populate_completed_rollback(
            parent_reply_token,
            parent_lost,
            &committed
        );
        release_record(record);
    } else {
        record->spawn.rollback_active = true;
        record->spawn.rollback_parent_lost = parent_lost;
        populate_rollback_action(record, &committed);
    }
    *action = committed;
    return MICROS_PM_MODEL_OK;
}

enum micros_pm_model_error micros_pm_spawn_rollback_ack(
    struct micros_pm_table *table,
    struct micros_pm_process_handle child,
    enum micros_pm_spawn_stage expected_stage,
    enum micros_pm_spawn_rollback_resource resource,
    uint64_t token,
    uint64_t secondary_token,
    micros_endpoint_t endpoint,
    struct micros_pm_spawn_rollback_action *action
)
{
    struct micros_pm_spawn_rollback_action committed;
    struct micros_pm_spawn_rollback_action expected;
    struct micros_pm_process_record *record;
    uint64_t parent_reply_token;
    bool parent_lost;
    enum micros_pm_model_error error;

    if (
        table == NULL
        || action == NULL
        || expected_stage < MICROS_PM_SPAWN_KERNEL_PROCESS_RESERVED
        || expected_stage > MICROS_PM_SPAWN_PM_VFS_COMMITTED
        || resource <= MICROS_PM_ROLLBACK_NONE
        || resource > MICROS_PM_ROLLBACK_KERNEL_PROCESS
    ) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = micros_pm_table_validate(table);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    error = resolve_spawning_record(
        table,
        child,
        expected_stage,
        &record
    );
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    if (!record->spawn.rollback_active) {
        return MICROS_PM_MODEL_ERROR_STATE;
    }
    populate_rollback_action(record, &expected);
    if (resource != expected.resource) {
        return MICROS_PM_MODEL_ERROR_STATE;
    }
    if (
        token != expected.token
        || secondary_token != expected.secondary_token
        || endpoint != expected.endpoint
    ) {
        return MICROS_PM_MODEL_ERROR_STALE;
    }

    parent_reply_token = record->spawn.parent_reply_token;
    parent_lost = record->spawn.rollback_parent_lost;
    switch (resource) {
    case MICROS_PM_ROLLBACK_PM_VFS_COMMIT:
        record->spawn.pm_vfs_commit_token = 0;
        record->spawn_stage
            = MICROS_PM_SPAWN_KERNEL_EXECUTION_PREPARED;
        break;
    case MICROS_PM_ROLLBACK_KERNEL_EXECUTION:
        record->spawn.kernel_prepared_token = 0;
        record->spawn.prepared_endpoint = MICROS_ENDPOINT_NONE;
        record->spawn_stage
            = MICROS_PM_SPAWN_VFS_DESCRIPTORS_PREPARED;
        break;
    case MICROS_PM_ROLLBACK_VFS_DESCRIPTORS:
        record->spawn.vfs_descriptor_token = 0;
        record->spawn_stage
            = MICROS_PM_SPAWN_VM_MAPPINGS_FROZEN;
        break;
    case MICROS_PM_ROLLBACK_VM_MAPPINGS:
        record->spawn.vm_load_token = 0;
        record->spawn.mapping_generation = 0;
        record->spawn_stage = MICROS_PM_SPAWN_VFS_IMAGE_PREPARED;
        break;
    case MICROS_PM_ROLLBACK_VFS_IMAGE:
        record->spawn.vfs_image_token = 0;
        record->spawn_stage
            = MICROS_PM_SPAWN_KERNEL_PROCESS_RESERVED;
        break;
    case MICROS_PM_ROLLBACK_KERNEL_PROCESS:
        record->spawn.kernel_transaction = 0;
        record->spawn_stage = MICROS_PM_SPAWN_PM_RECORD_RESERVED;
        break;
    case MICROS_PM_ROLLBACK_NONE:
    default:
        return MICROS_PM_MODEL_ERROR_INVARIANT;
    }

    if (record->spawn_stage == MICROS_PM_SPAWN_PM_RECORD_RESERVED) {
        populate_completed_rollback(
            parent_reply_token,
            parent_lost,
            &committed
        );
        release_record(record);
    } else {
        populate_rollback_action(record, &committed);
    }
    *action = committed;
    return MICROS_PM_MODEL_OK;
}

enum micros_pm_model_error micros_pm_process_resolve(
    const struct micros_pm_table *table,
    struct micros_pm_process_handle handle,
    const struct micros_pm_process_record **record
)
{
    enum micros_pm_model_error error;

    if (table == NULL || record == NULL) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = micros_pm_table_validate(table);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    return resolve_record(table, handle, record);
}

enum micros_pm_model_error micros_pm_exit_begin(
    struct micros_pm_table *table,
    struct micros_pm_process_handle process,
    uint64_t transaction,
    enum micros_pm_exit_kind exit_kind,
    int32_t exit_code,
    struct micros_pm_exit_effects *effects
)
{
    struct micros_pm_exit_effects committed;
    struct micros_pm_process_record *record;
    const struct micros_pm_process_record *resolved;
    struct micros_pm_process_handle reaper;
    size_t index;
    enum micros_pm_model_error error;

    if (
        table == NULL
        || effects == NULL
        || transaction == 0
        || (
            exit_kind != MICROS_PM_EXIT_NORMAL
            && exit_kind != MICROS_PM_EXIT_FAULT
        )
    ) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = micros_pm_table_validate(table);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    error = resolve_record(table, process, &resolved);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    if (resolved->state != MICROS_PM_PROCESS_RUNNING) {
        return MICROS_PM_MODEL_ERROR_STATE;
    }
    if (resolved->reaper) {
        return MICROS_PM_MODEL_ERROR_INVARIANT;
    }

    for (index = 0; index < MICROS_PM_PROCESS_CAPACITY; ++index) {
        const struct micros_pm_process_record *child
            = &table->records[index];

        if (
            child->state == MICROS_PM_PROCESS_SPAWNING
            && handles_equal(child->parent, process)
        ) {
            return MICROS_PM_MODEL_ERROR_BUSY;
        }
    }

    zero_bytes(&committed, sizeof(committed));
    clear_wait_outcome(&committed.reaper_wait);
    record = &table->records[process.slot];
    if (record->wait.result_ready) {
        table->records[
            record->wait.pending_child.slot
        ].reap_pending = false;
    }
    clear_wait(&record->wait);

    reaper = table->reaper;
    for (index = 0; index < MICROS_PM_PROCESS_CAPACITY; ++index) {
        struct micros_pm_process_record *child
            = &table->records[index];

        if (
            handles_equal(child->parent, process)
            && (
                child->state == MICROS_PM_PROCESS_RUNNING
                || child->state == MICROS_PM_PROCESS_EXITING
                || child->state == MICROS_PM_PROCESS_ZOMBIE
            )
        ) {
            child->parent = reaper;
            ++committed.reparented_count;
        }
    }

    record->state = MICROS_PM_PROCESS_EXITING;
    record->exit_stage = MICROS_PM_EXIT_STAGE_STOPPED;
    record->exit_kind = exit_kind;
    record->exit_code = exit_code;
    record->exit_transaction = transaction;
    (void)stage_wait_result(
        table,
        reaper,
        &committed.reaper_wait
    );
    *effects = committed;
    return MICROS_PM_MODEL_OK;
}

enum micros_pm_model_error micros_pm_exit_detach(
    struct micros_pm_table *table,
    struct micros_pm_process_handle process,
    uint64_t transaction
)
{
    struct micros_pm_process_record *record;
    const struct micros_pm_process_record *resolved;
    enum micros_pm_model_error error;

    if (table == NULL || transaction == 0) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = micros_pm_table_validate(table);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    error = resolve_record(table, process, &resolved);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    if (
        resolved->state != MICROS_PM_PROCESS_EXITING
        || resolved->exit_stage != MICROS_PM_EXIT_STAGE_STOPPED
    ) {
        return MICROS_PM_MODEL_ERROR_STATE;
    }
    if (resolved->exit_transaction != transaction) {
        return MICROS_PM_MODEL_ERROR_STALE;
    }

    record = &table->records[process.slot];
    record->endpoint = MICROS_ENDPOINT_NONE;
    record->exit_stage = MICROS_PM_EXIT_STAGE_VM_RELEASE_PENDING;
    return MICROS_PM_MODEL_OK;
}

enum micros_pm_model_error micros_pm_exit_release_vm(
    struct micros_pm_table *table,
    struct micros_pm_process_handle process,
    uint64_t transaction
)
{
    struct micros_pm_process_record *record;
    const struct micros_pm_process_record *resolved;
    enum micros_pm_model_error error;

    if (table == NULL || transaction == 0) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = micros_pm_table_validate(table);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    error = resolve_record(table, process, &resolved);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    if (
        resolved->state != MICROS_PM_PROCESS_EXITING
        || resolved->exit_stage
            != MICROS_PM_EXIT_STAGE_VM_RELEASE_PENDING
    ) {
        return MICROS_PM_MODEL_ERROR_STATE;
    }
    if (resolved->exit_transaction != transaction) {
        return MICROS_PM_MODEL_ERROR_STALE;
    }

    record = &table->records[process.slot];
    record->exit_stage
        = MICROS_PM_EXIT_STAGE_KERNEL_PROCESS_EMPTY;
    return MICROS_PM_MODEL_OK;
}

enum micros_pm_model_error micros_pm_exit_release_process(
    struct micros_pm_table *table,
    struct micros_pm_process_handle process,
    uint64_t transaction,
    struct micros_pm_wait_outcome *outcome
)
{
    struct micros_pm_wait_outcome committed;
    struct micros_pm_process_record *record;
    const struct micros_pm_process_record *resolved;
    struct micros_pm_process_handle parent;
    enum micros_pm_model_error error;

    if (table == NULL || outcome == NULL || transaction == 0) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = micros_pm_table_validate(table);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    error = resolve_record(table, process, &resolved);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    if (
        resolved->state != MICROS_PM_PROCESS_EXITING
        || resolved->exit_stage
            != MICROS_PM_EXIT_STAGE_KERNEL_PROCESS_EMPTY
    ) {
        return MICROS_PM_MODEL_ERROR_STATE;
    }
    if (resolved->exit_transaction != transaction) {
        return MICROS_PM_MODEL_ERROR_STALE;
    }

    clear_wait_outcome(&committed);
    record = &table->records[process.slot];
    parent = record->parent;
    record->state = MICROS_PM_PROCESS_ZOMBIE;
    record->exit_stage = MICROS_PM_EXIT_STAGE_NONE;
    record->exit_transaction = 0;
    (void)stage_wait_result(table, parent, &committed);
    *outcome = committed;
    return MICROS_PM_MODEL_OK;
}

enum micros_pm_model_error micros_pm_wait_begin(
    struct micros_pm_table *table,
    struct micros_pm_process_handle parent,
    uint64_t selector,
    uint32_t flags,
    uint64_t reply_token,
    struct micros_pm_wait_outcome *outcome
)
{
    struct micros_pm_wait_outcome committed;
    struct micros_pm_process_record *parent_record;
    struct micros_pm_process_record *child_record;
    struct micros_pm_process_handle child_handle;
    const struct micros_pm_process_record *resolved;
    uint16_t child_slot;
    enum micros_pm_model_error error;

    if (
        table == NULL
        || outcome == NULL
        || reply_token == 0
        || (flags & ~MICROS_PM_WAIT_NOHANG) != 0
    ) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = micros_pm_table_validate(table);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    error = resolve_record(table, parent, &resolved);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    if (resolved->state != MICROS_PM_PROCESS_RUNNING) {
        return MICROS_PM_MODEL_ERROR_STATE;
    }

    clear_wait_outcome(&committed);
    committed.reply_token = reply_token;
    if (resolved->wait.active) {
        committed.action = MICROS_PM_WAIT_ACTION_REPLY;
        committed.result = MICROS_PM_RESULT_BUSY;
        *outcome = committed;
        return MICROS_PM_MODEL_OK;
    }

    parent_record = &table->records[parent.slot];
    if (
        find_lowest_matching_zombie(
            table,
            parent,
            selector,
            &child_slot
        )
    ) {
        child_record = &table->records[child_slot];
        child_handle = record_handle(child_slot, child_record);
        parent_record->wait.active = true;
        parent_record->wait.result_ready = true;
        parent_record->wait.flags = flags;
        parent_record->wait.selector = selector;
        parent_record->wait.reply_token = reply_token;
        parent_record->wait.pending_child = child_handle;
        child_record->reap_pending = true;
        populate_wait_outcome(
            parent_record,
            child_record,
            child_handle,
            &committed
        );
    } else if (!has_matching_child(table, parent, selector)) {
        committed.action = MICROS_PM_WAIT_ACTION_REPLY;
        committed.result = MICROS_PM_RESULT_NO_CHILD;
    } else if ((flags & MICROS_PM_WAIT_NOHANG) != 0) {
        committed.action = MICROS_PM_WAIT_ACTION_REPLY;
    } else {
        parent_record->wait.active = true;
        parent_record->wait.flags = flags;
        parent_record->wait.selector = selector;
        parent_record->wait.reply_token = reply_token;
        committed.action = MICROS_PM_WAIT_ACTION_BLOCKED;
    }
    *outcome = committed;
    return MICROS_PM_MODEL_OK;
}

enum micros_pm_model_error micros_pm_wait_resume(
    struct micros_pm_table *table,
    struct micros_pm_process_handle parent,
    struct micros_pm_wait_outcome *outcome
)
{
    struct micros_pm_wait_outcome committed;
    struct micros_pm_process_record *parent_record;
    const struct micros_pm_process_record *resolved;
    enum micros_pm_model_error error;

    if (table == NULL || outcome == NULL) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = micros_pm_table_validate(table);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    error = resolve_record(table, parent, &resolved);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    if (
        resolved->state != MICROS_PM_PROCESS_RUNNING
        || !resolved->wait.active
        || resolved->wait.result_ready
    ) {
        return MICROS_PM_MODEL_ERROR_STATE;
    }

    clear_wait_outcome(&committed);
    parent_record = &table->records[parent.slot];
    committed.action = MICROS_PM_WAIT_ACTION_BLOCKED;
    committed.reply_token = parent_record->wait.reply_token;
    (void)stage_wait_result(table, parent, &committed);
    *outcome = committed;
    return MICROS_PM_MODEL_OK;
}

enum micros_pm_model_error micros_pm_wait_reply_complete(
    struct micros_pm_table *table,
    struct micros_pm_process_handle parent,
    uint64_t reply_token,
    bool accepted
)
{
    struct micros_pm_process_record *parent_record;
    struct micros_pm_process_record *child_record;
    const struct micros_pm_process_record *resolved;
    enum micros_pm_model_error error;

    if (table == NULL || reply_token == 0) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = micros_pm_table_validate(table);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    error = resolve_record(table, parent, &resolved);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    if (
        resolved->state != MICROS_PM_PROCESS_RUNNING
        || !resolved->wait.active
        || !resolved->wait.result_ready
    ) {
        return MICROS_PM_MODEL_ERROR_STATE;
    }
    if (resolved->wait.reply_token != reply_token) {
        return MICROS_PM_MODEL_ERROR_STALE;
    }

    parent_record = &table->records[parent.slot];
    child_record = &table->records[
        parent_record->wait.pending_child.slot
    ];
    if (!accepted) {
        child_record->reap_pending = false;
        parent_record->wait.result_ready = false;
        zero_bytes(
            &parent_record->wait.pending_child,
            sizeof(parent_record->wait.pending_child)
        );
        return MICROS_PM_MODEL_OK;
    }

    clear_wait(&parent_record->wait);
    release_record(child_record);
    return MICROS_PM_MODEL_OK;
}

enum micros_pm_model_error micros_pm_find_running_by_endpoint(
    const struct micros_pm_table *table,
    micros_endpoint_t endpoint,
    struct micros_pm_process_handle *process,
    uint64_t *pid
)
{
    struct micros_pm_process_handle found;
    uint64_t found_pid;
    uint16_t index;
    enum micros_pm_model_error error;

    if (
        table == NULL
        || process == NULL
        || pid == NULL
        || !endpoint_is_concrete(endpoint)
    ) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = micros_pm_table_validate(table);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }

    for (index = 0; index < MICROS_PM_PROCESS_CAPACITY; ++index) {
        const struct micros_pm_process_record *record
            = &table->records[index];

        if (
            record->state == MICROS_PM_PROCESS_RUNNING
            && record->endpoint == endpoint
        ) {
            found = record_handle(index, record);
            found_pid = record->pid;
            *process = found;
            *pid = found_pid;
            return MICROS_PM_MODEL_OK;
        }
    }
    return MICROS_PM_MODEL_ERROR_STALE;
}

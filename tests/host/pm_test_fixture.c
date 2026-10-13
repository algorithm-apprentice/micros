#include "tests/host/pm_test_internal.h"

void pm_test_write_u32_le(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

void pm_test_write_u64_le(uint8_t *bytes, uint64_t value)
{
    size_t index;

    for (index = 0; index < 8; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}

uint32_t pm_test_read_u32_le(const uint8_t *bytes)
{
    return (
        (uint32_t)bytes[0]
        | (uint32_t)bytes[1] << 8
        | (uint32_t)bytes[2] << 16
        | (uint32_t)bytes[3] << 24
    );
}

uint64_t pm_test_read_u64_le(const uint8_t *bytes)
{
    uint64_t value = 0;
    size_t index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8);
    }
    return value;
}

bool pm_test_handles_equal(
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

bool pm_test_handle_is_none(struct micros_pm_process_handle handle)
{
    return (
        handle.slot == 0
        && handle.reserved == 0
        && handle.generation == 0
    );
}

bool pm_test_bytes_are_zero(const void *storage, size_t size)
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

struct micros_ipc_message pm_test_exit_request(int32_t exit_code)
{
    struct micros_ipc_message message;

    memset(&message, 0, sizeof(message));
    message.source = UINT32_C(0x00001001);
    message.type = MICROS_PM_MESSAGE_EXIT;
    message.reply_token = 7;
    pm_test_write_u32_le(&message.payload[0], MICROS_PM_PROTOCOL_VERSION);
    pm_test_write_u32_le(&message.payload[8], (uint32_t)exit_code);
    return message;
}

struct micros_ipc_message pm_test_wait_request(
    uint32_t flags,
    uint64_t child_pid
)
{
    struct micros_ipc_message message;

    memset(&message, 0, sizeof(message));
    message.source = UINT32_C(0x00001001);
    message.type = MICROS_PM_MESSAGE_WAIT;
    message.reply_token = 9;
    pm_test_write_u32_le(&message.payload[0], MICROS_PM_PROTOCOL_VERSION);
    pm_test_write_u32_le(&message.payload[4], flags);
    pm_test_write_u64_le(&message.payload[8], child_pid);
    return message;
}

struct micros_ipc_message pm_test_spawn_request(
    uint32_t grant,
    uint32_t request_size
)
{
    struct micros_ipc_message message;

    memset(&message, 0, sizeof(message));
    message.source = UINT32_C(0x00001001);
    message.type = MICROS_PM_MESSAGE_SPAWN;
    message.reply_token = 11;
    pm_test_write_u32_le(&message.payload[0], MICROS_PM_PROTOCOL_VERSION);
    pm_test_write_u32_le(&message.payload[8], grant);
    pm_test_write_u32_le(&message.payload[12], request_size);
    return message;
}

struct micros_ipc_message pm_test_sealed_event(void)
{
    struct micros_ipc_message message;

    memset(&message, 0, sizeof(message));
    message.source = MICROS_ENDPOINT_NONE;
    message.type = MICROS_IPC_TYPE_KERNEL_NOTIFICATION;
    pm_test_write_u64_le(
        &message.payload[0],
        MICROS_KERNEL_EVENT_BOOTSTRAP_SEALED
    );
    return message;
}

bool pm_test_complete_spawn(
    struct micros_pm_table *table,
    struct micros_pm_process_handle child,
    micros_endpoint_t endpoint,
    uint64_t expected_reply_token
)
{
    uint64_t reply_token = UINT64_MAX;

    return (
        micros_pm_spawn_reserve_kernel(table, child, 11)
            == MICROS_PM_MODEL_OK
        && micros_pm_spawn_prepare_image(table, child, 12)
            == MICROS_PM_MODEL_OK
        && micros_pm_spawn_freeze_mappings(table, child, 13, 14)
            == MICROS_PM_MODEL_OK
        && micros_pm_spawn_prepare_descriptors(table, child, 15)
            == MICROS_PM_MODEL_OK
        && micros_pm_spawn_prepare_execution(
            table,
            child,
            16,
            endpoint
        ) == MICROS_PM_MODEL_OK
        && micros_pm_spawn_commit_hidden(table, child, 17)
            == MICROS_PM_MODEL_OK
        && micros_pm_spawn_activate(
            table,
            child,
            endpoint,
            &reply_token
        )
            == MICROS_PM_MODEL_OK
        && reply_token == expected_reply_token
    );
}

bool pm_test_initialize_running_init(
    struct micros_pm_table *table,
    struct micros_pm_process_handle *init
)
{
    struct micros_ipc_message event = pm_test_sealed_event();
    uint64_t pid = 0;

    memset(table, 0, sizeof(*table));
    return (
        micros_pm_table_initialize(table) == MICROS_PM_MODEL_OK
        && micros_pm_enable_runtime(table, &event)
            == MICROS_PM_MODEL_OK
        && micros_pm_begin_init(table, init, &pid)
            == MICROS_PM_MODEL_OK
        && pid == 1
        && pm_test_complete_spawn(
            table,
            *init,
            UINT32_C(0x00001000),
            0
        )
        && micros_pm_table_validate(table) == MICROS_PM_MODEL_OK
    );
}

uint32_t pm_test_expected_resources(enum micros_pm_spawn_stage stage)
{
    uint32_t resources = 0;

    if (stage >= MICROS_PM_SPAWN_KERNEL_PROCESS_RESERVED) {
        resources |= MICROS_PM_RESOURCE_KERNEL_PROCESS;
    }
    if (stage >= MICROS_PM_SPAWN_VFS_IMAGE_PREPARED) {
        resources |= MICROS_PM_RESOURCE_VFS_IMAGE;
    }
    if (stage >= MICROS_PM_SPAWN_VM_MAPPINGS_FROZEN) {
        resources |= MICROS_PM_RESOURCE_VM_MAPPINGS;
    }
    if (stage >= MICROS_PM_SPAWN_VFS_DESCRIPTORS_PREPARED) {
        resources |= MICROS_PM_RESOURCE_VFS_DESCRIPTORS;
    }
    if (stage >= MICROS_PM_SPAWN_KERNEL_EXECUTION_PREPARED) {
        resources |= MICROS_PM_RESOURCE_KERNEL_EXECUTION;
    }
    if (stage >= MICROS_PM_SPAWN_PM_VFS_COMMITTED) {
        resources |= MICROS_PM_RESOURCE_PM_VFS_COMMIT;
    }
    return resources;
}

bool pm_test_advance_to_stage(
    struct micros_pm_table *table,
    struct micros_pm_process_handle child,
    enum micros_pm_spawn_stage stage
)
{
    if (
        stage >= MICROS_PM_SPAWN_KERNEL_PROCESS_RESERVED
        && micros_pm_spawn_reserve_kernel(table, child, 21)
            != MICROS_PM_MODEL_OK
    ) {
        return false;
    }
    if (
        stage >= MICROS_PM_SPAWN_VFS_IMAGE_PREPARED
        && micros_pm_spawn_prepare_image(table, child, 22)
            != MICROS_PM_MODEL_OK
    ) {
        return false;
    }
    if (
        stage >= MICROS_PM_SPAWN_VM_MAPPINGS_FROZEN
        && micros_pm_spawn_freeze_mappings(table, child, 23, 24)
            != MICROS_PM_MODEL_OK
    ) {
        return false;
    }
    if (
        stage >= MICROS_PM_SPAWN_VFS_DESCRIPTORS_PREPARED
        && micros_pm_spawn_prepare_descriptors(table, child, 25)
            != MICROS_PM_MODEL_OK
    ) {
        return false;
    }
    if (
        stage >= MICROS_PM_SPAWN_KERNEL_EXECUTION_PREPARED
        && micros_pm_spawn_prepare_execution(
            table,
            child,
            26,
            UINT32_C(0x00003001)
        ) != MICROS_PM_MODEL_OK
    ) {
        return false;
    }
    return (
        stage < MICROS_PM_SPAWN_PM_VFS_COMMITTED
        || micros_pm_spawn_commit_hidden(table, child, 27)
            == MICROS_PM_MODEL_OK
    );
}

enum micros_pm_spawn_rollback_resource
pm_test_rollback_resource_for_stage(enum micros_pm_spawn_stage stage)
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

uint32_t pm_test_rollback_resource_bit(
    enum micros_pm_spawn_rollback_resource resource
)
{
    switch (resource) {
    case MICROS_PM_ROLLBACK_PM_VFS_COMMIT:
        return MICROS_PM_RESOURCE_PM_VFS_COMMIT;
    case MICROS_PM_ROLLBACK_KERNEL_EXECUTION:
        return MICROS_PM_RESOURCE_KERNEL_EXECUTION;
    case MICROS_PM_ROLLBACK_VFS_DESCRIPTORS:
        return MICROS_PM_RESOURCE_VFS_DESCRIPTORS;
    case MICROS_PM_ROLLBACK_VM_MAPPINGS:
        return MICROS_PM_RESOURCE_VM_MAPPINGS;
    case MICROS_PM_ROLLBACK_VFS_IMAGE:
        return MICROS_PM_RESOURCE_VFS_IMAGE;
    case MICROS_PM_ROLLBACK_KERNEL_PROCESS:
        return MICROS_PM_RESOURCE_KERNEL_PROCESS;
    case MICROS_PM_ROLLBACK_NONE:
    default:
        return 0;
    }
}

bool pm_test_rollback_spawn(
    struct micros_pm_table *table,
    struct micros_pm_process_handle child,
    enum micros_pm_spawn_stage stage,
    uint64_t parent_reply_token,
    bool parent_lost,
    uint32_t *released_resources
)
{
    struct micros_pm_spawn_rollback_action action;
    enum micros_pm_spawn_stage current_stage = stage;
    uint32_t released = 0;

    if (
        micros_pm_spawn_rollback_begin(
            table,
            child,
            stage,
            parent_reply_token,
            parent_lost,
            &action
        ) != MICROS_PM_MODEL_OK
    ) {
        return false;
    }
    while (!action.complete) {
        if (
            action.expected_stage != current_stage
            || action.resource
                != pm_test_rollback_resource_for_stage(current_stage)
        ) {
            return false;
        }
        released |= pm_test_rollback_resource_bit(action.resource);
        if (
            micros_pm_spawn_rollback_ack(
                table,
                child,
                action.expected_stage,
                action.resource,
                action.token,
                action.secondary_token,
                action.endpoint,
                &action
            ) != MICROS_PM_MODEL_OK
        ) {
            return false;
        }
        current_stage = (
            enum micros_pm_spawn_stage
        )((unsigned int)current_stage - 1U);
    }
    if (
        action.resource != MICROS_PM_ROLLBACK_NONE
        || action.parent_lost != parent_lost
        || action.reply_token
            != (parent_lost ? 0 : parent_reply_token)
    ) {
        return false;
    }
    *released_resources = released;
    return true;
}

bool pm_test_spawn_running_child(
    struct micros_pm_table *table,
    struct micros_pm_process_handle parent,
    micros_endpoint_t endpoint,
    struct micros_pm_process_handle *child,
    uint64_t *pid
)
{
    uint64_t reply_token = UINT64_C(0x400000) + endpoint;

    return (
        micros_pm_begin_spawn(
            table,
            parent,
            reply_token,
            child,
            pid
        )
            == MICROS_PM_MODEL_OK
        && pm_test_complete_spawn(table, *child, endpoint, reply_token)
    );
}

bool pm_test_complete_exit(
    struct micros_pm_table *table,
    struct micros_pm_process_handle child,
    uint64_t transaction,
    int32_t exit_code,
    struct micros_pm_wait_outcomes *outcomes
)
{
    struct micros_pm_exit_effects effects;

    return (
        micros_pm_exit_begin(
            table,
            child,
            transaction,
            MICROS_PM_EXIT_NORMAL,
            exit_code,
            &effects
        ) == MICROS_PM_MODEL_OK
        && !effects.aborted_spawn
        && effects.reparented_count == 0
        && micros_pm_exit_detach(table, child, transaction)
            == MICROS_PM_MODEL_OK
        && micros_pm_exit_release_vm(table, child, transaction)
            == MICROS_PM_MODEL_OK
        && micros_pm_exit_release_process(
            table,
            child,
            transaction,
            outcomes
        ) == MICROS_PM_MODEL_OK
    );
}

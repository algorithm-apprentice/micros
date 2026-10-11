#include "servers/pm/pm_core.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

bool micros_pm_control_test_run(void);
bool micros_pm_control_execution_test_run(void);

#define EXPECT_TRUE(expression) \
    do { \
        if (!(expression)) { \
            fprintf( \
                stderr, \
                "%s:%d: expected %s\n", \
                __FILE__, \
                __LINE__, \
                #expression \
            ); \
            return false; \
        } \
    } while (false)

static bool spawn_running_child(
    struct micros_pm_table *table,
    struct micros_pm_process_handle parent,
    micros_endpoint_t endpoint,
    struct micros_pm_process_handle *child,
    uint64_t *pid
);

static bool complete_exit(
    struct micros_pm_table *table,
    struct micros_pm_process_handle child,
    uint64_t transaction,
    int32_t exit_code,
    struct micros_pm_wait_outcomes *outcomes
);

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

static bool pm_handles_equal(
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

static bool pm_handle_is_none(struct micros_pm_process_handle handle)
{
    return (
        handle.slot == 0
        && handle.reserved == 0
        && handle.generation == 0
    );
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

static struct micros_ipc_message exit_request(int32_t exit_code)
{
    struct micros_ipc_message message;

    memset(&message, 0, sizeof(message));
    message.source = UINT32_C(0x00001001);
    message.type = MICROS_PM_MESSAGE_EXIT;
    message.reply_token = 7;
    write_u32_le(&message.payload[0], MICROS_PM_PROTOCOL_VERSION);
    write_u32_le(&message.payload[8], (uint32_t)exit_code);
    return message;
}

static struct micros_ipc_message wait_request(
    uint32_t flags,
    uint64_t child_pid
)
{
    struct micros_ipc_message message;

    memset(&message, 0, sizeof(message));
    message.source = UINT32_C(0x00001001);
    message.type = MICROS_PM_MESSAGE_WAIT;
    message.reply_token = 9;
    write_u32_le(&message.payload[0], MICROS_PM_PROTOCOL_VERSION);
    write_u32_le(&message.payload[4], flags);
    write_u64_le(&message.payload[8], child_pid);
    return message;
}

static struct micros_ipc_message spawn_request(
    uint32_t grant,
    uint32_t request_size
)
{
    struct micros_ipc_message message;

    memset(&message, 0, sizeof(message));
    message.source = UINT32_C(0x00001001);
    message.type = MICROS_PM_MESSAGE_SPAWN;
    message.reply_token = 11;
    write_u32_le(&message.payload[0], MICROS_PM_PROTOCOL_VERSION);
    write_u32_le(&message.payload[8], grant);
    write_u32_le(&message.payload[12], request_size);
    return message;
}

static struct micros_ipc_message sealed_event(void)
{
    struct micros_ipc_message message;

    memset(&message, 0, sizeof(message));
    message.source = MICROS_ENDPOINT_NONE;
    message.type = MICROS_IPC_TYPE_KERNEL_NOTIFICATION;
    write_u64_le(
        &message.payload[0],
        MICROS_KERNEL_EVENT_BOOTSTRAP_SEALED
    );
    return message;
}

static bool test_protocol_contract(void)
{
    struct micros_ipc_message message = exit_request(-19);
    struct micros_pm_request request;
    struct micros_pm_request sentinel;
    struct micros_ipc_message result;
    struct micros_ipc_message result_sentinel;
    size_t index;

    EXPECT_TRUE(
        MICROS_PM_PROCESS_CAPACITY == 64
        && MICROS_PM_PROTOCOL_VERSION == 1
        && sizeof(struct micros_pm_reservation_result) == 32
    );
    EXPECT_TRUE(
        micros_pm_decode_request(&message, &request)
            == MICROS_PM_PROTOCOL_OK
        && request.type == MICROS_PM_MESSAGE_EXIT
        && request.source == message.source
        && request.reply_token == message.reply_token
        && request.flags == 0
        && request.exit_code == -19
        && request.child_pid == 0
    );
    message = wait_request(MICROS_PM_WAIT_NOHANG, UINT64_C(45));
    EXPECT_TRUE(
        micros_pm_decode_request(&message, &request)
            == MICROS_PM_PROTOCOL_OK
        && request.type == MICROS_PM_MESSAGE_WAIT
        && request.flags == MICROS_PM_WAIT_NOHANG
        && request.child_pid == UINT64_C(45)
        && request.exit_code == 0
    );
    message = spawn_request(UINT32_C(0x1234), 256);
    EXPECT_TRUE(
        micros_pm_decode_request(&message, &request)
            == MICROS_PM_PROTOCOL_OK
        && request.type == MICROS_PM_MESSAGE_SPAWN
        && request.flags == 0
        && request.grant == UINT32_C(0x1234)
        && request.request_size == 256
        && request.child_pid == 0
        && request.exit_code == 0
    );
    message = spawn_request(UINT32_MAX, 256);
    EXPECT_TRUE(
        micros_pm_decode_request(&message, &request)
            == MICROS_PM_PROTOCOL_OK
        && request.grant == UINT32_MAX
    );
    message = spawn_request(1, 4096);
    EXPECT_TRUE(
        micros_pm_decode_request(&message, &request)
            == MICROS_PM_PROTOCOL_OK
        && request.request_size == 4096
    );

    memset(&sentinel, 0xa5, sizeof(sentinel));
    request = sentinel;
    message.type = UINT32_C(0x000100ff);
    EXPECT_TRUE(
        micros_pm_decode_request(&message, &request)
            == MICROS_PM_PROTOCOL_BAD_TYPE
        && memcmp(&request, &sentinel, sizeof(request)) == 0
    );
    message = wait_request(0, 0);
    write_u32_le(&message.payload[0], 2);
    request = sentinel;
    EXPECT_TRUE(
        micros_pm_decode_request(&message, &request)
            == MICROS_PM_PROTOCOL_BAD_VERSION
        && memcmp(&request, &sentinel, sizeof(request)) == 0
    );
    message = wait_request(UINT32_C(0x2), 0);
    request = sentinel;
    EXPECT_TRUE(
        micros_pm_decode_request(&message, &request)
            == MICROS_PM_PROTOCOL_MALFORMED
        && memcmp(&request, &sentinel, sizeof(request)) == 0
    );
    message = spawn_request(1, 255);
    request = sentinel;
    EXPECT_TRUE(
        micros_pm_decode_request(&message, &request)
            == MICROS_PM_PROTOCOL_MALFORMED
        && memcmp(&request, &sentinel, sizeof(request)) == 0
    );
    message = spawn_request(1, 4097);
    request = sentinel;
    EXPECT_TRUE(
        micros_pm_decode_request(&message, &request)
            == MICROS_PM_PROTOCOL_MALFORMED
        && memcmp(&request, &sentinel, sizeof(request)) == 0
    );
    message = spawn_request(1, 256);
    write_u32_le(&message.payload[4], 1);
    request = sentinel;
    EXPECT_TRUE(
        micros_pm_decode_request(&message, &request)
            == MICROS_PM_PROTOCOL_MALFORMED
        && memcmp(&request, &sentinel, sizeof(request)) == 0
    );
    for (index = 0; index < 32; ++index) {
        message = spawn_request(1, 256);
        write_u32_le(
            &message.payload[4],
            UINT32_C(1) << index
        );
        request = sentinel;
        EXPECT_TRUE(
            micros_pm_decode_request(&message, &request)
                == MICROS_PM_PROTOCOL_MALFORMED
            && memcmp(&request, &sentinel, sizeof(request)) == 0
        );
    }
    for (index = 16; index < sizeof(message.payload); ++index) {
        message = spawn_request(1, 256);
        message.payload[index] = 1;
        request = sentinel;
        EXPECT_TRUE(
            micros_pm_decode_request(&message, &request)
                == MICROS_PM_PROTOCOL_MALFORMED
            && memcmp(&request, &sentinel, sizeof(request)) == 0
        );
    }
    message = wait_request(0, 0);
    message.reply_token = 0;
    request = sentinel;
    EXPECT_TRUE(
        micros_pm_decode_request(&message, &request)
            == MICROS_PM_PROTOCOL_INVARIANT
        && memcmp(&request, &sentinel, sizeof(request)) == 0
    );

    memset(&result_sentinel, 0xa5, sizeof(result_sentinel));
    result = result_sentinel;
    EXPECT_TRUE(
        micros_pm_build_result(
            MICROS_PM_MESSAGE_WAIT,
            MICROS_PM_RESULT_OK,
            UINT64_C(0x0102030405060708),
            MICROS_PM_EXIT_NORMAL,
            -23,
            &result
        ) == MICROS_PM_MODEL_OK
        && result.source == 0
        && result.type == MICROS_PM_MESSAGE_RESULT
        && result.reply_token == 0
        && read_u32_le(&result.payload[0])
            == MICROS_PM_PROTOCOL_VERSION
        && read_u32_le(&result.payload[4])
            == MICROS_PM_MESSAGE_WAIT
        && (int32_t)read_u32_le(&result.payload[8])
            == MICROS_PM_RESULT_OK
        && read_u32_le(&result.payload[12]) == 0
        && read_u64_le(&result.payload[16])
            == UINT64_C(0x0102030405060708)
        && read_u32_le(&result.payload[24])
            == MICROS_PM_EXIT_NORMAL
        && (int32_t)read_u32_le(&result.payload[28]) == -23
    );
    result = result_sentinel;
    EXPECT_TRUE(
        micros_pm_build_result(
            MICROS_PM_MESSAGE_WAIT,
            MICROS_PM_RESULT_NO_CHILD,
            1,
            MICROS_PM_EXIT_NORMAL,
            0,
            &result
        ) == MICROS_PM_MODEL_ERROR_ARGUMENT
        && memcmp(&result, &result_sentinel, sizeof(result)) == 0
    );
    result = result_sentinel;
    EXPECT_TRUE(
        micros_pm_build_result(
            MICROS_PM_MESSAGE_RESULT,
            MICROS_PM_RESULT_BAD_TYPE,
            0,
            MICROS_PM_EXIT_NONE,
            0,
            &result
        ) == MICROS_PM_MODEL_OK
        && result.type == MICROS_PM_MESSAGE_RESULT
        && read_u32_le(&result.payload[4])
            == MICROS_PM_MESSAGE_RESULT
        && (int32_t)read_u32_le(&result.payload[8])
            == MICROS_PM_RESULT_BAD_TYPE
    );
    result = result_sentinel;
    EXPECT_TRUE(
        micros_pm_build_result(
            MICROS_PM_MESSAGE_SPAWN,
            MICROS_PM_RESULT_OK,
            UINT64_C(0x0102030405060708),
            MICROS_PM_EXIT_NONE,
            0,
            &result
        ) == MICROS_PM_MODEL_OK
        && read_u32_le(&result.payload[4])
            == MICROS_PM_MESSAGE_SPAWN
        && (int32_t)read_u32_le(&result.payload[8])
            == MICROS_PM_RESULT_OK
        && read_u64_le(&result.payload[16])
            == UINT64_C(0x0102030405060708)
        && read_u32_le(&result.payload[24])
            == MICROS_PM_EXIT_NONE
        && read_u32_le(&result.payload[28]) == 0
    );
    result = result_sentinel;
    EXPECT_TRUE(
        micros_pm_build_result(
            MICROS_PM_MESSAGE_SPAWN,
            MICROS_PM_RESULT_OK,
            0,
            MICROS_PM_EXIT_NONE,
            0,
            &result
        ) == MICROS_PM_MODEL_ERROR_ARGUMENT
        && memcmp(&result, &result_sentinel, sizeof(result)) == 0
    );
    result = result_sentinel;
    EXPECT_TRUE(
        micros_pm_build_result(
            MICROS_PM_MESSAGE_SPAWN,
            MICROS_PM_RESULT_DESCRIPTOR,
            0,
            MICROS_PM_EXIT_NONE,
            0,
            &result
        ) == MICROS_PM_MODEL_OK
        && (int32_t)read_u32_le(&result.payload[8])
            == MICROS_PM_RESULT_DESCRIPTOR
        && bytes_are_zero(&result.payload[12], 36)
    );
    result = result_sentinel;
    EXPECT_TRUE(
        micros_pm_build_result(
            MICROS_PM_MESSAGE_SPAWN,
            MICROS_PM_RESULT_DESCRIPTOR,
            1,
            MICROS_PM_EXIT_NONE,
            0,
            &result
        ) == MICROS_PM_MODEL_ERROR_ARGUMENT
        && memcmp(&result, &result_sentinel, sizeof(result)) == 0
    );
    return true;
}

static bool complete_spawn(
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

static bool initialize_running_init(
    struct micros_pm_table *table,
    struct micros_pm_process_handle *init
)
{
    struct micros_ipc_message event = sealed_event();
    uint64_t pid = 0;

    memset(table, 0, sizeof(*table));
    return (
        micros_pm_table_initialize(table) == MICROS_PM_MODEL_OK
        && micros_pm_enable_runtime(table, &event)
            == MICROS_PM_MODEL_OK
        && micros_pm_begin_init(table, init, &pid)
            == MICROS_PM_MODEL_OK
        && pid == 1
        && complete_spawn(
            table,
            *init,
            UINT32_C(0x00001000),
            0
        )
        && micros_pm_table_validate(table) == MICROS_PM_MODEL_OK
    );
}

static bool test_runtime_gate_and_spawn(void)
{
    struct micros_pm_table table;
    struct micros_pm_process_handle init;
    struct micros_pm_process_handle child;
    const struct micros_pm_process_record *record;
    struct micros_ipc_message event = sealed_event();
    uint64_t pid = 0;

    memset(&table, 0, sizeof(table));
    EXPECT_TRUE(
        micros_pm_table_initialize(&table) == MICROS_PM_MODEL_OK
        && micros_pm_table_validate(&table) == MICROS_PM_MODEL_OK
        && micros_pm_begin_init(&table, &init, &pid)
            == MICROS_PM_MODEL_ERROR_STATE
    );
    event.source = UINT32_C(1);
    EXPECT_TRUE(
        micros_pm_enable_runtime(&table, &event)
            == MICROS_PM_MODEL_ERROR_ARGUMENT
        && !table.runtime_enabled
    );
    event = sealed_event();
    EXPECT_TRUE(
        micros_pm_enable_runtime(&table, &event)
            == MICROS_PM_MODEL_OK
        && table.runtime_enabled
        && micros_pm_enable_runtime(&table, &event)
            == MICROS_PM_MODEL_ERROR_STATE
    );
    EXPECT_TRUE(
        micros_pm_begin_init(&table, &init, &pid)
            == MICROS_PM_MODEL_OK
        && pid == 1
        && micros_pm_process_resolve(&table, init, &record)
            == MICROS_PM_MODEL_OK
        && record->state == MICROS_PM_PROCESS_SPAWNING
        && record->spawn_stage
            == MICROS_PM_SPAWN_PM_RECORD_RESERVED
        && record->endpoint == MICROS_ENDPOINT_NONE
        && table.reaper.generation == 0
    );
    EXPECT_TRUE(
        complete_spawn(&table, init, UINT32_C(0x00001000), 0)
        && micros_pm_process_resolve(&table, init, &record)
            == MICROS_PM_MODEL_OK
        && record->state == MICROS_PM_PROCESS_RUNNING
        && record->reaper
        && record->endpoint == UINT32_C(0x00001000)
        && table.reaper.slot == init.slot
        && table.reaper.generation == init.generation
    );
    EXPECT_TRUE(
        micros_pm_begin_spawn(&table, init, 101, &child, &pid)
            == MICROS_PM_MODEL_OK
        && pid == 2
        && micros_pm_process_resolve(&table, child, &record)
            == MICROS_PM_MODEL_OK
        && record->spawn.parent_reply_token == 101
        && micros_pm_spawn_prepare_image(&table, child, 1)
            == MICROS_PM_MODEL_ERROR_STATE
        && complete_spawn(
            &table,
            child,
            UINT32_C(0x00002001),
            101
        )
        && micros_pm_table_validate(&table) == MICROS_PM_MODEL_OK
    );
    return true;
}

static uint32_t expected_resources(enum micros_pm_spawn_stage stage)
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

static bool advance_to_stage(
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

static uint32_t rollback_resource_bit(
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

static bool rollback_spawn(
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
                != rollback_resource_for_stage(current_stage)
        ) {
            return false;
        }
        released |= rollback_resource_bit(action.resource);
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

static bool test_reverse_rollback_and_identity(void)
{
    enum micros_pm_spawn_stage stage;

    for (
        stage = MICROS_PM_SPAWN_PM_RECORD_RESERVED;
        stage <= MICROS_PM_SPAWN_PM_VFS_COMMITTED;
        ++stage
    ) {
        struct micros_pm_table table;
        struct micros_pm_process_handle init;
        struct micros_pm_process_handle child;
        struct micros_pm_process_handle next;
        const struct micros_pm_process_record *record;
        uint32_t released = UINT32_MAX;
        uint64_t pid;

        EXPECT_TRUE(initialize_running_init(&table, &init));
        EXPECT_TRUE(
            micros_pm_begin_spawn(&table, init, 100, &child, &pid)
                == MICROS_PM_MODEL_OK
            && pid == 2
            && advance_to_stage(&table, child, stage)
            && rollback_spawn(
                &table,
                child,
                stage,
                100,
                false,
                &released
            )
            && released == expected_resources(stage)
            && micros_pm_process_resolve(&table, child, &record)
                == MICROS_PM_MODEL_ERROR_STALE
            && micros_pm_begin_spawn(
                &table,
                init,
                101,
                &next,
                &pid
            )
                == MICROS_PM_MODEL_OK
            && pid == 3
            && next.slot == child.slot
            && next.generation == child.generation + 1
            && micros_pm_table_validate(&table)
                == MICROS_PM_MODEL_OK
        );
    }
    return true;
}

static bool test_rollback_acknowledgment_identity(void)
{
    struct micros_pm_table table;
    struct micros_pm_table snapshot;
    struct micros_pm_process_handle init;
    struct micros_pm_process_handle child;
    struct micros_pm_process_handle next_child;
    const struct micros_pm_process_record *record;
    struct micros_pm_spawn_rollback_action action;
    struct micros_pm_spawn_rollback_action output;
    struct micros_pm_spawn_rollback_action output_sentinel;
    enum micros_pm_spawn_stage current_stage;
    uint32_t released = 0;
    uint64_t pid;

    EXPECT_TRUE(
        initialize_running_init(&table, &init)
        && micros_pm_begin_spawn(
            &table,
            init,
            300,
            &child,
            &pid
        ) == MICROS_PM_MODEL_OK
        && advance_to_stage(
            &table,
            child,
            MICROS_PM_SPAWN_PM_VFS_COMMITTED
        )
    );

    snapshot = table;
    memset(&output_sentinel, 0xa5, sizeof(output_sentinel));
    output = output_sentinel;
    EXPECT_TRUE(
        micros_pm_spawn_rollback_begin(
            &table,
            child,
            MICROS_PM_SPAWN_PM_VFS_COMMITTED,
            301,
            false,
            &output
        ) == MICROS_PM_MODEL_ERROR_STALE
        && memcmp(&output, &output_sentinel, sizeof(output)) == 0
        && memcmp(&table, &snapshot, sizeof(table)) == 0
        && micros_pm_spawn_rollback_begin(
            &table,
            child,
            MICROS_PM_SPAWN_PM_VFS_COMMITTED,
            300,
            false,
            &action
        ) == MICROS_PM_MODEL_OK
        && action.resource == MICROS_PM_ROLLBACK_PM_VFS_COMMIT
        && action.expected_stage
            == MICROS_PM_SPAWN_PM_VFS_COMMITTED
        && action.token == 27
        && !action.complete
    );

    snapshot = table;
    output = output_sentinel;
    EXPECT_TRUE(
        micros_pm_spawn_rollback_ack(
            &table,
            child,
            action.expected_stage,
            action.resource,
            action.token + 1,
            action.secondary_token,
            action.endpoint,
            &output
        ) == MICROS_PM_MODEL_ERROR_STALE
        && memcmp(&output, &output_sentinel, sizeof(output)) == 0
        && memcmp(&table, &snapshot, sizeof(table)) == 0
        && micros_pm_process_resolve(&table, child, &record)
            == MICROS_PM_MODEL_OK
        && record->spawn.rollback_active
        && micros_pm_begin_spawn(
            &table,
            init,
            302,
            &next_child,
            &pid
        ) == MICROS_PM_MODEL_ERROR_STATE
    );

    current_stage = MICROS_PM_SPAWN_PM_VFS_COMMITTED;
    while (!action.complete) {
        EXPECT_TRUE(
            action.expected_stage == current_stage
            && action.resource
                == rollback_resource_for_stage(current_stage)
        );
        released |= rollback_resource_bit(action.resource);
        EXPECT_TRUE(
            micros_pm_spawn_rollback_ack(
                &table,
                child,
                action.expected_stage,
                action.resource,
                action.token,
                action.secondary_token,
                action.endpoint,
                &action
            ) == MICROS_PM_MODEL_OK
        );
        current_stage = (
            enum micros_pm_spawn_stage
        )((unsigned int)current_stage - 1U);
        if (!action.complete) {
            EXPECT_TRUE(
                micros_pm_process_resolve(&table, child, &record)
                    == MICROS_PM_MODEL_OK
                && record->spawn.rollback_active
            );
        }
    }
    EXPECT_TRUE(
        released
            == expected_resources(MICROS_PM_SPAWN_PM_VFS_COMMITTED)
        && action.reply_token == 300
        && !action.parent_lost
        && micros_pm_process_resolve(&table, child, &record)
            == MICROS_PM_MODEL_ERROR_STALE
        && micros_pm_table_validate(&table) == MICROS_PM_MODEL_OK
    );
    return true;
}

static bool test_generation_and_pid_exhaustion(void)
{
    struct micros_pm_table table;
    struct micros_pm_process_handle init;
    struct micros_pm_process_handle child;
    const struct micros_pm_process_record *record;
    uint32_t released;
    uint64_t pid;

    EXPECT_TRUE(initialize_running_init(&table, &init));
    table.last_pid = UINT64_MAX - 1;
    EXPECT_TRUE(
        micros_pm_begin_spawn(&table, init, 200, &child, &pid)
            == MICROS_PM_MODEL_OK
        && pid == UINT64_MAX
        && table.pid_exhausted
        && rollback_spawn(
            &table,
            child,
            MICROS_PM_SPAWN_PM_RECORD_RESERVED,
            200,
            false,
            &released
        )
        && micros_pm_begin_spawn(
            &table,
            init,
            201,
            &child,
            &pid
        )
            == MICROS_PM_MODEL_ERROR_CAPACITY
    );

    EXPECT_TRUE(initialize_running_init(&table, &init));
    table.records[1].generation = UINT32_MAX - 1;
    EXPECT_TRUE(
        micros_pm_begin_spawn(&table, init, 202, &child, &pid)
            == MICROS_PM_MODEL_OK
        && child.slot == 1
        && child.generation == UINT32_MAX
        && rollback_spawn(
            &table,
            child,
            MICROS_PM_SPAWN_PM_RECORD_RESERVED,
            202,
            false,
            &released
        )
        && table.records[1].state
            == MICROS_PM_PROCESS_QUARANTINED
        && micros_pm_process_resolve(&table, child, &record)
            == MICROS_PM_MODEL_ERROR_STALE
        && micros_pm_table_validate(&table)
            == MICROS_PM_MODEL_OK
    );
    return true;
}

static bool test_failed_init_is_terminal(void)
{
    struct micros_pm_table table;
    struct micros_pm_process_handle init;
    struct micros_ipc_message event = sealed_event();
    uint32_t released = UINT32_MAX;
    uint64_t pid = 0;

    memset(&table, 0, sizeof(table));
    EXPECT_TRUE(
        micros_pm_table_initialize(&table) == MICROS_PM_MODEL_OK
        && micros_pm_enable_runtime(&table, &event)
            == MICROS_PM_MODEL_OK
        && micros_pm_begin_init(&table, &init, &pid)
            == MICROS_PM_MODEL_OK
        && pid == 1
        && rollback_spawn(
            &table,
            init,
            MICROS_PM_SPAWN_PM_RECORD_RESERVED,
            0,
            false,
            &released
        )
        && released == 0
        && table.init_attempted
        && table.last_pid == 1
        && table.reaper.generation == 0
        && micros_pm_begin_init(&table, &init, &pid)
            == MICROS_PM_MODEL_ERROR_STATE
        && micros_pm_table_validate(&table) == MICROS_PM_MODEL_OK
    );
    return true;
}

static bool test_endpoint_lookup_and_failure_preservation(void)
{
    struct micros_pm_table table;
    struct micros_pm_table snapshot;
    struct micros_pm_process_handle init;
    struct micros_pm_process_handle child;
    struct micros_pm_process_handle found;
    struct micros_pm_process_handle found_sentinel;
    struct micros_pm_exit_effects effects;
    struct micros_pm_exit_effects effects_sentinel;
    struct micros_pm_spawn_rollback_action rollback;
    struct micros_pm_spawn_rollback_action rollback_sentinel;
    struct micros_pm_wait_outcome outcome;
    struct micros_pm_wait_outcome outcome_sentinel;
    uint64_t pid;
    uint64_t found_pid;
    uint64_t found_pid_sentinel = UINT64_C(0xa5a5a5a5a5a5a5a5);

    EXPECT_TRUE(
        initialize_running_init(&table, &init)
        && micros_pm_find_running_by_endpoint(
            &table,
            UINT32_C(0x00001000),
            &found,
            &found_pid
        ) == MICROS_PM_MODEL_OK
        && found.slot == init.slot
        && found.generation == init.generation
        && found_pid == 1
        && micros_pm_begin_spawn(
            &table,
            init,
            800,
            &child,
            &pid
        )
            == MICROS_PM_MODEL_OK
        && micros_pm_spawn_reserve_kernel(&table, child, 801)
            == MICROS_PM_MODEL_OK
    );

    memset(&found_sentinel, 0xa5, sizeof(found_sentinel));
    found = found_sentinel;
    found_pid = found_pid_sentinel;
    EXPECT_TRUE(
        micros_pm_find_running_by_endpoint(
            &table,
            UINT32_C(0x00002001),
            &found,
            &found_pid
        ) == MICROS_PM_MODEL_ERROR_STALE
        && memcmp(&found, &found_sentinel, sizeof(found)) == 0
        && found_pid == found_pid_sentinel
    );

    snapshot = table;
    memset(&rollback_sentinel, 0xa5, sizeof(rollback_sentinel));
    rollback = rollback_sentinel;
    EXPECT_TRUE(
        micros_pm_spawn_rollback_begin(
            &table,
            child,
            MICROS_PM_SPAWN_VFS_IMAGE_PREPARED,
            800,
            false,
            &rollback
        ) == MICROS_PM_MODEL_ERROR_STATE
        && memcmp(
            &rollback,
            &rollback_sentinel,
            sizeof(rollback)
        ) == 0
        && memcmp(&table, &snapshot, sizeof(table)) == 0
    );
    EXPECT_TRUE(
        micros_pm_spawn_prepare_image(&table, child, 0)
            == MICROS_PM_MODEL_ERROR_ARGUMENT
        && memcmp(&table, &snapshot, sizeof(table)) == 0
    );
    EXPECT_TRUE(
        micros_pm_spawn_prepare_image(&table, child, 802)
            == MICROS_PM_MODEL_OK
        && micros_pm_spawn_freeze_mappings(
            &table,
            child,
            803,
            804
        ) == MICROS_PM_MODEL_OK
        && micros_pm_spawn_prepare_descriptors(&table, child, 805)
            == MICROS_PM_MODEL_OK
    );
    snapshot = table;
    EXPECT_TRUE(
        micros_pm_spawn_prepare_execution(&table, child, 806, 1)
            == MICROS_PM_MODEL_ERROR_ARGUMENT
        && memcmp(&table, &snapshot, sizeof(table)) == 0
        && micros_pm_spawn_prepare_execution(
            &table,
            child,
            806,
            UINT32_C(0x00001000)
        ) == MICROS_PM_MODEL_ERROR_ARGUMENT
        && memcmp(&table, &snapshot, sizeof(table)) == 0
    );

    memset(&effects_sentinel, 0xa5, sizeof(effects_sentinel));
    effects = effects_sentinel;
    EXPECT_TRUE(
        micros_pm_exit_begin(
            &table,
            init,
            802,
            MICROS_PM_EXIT_NORMAL,
            0,
            &effects
        ) == MICROS_PM_MODEL_ERROR_INVARIANT
        && memcmp(&effects, &effects_sentinel, sizeof(effects)) == 0
        && memcmp(&table, &snapshot, sizeof(table)) == 0
    );

    memset(&outcome_sentinel, 0xa5, sizeof(outcome_sentinel));
    outcome = outcome_sentinel;
    EXPECT_TRUE(
        micros_pm_wait_begin(
            &table,
            init,
            0,
            UINT32_C(0x2),
            803,
            &outcome
        ) == MICROS_PM_MODEL_ERROR_ARGUMENT
        && memcmp(&outcome, &outcome_sentinel, sizeof(outcome)) == 0
        && memcmp(&table, &snapshot, sizeof(table)) == 0
        && micros_pm_table_validate(&table) == MICROS_PM_MODEL_OK
    );
    return true;
}

static bool test_orphaned_lifecycle_is_invalid(void)
{
    struct micros_pm_table table;
    struct micros_pm_process_handle init;
    struct micros_pm_process_handle child;
    struct micros_pm_wait_outcomes outcomes;
    uint64_t pid;

    EXPECT_TRUE(
        initialize_running_init(&table, &init)
        && spawn_running_child(
            &table,
            init,
            UINT32_C(0x00002001),
            &child,
            &pid
        )
        && complete_exit(&table, child, 901, 9, &outcomes)
        && outcomes.count == 0
    );
    memset(
        &table.records[child.slot].parent,
        0,
        sizeof(table.records[child.slot].parent)
    );
    EXPECT_TRUE(
        micros_pm_table_validate(&table)
            == MICROS_PM_MODEL_ERROR_INVARIANT
    );
    return true;
}

static bool spawn_running_child(
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
        && complete_spawn(table, *child, endpoint, reply_token)
    );
}

static bool complete_exit(
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

static bool test_exit_wait_and_reply_restore(void)
{
    struct micros_pm_table table;
    struct micros_pm_process_handle init;
    struct micros_pm_process_handle child;
    const struct micros_pm_process_record *record;
    struct micros_pm_wait_outcome outcome;
    struct micros_pm_wait_outcomes outcomes;
    struct micros_pm_wait_outcomes outcomes_sentinel;
    uint64_t pid;

    EXPECT_TRUE(
        initialize_running_init(&table, &init)
        && spawn_running_child(
            &table,
            init,
            UINT32_C(0x00002001),
            &child,
            &pid
        )
        && pid == 2
        && micros_pm_wait_begin(
            &table,
            init,
            pid,
            0,
            41,
            &outcome
        ) == MICROS_PM_MODEL_OK
        && outcome.action == MICROS_PM_WAIT_ACTION_BLOCKED
        && outcome.reply_token == 41
    );
    EXPECT_TRUE(
        micros_pm_wait_begin(
            &table,
            init,
            0,
            0,
            42,
            &outcome
        ) == MICROS_PM_MODEL_OK
        && outcome.action == MICROS_PM_WAIT_ACTION_REPLY
        && outcome.result == MICROS_PM_RESULT_BUSY
        && outcome.reply_token == 42
        && table.records[init.slot].wait.reply_token == 41
    );

    memset(&outcomes_sentinel, 0xa5, sizeof(outcomes_sentinel));
    outcomes = outcomes_sentinel;
    EXPECT_TRUE(
        micros_pm_exit_begin(
            &table,
            child,
            501,
            MICROS_PM_EXIT_NORMAL,
            -17,
            &(struct micros_pm_exit_effects){0}
        ) == MICROS_PM_MODEL_OK
        && micros_pm_process_resolve(&table, child, &record)
            == MICROS_PM_MODEL_OK
        && record->state == MICROS_PM_PROCESS_EXITING
        && record->exit_stage == MICROS_PM_EXIT_STAGE_STOPPED
        && record->endpoint == UINT32_C(0x00002001)
        && record->exit_transaction == 501
        && record->exit_kind == MICROS_PM_EXIT_NORMAL
        && record->exit_code == -17
        && micros_pm_exit_detach(&table, child, 500)
            == MICROS_PM_MODEL_ERROR_STALE
        && micros_pm_exit_detach(&table, child, 501)
            == MICROS_PM_MODEL_OK
        && micros_pm_process_resolve(&table, child, &record)
            == MICROS_PM_MODEL_OK
        && record->exit_stage
            == MICROS_PM_EXIT_STAGE_VM_RELEASE_PENDING
        && record->endpoint == MICROS_ENDPOINT_NONE
        && micros_pm_exit_release_process(
            &table,
            child,
            501,
            &outcomes
        ) == MICROS_PM_MODEL_ERROR_STATE
        && memcmp(
            &outcomes,
            &outcomes_sentinel,
            sizeof(outcomes)
        ) == 0
        && micros_pm_exit_release_vm(&table, child, 501)
            == MICROS_PM_MODEL_OK
        && micros_pm_exit_release_process(
            &table,
            child,
            501,
            &outcomes
        ) == MICROS_PM_MODEL_OK
        && outcomes.count == 1
        && outcomes.reserved == 0
        && outcomes.entries[0].action
            == MICROS_PM_WAIT_ACTION_REPLY
        && outcomes.entries[0].result == MICROS_PM_RESULT_OK
        && outcomes.entries[0].reply_token == 41
        && outcomes.entries[0].child_pid == 2
        && outcomes.entries[0].exit_kind == MICROS_PM_EXIT_NORMAL
        && outcomes.entries[0].exit_code == -17
        && pm_handles_equal(outcomes.entries[0].parent, init)
        && bytes_are_zero(
            &outcomes.entries[1],
            sizeof(outcomes.entries[1])
        )
        && micros_pm_process_resolve(&table, child, &record)
            == MICROS_PM_MODEL_OK
        && record->state == MICROS_PM_PROCESS_ZOMBIE
        && record->reap_pending
    );
    EXPECT_TRUE(
        micros_pm_wait_reply_complete(&table, init, 41, false)
            == MICROS_PM_MODEL_OK
        && micros_pm_process_resolve(&table, child, &record)
            == MICROS_PM_MODEL_OK
        && record->state == MICROS_PM_PROCESS_ZOMBIE
        && !record->reap_pending
        && table.records[init.slot].wait.active
        && !table.records[init.slot].wait.result_ready
        && micros_pm_wait_resume(&table, init, &outcome)
            == MICROS_PM_MODEL_OK
        && outcome.action == MICROS_PM_WAIT_ACTION_REPLY
        && outcome.child_pid == 2
        && micros_pm_wait_reply_complete(&table, init, 40, true)
            == MICROS_PM_MODEL_ERROR_STALE
        && micros_pm_wait_reply_complete(&table, init, 41, true)
            == MICROS_PM_MODEL_OK
        && micros_pm_process_resolve(&table, child, &record)
            == MICROS_PM_MODEL_ERROR_STALE
        && !table.records[init.slot].wait.active
        && micros_pm_table_validate(&table) == MICROS_PM_MODEL_OK
    );
    return true;
}

static bool test_wait_selection(void)
{
    struct micros_pm_table table;
    struct micros_pm_process_handle init;
    struct micros_pm_process_handle first;
    struct micros_pm_process_handle second;
    struct micros_pm_process_handle third;
    struct micros_pm_wait_outcome outcome;
    struct micros_pm_wait_outcomes outcomes;
    uint64_t first_pid;
    uint64_t second_pid;
    uint64_t third_pid;

    EXPECT_TRUE(
        initialize_running_init(&table, &init)
        && spawn_running_child(
            &table,
            init,
            UINT32_C(0x00002001),
            &first,
            &first_pid
        )
        && spawn_running_child(
            &table,
            init,
            UINT32_C(0x00002002),
            &second,
            &second_pid
        )
        && spawn_running_child(
            &table,
            init,
            UINT32_C(0x00002003),
            &third,
            &third_pid
        )
        && micros_pm_wait_begin(
            &table,
            init,
            0,
            MICROS_PM_WAIT_NOHANG,
            50,
            &outcome
        ) == MICROS_PM_MODEL_OK
        && outcome.action == MICROS_PM_WAIT_ACTION_REPLY
        && outcome.result == MICROS_PM_RESULT_OK
        && outcome.child_pid == 0
        && !table.records[init.slot].wait.active
        && micros_pm_wait_begin(
            &table,
            init,
            UINT64_C(999),
            0,
            51,
            &outcome
        ) == MICROS_PM_MODEL_OK
        && outcome.result == MICROS_PM_RESULT_NO_CHILD
    );
    EXPECT_TRUE(
        complete_exit(&table, second, 601, 22, &outcomes)
        && outcomes.count == 0
        && bytes_are_zero(&outcomes, sizeof(outcomes))
        && complete_exit(&table, first, 602, 11, &outcomes)
        && outcomes.count == 0
        && bytes_are_zero(&outcomes, sizeof(outcomes))
        && first_pid < second_pid
        && second_pid < third_pid
        && micros_pm_wait_begin(
            &table,
            init,
            0,
            0,
            52,
            &outcome
        ) == MICROS_PM_MODEL_OK
        && outcome.action == MICROS_PM_WAIT_ACTION_REPLY
        && outcome.child_pid == first_pid
        && outcome.exit_code == 11
        && micros_pm_wait_reply_complete(&table, init, 52, true)
            == MICROS_PM_MODEL_OK
        && micros_pm_wait_begin(
            &table,
            init,
            0,
            0,
            53,
            &outcome
        ) == MICROS_PM_MODEL_OK
        && outcome.child_pid == second_pid
        && outcome.exit_code == 22
        && micros_pm_wait_reply_complete(&table, init, 53, true)
            == MICROS_PM_MODEL_OK
        && micros_pm_wait_begin(
            &table,
            init,
            second_pid,
            0,
            54,
            &outcome
        ) == MICROS_PM_MODEL_OK
        && outcome.result == MICROS_PM_RESULT_NO_CHILD
        && micros_pm_table_validate(&table) == MICROS_PM_MODEL_OK
    );
    return true;
}

static bool test_parent_exit_reparents_and_aborts(void)
{
    struct micros_pm_table table;
    struct micros_pm_process_handle init;
    struct micros_pm_process_handle parent;
    struct micros_pm_process_handle zombie;
    struct micros_pm_process_handle running;
    struct micros_pm_process_handle hidden;
    const struct micros_pm_process_record *record;
    struct micros_pm_table snapshot;
    struct micros_pm_exit_effects effects;
    struct micros_pm_exit_effects effects_sentinel;
    struct micros_pm_wait_outcome outcome;
    struct micros_pm_wait_outcomes outcomes;
    uint32_t released;
    uint64_t parent_pid;
    uint64_t zombie_pid;
    uint64_t running_pid;
    uint64_t hidden_pid;

    EXPECT_TRUE(
        initialize_running_init(&table, &init)
        && spawn_running_child(
            &table,
            init,
            UINT32_C(0x00002001),
            &parent,
            &parent_pid
        )
        && spawn_running_child(
            &table,
            parent,
            UINT32_C(0x00003001),
            &zombie,
            &zombie_pid
        )
        && spawn_running_child(
            &table,
            parent,
            UINT32_C(0x00003002),
            &running,
            &running_pid
        )
        && micros_pm_wait_begin(
            &table,
            parent,
            zombie_pid,
            0,
            70,
            &outcome
        ) == MICROS_PM_MODEL_OK
        && outcome.action == MICROS_PM_WAIT_ACTION_BLOCKED
        && complete_exit(&table, zombie, 701, 33, &outcomes)
        && outcomes.count == 1
        && outcomes.entries[0].child_pid == zombie_pid
        && micros_pm_begin_spawn(
            &table,
            parent,
            72,
            &hidden,
            &hidden_pid
        ) == MICROS_PM_MODEL_OK
        && micros_pm_spawn_reserve_kernel(&table, hidden, 702)
            == MICROS_PM_MODEL_OK
        && micros_pm_spawn_prepare_image(&table, hidden, 703)
            == MICROS_PM_MODEL_OK
        && micros_pm_wait_begin(
            &table,
            init,
            0,
            0,
            71,
            &outcome
        ) == MICROS_PM_MODEL_OK
        && outcome.action == MICROS_PM_WAIT_ACTION_BLOCKED
    );
    snapshot = table;
    memset(&effects_sentinel, 0xa5, sizeof(effects_sentinel));
    effects = effects_sentinel;
    EXPECT_TRUE(
        micros_pm_exit_begin(
            &table,
            parent,
            704,
            MICROS_PM_EXIT_NORMAL,
            44,
            &effects
        ) == MICROS_PM_MODEL_ERROR_BUSY
        && memcmp(&effects, &effects_sentinel, sizeof(effects)) == 0
        && memcmp(&table, &snapshot, sizeof(table)) == 0
        && rollback_spawn(
            &table,
            hidden,
            MICROS_PM_SPAWN_VFS_IMAGE_PREPARED,
            72,
            true,
            &released
        )
        && released
            == (
                MICROS_PM_RESOURCE_KERNEL_PROCESS
                | MICROS_PM_RESOURCE_VFS_IMAGE
            )
        && micros_pm_exit_begin(
            &table,
            parent,
            704,
            MICROS_PM_EXIT_NORMAL,
            44,
            &effects
        ) == MICROS_PM_MODEL_OK
        && !effects.aborted_spawn
        && effects.reparented_count == 2
        && !table.records[parent.slot].wait.active
        && table.records[init.slot].wait.active
        && !table.records[init.slot].wait.result_ready
        && micros_pm_process_resolve(&table, hidden, &record)
            == MICROS_PM_MODEL_ERROR_STALE
        && micros_pm_process_resolve(&table, zombie, &record)
            == MICROS_PM_MODEL_OK
        && record->parent.slot == init.slot
        && record->parent.generation == init.generation
        && !record->reap_pending
        && micros_pm_process_resolve(&table, running, &record)
            == MICROS_PM_MODEL_OK
        && record->parent.slot == init.slot
        && record->parent.generation == init.generation
        && micros_pm_process_resolve(&table, parent, &record)
            == MICROS_PM_MODEL_OK
        && record->state == MICROS_PM_PROCESS_EXITING
        && record->parent.slot == init.slot
        && record->parent.generation == init.generation
        && micros_pm_exit_begin(
            &table,
            init,
            705,
            MICROS_PM_EXIT_NORMAL,
            0,
            &effects
        ) == MICROS_PM_MODEL_ERROR_INVARIANT
        && micros_pm_exit_detach(&table, parent, 704)
            == MICROS_PM_MODEL_OK
        && micros_pm_exit_release_vm(&table, parent, 704)
            == MICROS_PM_MODEL_OK
        && micros_pm_exit_release_process(
            &table,
            parent,
            704,
            &outcomes
        ) == MICROS_PM_MODEL_OK
        && outcomes.count == 1
        && outcomes.reserved == 0
        && outcomes.entries[0].action
            == MICROS_PM_WAIT_ACTION_REPLY
        && outcomes.entries[0].reply_token == 71
        && outcomes.entries[0].child_pid == parent_pid
        && outcomes.entries[0].exit_kind
            == MICROS_PM_EXIT_NORMAL
        && outcomes.entries[0].exit_code == 44
        && pm_handles_equal(outcomes.entries[0].parent, init)
        && pm_handles_equal(outcomes.entries[0].child, parent)
        && bytes_are_zero(
            &outcomes.entries[1],
            sizeof(outcomes.entries[1])
        )
        && micros_pm_process_resolve(&table, zombie, &record)
            == MICROS_PM_MODEL_OK
        && !record->reap_pending
        && micros_pm_wait_reply_complete(&table, init, 71, true)
            == MICROS_PM_MODEL_OK
        && micros_pm_process_resolve(&table, parent, &record)
            == MICROS_PM_MODEL_ERROR_STALE
        && micros_pm_wait_begin(
            &table,
            init,
            0,
            0,
            73,
            &outcome
        ) == MICROS_PM_MODEL_OK
        && outcome.action == MICROS_PM_WAIT_ACTION_REPLY
        && outcome.child_pid == zombie_pid
        && micros_pm_wait_reply_complete(&table, init, 73, true)
            == MICROS_PM_MODEL_OK
        && micros_pm_table_validate(&table) == MICROS_PM_MODEL_OK
    );
    (void)running_pid;
    (void)hidden_pid;
    return true;
}

static bool test_nested_parent_exit_stages_independent_waits(void)
{
    struct micros_pm_table table;
    struct micros_pm_process_handle init;
    struct micros_pm_process_handle grandparent;
    struct micros_pm_process_handle parent;
    struct micros_pm_process_handle child;
    const struct micros_pm_process_record *record;
    struct micros_pm_exit_effects effects;
    struct micros_pm_wait_outcome outcome;
    struct micros_pm_wait_outcomes outcomes;
    uint64_t grandparent_pid;
    uint64_t parent_pid;
    uint64_t child_pid;

    EXPECT_TRUE(
        initialize_running_init(&table, &init)
        && spawn_running_child(
            &table,
            init,
            UINT32_C(0x00002001),
            &grandparent,
            &grandparent_pid
        )
        && spawn_running_child(
            &table,
            grandparent,
            UINT32_C(0x00003001),
            &parent,
            &parent_pid
        )
        && spawn_running_child(
            &table,
            parent,
            UINT32_C(0x00004001),
            &child,
            &child_pid
        )
        && complete_exit(&table, child, 801, 33, &outcomes)
        && outcomes.count == 0
        && micros_pm_wait_begin(
            &table,
            init,
            0,
            0,
            81,
            &outcome
        ) == MICROS_PM_MODEL_OK
        && outcome.action == MICROS_PM_WAIT_ACTION_BLOCKED
        && micros_pm_wait_begin(
            &table,
            grandparent,
            parent_pid,
            0,
            82,
            &outcome
        ) == MICROS_PM_MODEL_OK
        && outcome.action == MICROS_PM_WAIT_ACTION_BLOCKED
        && micros_pm_exit_begin(
            &table,
            parent,
            802,
            MICROS_PM_EXIT_NORMAL,
            44,
            &effects
        ) == MICROS_PM_MODEL_OK
        && !effects.aborted_spawn
        && effects.reparented_count == 1
        && table.records[init.slot].wait.active
        && !table.records[init.slot].wait.result_ready
        && table.records[grandparent.slot].wait.active
        && !table.records[grandparent.slot].wait.result_ready
        && micros_pm_process_resolve(&table, child, &record)
            == MICROS_PM_MODEL_OK
        && pm_handles_equal(record->parent, init)
        && !record->reap_pending
        && micros_pm_exit_detach(&table, parent, 802)
            == MICROS_PM_MODEL_OK
        && micros_pm_exit_release_vm(&table, parent, 802)
            == MICROS_PM_MODEL_OK
        && micros_pm_exit_release_process(
            &table,
            parent,
            802,
            &outcomes
        ) == MICROS_PM_MODEL_OK
        && outcomes.count == 2
        && outcomes.reserved == 0
        && outcomes.entries[0].action
            == MICROS_PM_WAIT_ACTION_REPLY
        && outcomes.entries[0].reply_token == 81
        && outcomes.entries[0].child_pid == child_pid
        && outcomes.entries[0].exit_code == 33
        && pm_handles_equal(outcomes.entries[0].parent, init)
        && pm_handles_equal(outcomes.entries[0].child, child)
        && outcomes.entries[1].action
            == MICROS_PM_WAIT_ACTION_REPLY
        && outcomes.entries[1].reply_token == 82
        && outcomes.entries[1].child_pid == parent_pid
        && outcomes.entries[1].exit_code == 44
        && pm_handles_equal(
            outcomes.entries[1].parent,
            grandparent
        )
        && pm_handles_equal(outcomes.entries[1].child, parent)
        && micros_pm_wait_reply_complete(&table, init, 81, false)
            == MICROS_PM_MODEL_OK
        && table.records[init.slot].wait.active
        && !table.records[init.slot].wait.result_ready
        && table.records[grandparent.slot].wait.result_ready
        && micros_pm_process_resolve(&table, child, &record)
            == MICROS_PM_MODEL_OK
        && !record->reap_pending
        && micros_pm_process_resolve(&table, parent, &record)
            == MICROS_PM_MODEL_OK
        && record->reap_pending
        && micros_pm_wait_reply_complete(
            &table,
            grandparent,
            82,
            true
        ) == MICROS_PM_MODEL_OK
        && micros_pm_process_resolve(&table, parent, &record)
            == MICROS_PM_MODEL_ERROR_STALE
        && micros_pm_wait_resume(&table, init, &outcome)
            == MICROS_PM_MODEL_OK
        && outcome.action == MICROS_PM_WAIT_ACTION_REPLY
        && outcome.reply_token == 81
        && outcome.child_pid == child_pid
        && pm_handles_equal(outcome.parent, init)
        && micros_pm_wait_reply_complete(&table, init, 81, true)
            == MICROS_PM_MODEL_OK
        && micros_pm_process_resolve(&table, child, &record)
            == MICROS_PM_MODEL_ERROR_STALE
        && micros_pm_table_validate(&table) == MICROS_PM_MODEL_OK
    );
    (void)grandparent_pid;
    return true;
}

enum {
    PM_MODEL_STEP_COUNT = 4096,
    PM_MODEL_NO_SLOT = UINT16_MAX,
};

struct pm_model_record {
    bool present;
    bool reaper;
    bool reap_pending;
    enum micros_pm_process_state state;
    enum micros_pm_spawn_stage spawn_stage;
    enum micros_pm_exit_stage exit_stage;
    struct micros_pm_process_handle handle;
    uint64_t pid;
    uint64_t transaction;
    uint64_t reply_token;
    micros_endpoint_t endpoint;
    int32_t exit_code;
};

struct pm_model_wait {
    bool active;
    bool result_ready;
    uint32_t flags;
    uint64_t selector;
    uint64_t reply_token;
    uint16_t pending_slot;
};

struct pm_model_reference {
    uint64_t last_pid;
    uint32_t generations[MICROS_PM_PROCESS_CAPACITY];
    struct pm_model_record records[MICROS_PM_PROCESS_CAPACITY];
    struct pm_model_wait wait;
};

static uint64_t pm_model_random(uint64_t *state)
{
    uint64_t value = *state;

    value ^= value << 13;
    value ^= value >> 7;
    value ^= value << 17;
    *state = value;
    return value;
}

static uint16_t pm_model_select_state(
    const struct pm_model_reference *reference,
    enum micros_pm_process_state state,
    uint64_t choice
)
{
    uint16_t slots[MICROS_PM_PROCESS_CAPACITY];
    uint16_t count = 0;
    uint16_t index;

    for (index = 0; index < MICROS_PM_PROCESS_CAPACITY; ++index) {
        if (
            reference->records[index].present
            && !reference->records[index].reaper
            && reference->records[index].state == state
        ) {
            slots[count] = index;
            ++count;
        }
    }
    if (count == 0) {
        return PM_MODEL_NO_SLOT;
    }
    return slots[choice % count];
}

static uint16_t pm_model_select_present(
    const struct pm_model_reference *reference,
    uint64_t choice
)
{
    uint16_t slots[MICROS_PM_PROCESS_CAPACITY];
    uint16_t count = 0;
    uint16_t index;

    for (index = 0; index < MICROS_PM_PROCESS_CAPACITY; ++index) {
        if (
            reference->records[index].present
            && !reference->records[index].reaper
        ) {
            slots[count] = index;
            ++count;
        }
    }
    if (count == 0) {
        return PM_MODEL_NO_SLOT;
    }
    return slots[choice % count];
}

static bool pm_model_find_zombie(
    const struct pm_model_reference *reference,
    uint64_t selector,
    uint16_t *slot
)
{
    uint64_t lowest_pid = UINT64_MAX;
    uint16_t index;
    bool found = false;

    for (index = 0; index < MICROS_PM_PROCESS_CAPACITY; ++index) {
        const struct pm_model_record *record
            = &reference->records[index];

        if (
            record->present
            && !record->reaper
            && record->state == MICROS_PM_PROCESS_ZOMBIE
            && !record->reap_pending
            && (selector == 0 || selector == record->pid)
            && (!found || record->pid < lowest_pid)
        ) {
            found = true;
            lowest_pid = record->pid;
            *slot = index;
        }
    }
    return found;
}

static bool pm_model_has_child(
    const struct pm_model_reference *reference,
    uint64_t selector
)
{
    uint16_t index;

    for (index = 0; index < MICROS_PM_PROCESS_CAPACITY; ++index) {
        const struct pm_model_record *record
            = &reference->records[index];

        if (
            record->present
            && !record->reaper
            && (selector == 0 || selector == record->pid)
        ) {
            return true;
        }
    }
    return false;
}

static void pm_model_set_wait_result(
    struct pm_model_reference *reference,
    uint16_t slot
)
{
    reference->wait.result_ready = true;
    reference->wait.pending_slot = slot;
    reference->records[slot].reap_pending = true;
}

static bool pm_model_compare(
    const struct micros_pm_table *table,
    const struct pm_model_reference *reference,
    struct micros_pm_process_handle init
)
{
    const struct micros_pm_process_record *init_record
        = &table->records[init.slot];
    uint16_t index;

    if (
        table->last_pid != reference->last_pid
        || !table->runtime_enabled
        || !pm_handles_equal(table->reaper, init)
        || init_record->state != MICROS_PM_PROCESS_RUNNING
        || !init_record->reaper
        || init_record->pid != 1
        || init_record->endpoint != UINT32_C(0x00001000)
        || init_record->wait.active != reference->wait.active
        || init_record->wait.result_ready
            != reference->wait.result_ready
        || init_record->wait.flags != reference->wait.flags
        || init_record->wait.selector != reference->wait.selector
        || init_record->wait.reply_token
            != reference->wait.reply_token
    ) {
        return false;
    }
    if (reference->wait.pending_slot == PM_MODEL_NO_SLOT) {
        if (!pm_handle_is_none(init_record->wait.pending_child)) {
            return false;
        }
    } else {
        const struct pm_model_record *pending
            = &reference->records[reference->wait.pending_slot];

        if (
            init_record->wait.pending_child.slot
                != pending->handle.slot
            || init_record->wait.pending_child.generation
                != pending->handle.generation
        ) {
            return false;
        }
    }

    for (index = 0; index < MICROS_PM_PROCESS_CAPACITY; ++index) {
        const struct pm_model_record *expected
            = &reference->records[index];
        const struct micros_pm_process_record *actual
            = &table->records[index];

        if (index == init.slot) {
            continue;
        }
        if (!expected->present) {
            if (
                actual->state != MICROS_PM_PROCESS_FREE
                || actual->generation
                    != reference->generations[index]
            ) {
                return false;
            }
            continue;
        }
        if (
            actual->state != expected->state
            || actual->generation != expected->handle.generation
            || actual->pid != expected->pid
            || actual->parent.slot != init.slot
            || actual->parent.generation != init.generation
            || actual->reaper
            || actual->reap_pending != expected->reap_pending
            || actual->spawn_stage != expected->spawn_stage
            || (
                expected->state == MICROS_PM_PROCESS_SPAWNING
                && actual->spawn.parent_reply_token
                    != expected->reply_token
            )
            || actual->exit_stage != expected->exit_stage
            || actual->exit_transaction != expected->transaction
            || actual->endpoint != expected->endpoint
        ) {
            return false;
        }
        if (
            (
                expected->state == MICROS_PM_PROCESS_EXITING
                || expected->state == MICROS_PM_PROCESS_ZOMBIE
            )
            && (
                actual->exit_kind != MICROS_PM_EXIT_NORMAL
                || actual->exit_code != expected->exit_code
            )
        ) {
            return false;
        }
    }
    return true;
}

static bool pm_model_failure(
    const char *expression,
    int line,
    uint64_t seed,
    size_t step,
    const uint32_t *trace
)
{
    size_t index;

    fprintf(
        stderr,
        "%s:%d: PM model failure: %s seed=0x%016llx step=%zu\n",
        __FILE__,
        line,
        expression,
        (unsigned long long)seed,
        step
    );
    fprintf(stderr, "operation trace:");
    for (index = 0; index <= step; ++index) {
        if ((index % 8) == 0) {
            fputc('\n', stderr);
        }
        fprintf(stderr, " %08x", trace[index]);
    }
    fputc('\n', stderr);
    return false;
}

#define PM_MODEL_EXPECT(expression) \
    do { \
        if (!(expression)) { \
            return pm_model_failure( \
                #expression, \
                __LINE__, \
                seed, \
                step, \
                operation_trace \
            ); \
        } \
    } while (false)

static bool test_replayable_transition_model(void)
{
    const uint64_t seed = UINT64_C(0x4d4943524f53504d);
    struct micros_pm_table table;
    struct micros_pm_process_handle init;
    struct pm_model_reference reference;
    uint32_t operation_trace[PM_MODEL_STEP_COUNT];
    uint64_t random_state = seed;
    size_t step;

    memset(&reference, 0, sizeof(reference));
    memset(operation_trace, 0, sizeof(operation_trace));
    reference.wait.pending_slot = PM_MODEL_NO_SLOT;
    step = 0;
    PM_MODEL_EXPECT(initialize_running_init(&table, &init));
    reference.last_pid = 1;
    reference.generations[init.slot] = init.generation;
    reference.records[init.slot].present = true;
    reference.records[init.slot].reaper = true;
    reference.records[init.slot].state = MICROS_PM_PROCESS_RUNNING;
    reference.records[init.slot].handle = init;
    reference.records[init.slot].pid = 1;
    reference.records[init.slot].endpoint = UINT32_C(0x00001000);

    for (step = 0; step < PM_MODEL_STEP_COUNT; ++step) {
        uint64_t choice = pm_model_random(&random_state);
        uint32_t operation = (uint32_t)(choice % 8);
        uint16_t slot = PM_MODEL_NO_SLOT;

        operation_trace[step] = operation << 24;
        if (operation == 0) {
            struct micros_pm_process_handle child;
            uint64_t pid;
            uint64_t reply_token = UINT64_C(0x500000) + step;

            if (
                pm_model_select_state(
                    &reference,
                    MICROS_PM_PROCESS_SPAWNING,
                    0
                ) == PM_MODEL_NO_SLOT
            ) {
                PM_MODEL_EXPECT(
                    micros_pm_begin_spawn(
                        &table,
                        init,
                        reply_token,
                        &child,
                        &pid
                    ) == MICROS_PM_MODEL_OK
                );
                slot = child.slot;
                operation_trace[step] |= (uint32_t)slot << 16;
                PM_MODEL_EXPECT(
                    !reference.records[slot].present
                    && child.generation
                        == reference.generations[slot] + 1
                    && pid == reference.last_pid + 1
                );
                reference.generations[slot] = child.generation;
                reference.last_pid = pid;
                reference.records[slot].present = true;
                reference.records[slot].state
                    = MICROS_PM_PROCESS_SPAWNING;
                reference.records[slot].spawn_stage
                    = MICROS_PM_SPAWN_PM_RECORD_RESERVED;
                reference.records[slot].handle = child;
                reference.records[slot].pid = pid;
                reference.records[slot].reply_token = reply_token;
                reference.records[slot].endpoint
                    = MICROS_ENDPOINT_NONE;
            }
        } else if (operation == 1) {
            slot = pm_model_select_state(
                &reference,
                MICROS_PM_PROCESS_SPAWNING,
                choice >> 8
            );
            if (slot != PM_MODEL_NO_SLOT) {
                struct pm_model_record *record
                    = &reference.records[slot];
                uint64_t token = UINT64_C(0x100000) + step;
                micros_endpoint_t endpoint;

                operation_trace[step] |= (uint32_t)slot << 16;
                if (
                    record->spawn_stage
                    == MICROS_PM_SPAWN_PM_RECORD_RESERVED
                ) {
                    PM_MODEL_EXPECT(
                        micros_pm_spawn_reserve_kernel(
                            &table,
                            record->handle,
                            token
                        ) == MICROS_PM_MODEL_OK
                    );
                    record->spawn_stage
                        = MICROS_PM_SPAWN_KERNEL_PROCESS_RESERVED;
                } else if (
                    record->spawn_stage
                    == MICROS_PM_SPAWN_KERNEL_PROCESS_RESERVED
                ) {
                    PM_MODEL_EXPECT(
                        micros_pm_spawn_prepare_image(
                            &table,
                            record->handle,
                            token
                        ) == MICROS_PM_MODEL_OK
                    );
                    record->spawn_stage
                        = MICROS_PM_SPAWN_VFS_IMAGE_PREPARED;
                } else if (
                    record->spawn_stage
                    == MICROS_PM_SPAWN_VFS_IMAGE_PREPARED
                ) {
                    PM_MODEL_EXPECT(
                        micros_pm_spawn_freeze_mappings(
                            &table,
                            record->handle,
                            token,
                            token + 1
                        ) == MICROS_PM_MODEL_OK
                    );
                    record->spawn_stage
                        = MICROS_PM_SPAWN_VM_MAPPINGS_FROZEN;
                } else if (
                    record->spawn_stage
                    == MICROS_PM_SPAWN_VM_MAPPINGS_FROZEN
                ) {
                    PM_MODEL_EXPECT(
                        micros_pm_spawn_prepare_descriptors(
                            &table,
                            record->handle,
                            token
                        ) == MICROS_PM_MODEL_OK
                    );
                    record->spawn_stage
                        = MICROS_PM_SPAWN_VFS_DESCRIPTORS_PREPARED;
                } else if (
                    record->spawn_stage
                    == MICROS_PM_SPAWN_VFS_DESCRIPTORS_PREPARED
                ) {
                    endpoint = (
                        record->handle.generation
                        << MICROS_ENDPOINT_SLOT_BITS
                    ) | record->handle.slot;
                    PM_MODEL_EXPECT(
                        micros_pm_spawn_prepare_execution(
                            &table,
                            record->handle,
                            token,
                            endpoint
                        ) == MICROS_PM_MODEL_OK
                    );
                    record->spawn_stage
                        = MICROS_PM_SPAWN_KERNEL_EXECUTION_PREPARED;
                } else if (
                    record->spawn_stage
                    == MICROS_PM_SPAWN_KERNEL_EXECUTION_PREPARED
                ) {
                    PM_MODEL_EXPECT(
                        micros_pm_spawn_commit_hidden(
                            &table,
                            record->handle,
                            token
                        ) == MICROS_PM_MODEL_OK
                    );
                    record->spawn_stage
                        = MICROS_PM_SPAWN_PM_VFS_COMMITTED;
                } else {
                    uint64_t reply_token = 0;

                    endpoint = (
                        record->handle.generation
                        << MICROS_ENDPOINT_SLOT_BITS
                    ) | record->handle.slot;
                    PM_MODEL_EXPECT(
                        micros_pm_spawn_activate(
                            &table,
                            record->handle,
                            endpoint,
                            &reply_token
                        ) == MICROS_PM_MODEL_OK
                        && reply_token == record->reply_token
                    );
                    record->state = MICROS_PM_PROCESS_RUNNING;
                    record->spawn_stage = MICROS_PM_SPAWN_NONE;
                    record->endpoint = endpoint;
                }
            }
        } else if (operation == 2) {
            slot = pm_model_select_state(
                &reference,
                MICROS_PM_PROCESS_SPAWNING,
                choice >> 8
            );
            if (slot != PM_MODEL_NO_SLOT) {
                struct pm_model_record *record
                    = &reference.records[slot];
                uint32_t released = UINT32_MAX;

                operation_trace[step] |= (uint32_t)slot << 16;
                PM_MODEL_EXPECT(
                    rollback_spawn(
                        &table,
                        record->handle,
                        record->spawn_stage,
                        record->reply_token,
                        false,
                        &released
                    )
                    && released
                        == expected_resources(record->spawn_stage)
                );
                memset(record, 0, sizeof(*record));
            }
        } else if (operation == 3) {
            slot = pm_model_select_state(
                &reference,
                MICROS_PM_PROCESS_RUNNING,
                choice >> 8
            );
            if (slot != PM_MODEL_NO_SLOT) {
                struct pm_model_record *record
                    = &reference.records[slot];
                struct micros_pm_exit_effects effects;
                uint64_t transaction
                    = UINT64_C(0x200000) + step;
                int32_t exit_code = -(int32_t)(step + 1);

                operation_trace[step] |= (uint32_t)slot << 16;
                PM_MODEL_EXPECT(
                    micros_pm_exit_begin(
                        &table,
                        record->handle,
                        transaction,
                        MICROS_PM_EXIT_NORMAL,
                        exit_code,
                        &effects
                    ) == MICROS_PM_MODEL_OK
                    && !effects.aborted_spawn
                    && effects.reparented_count == 0
                );
                record->state = MICROS_PM_PROCESS_EXITING;
                record->exit_stage = MICROS_PM_EXIT_STAGE_STOPPED;
                record->transaction = transaction;
                record->exit_code = exit_code;
            }
        } else if (operation == 4) {
            slot = pm_model_select_state(
                &reference,
                MICROS_PM_PROCESS_EXITING,
                choice >> 8
            );
            if (slot != PM_MODEL_NO_SLOT) {
                struct pm_model_record *record
                    = &reference.records[slot];
                struct micros_pm_wait_outcomes outcomes;

                operation_trace[step] |= (uint32_t)slot << 16;
                if (
                    record->exit_stage
                    == MICROS_PM_EXIT_STAGE_STOPPED
                ) {
                    PM_MODEL_EXPECT(
                        micros_pm_exit_detach(
                            &table,
                            record->handle,
                            record->transaction
                        ) == MICROS_PM_MODEL_OK
                    );
                    record->exit_stage
                        = MICROS_PM_EXIT_STAGE_VM_RELEASE_PENDING;
                    record->endpoint = MICROS_ENDPOINT_NONE;
                } else if (
                    record->exit_stage
                    == MICROS_PM_EXIT_STAGE_VM_RELEASE_PENDING
                ) {
                    PM_MODEL_EXPECT(
                        micros_pm_exit_release_vm(
                            &table,
                            record->handle,
                            record->transaction
                        ) == MICROS_PM_MODEL_OK
                    );
                    record->exit_stage
                        = MICROS_PM_EXIT_STAGE_KERNEL_PROCESS_EMPTY;
                } else {
                    uint16_t zombie_slot;

                    PM_MODEL_EXPECT(
                        micros_pm_exit_release_process(
                            &table,
                            record->handle,
                            record->transaction,
                            &outcomes
                        ) == MICROS_PM_MODEL_OK
                    );
                    record->state = MICROS_PM_PROCESS_ZOMBIE;
                    record->exit_stage = MICROS_PM_EXIT_STAGE_NONE;
                    record->transaction = 0;
                    if (
                        reference.wait.active
                        && !reference.wait.result_ready
                        && pm_model_find_zombie(
                            &reference,
                            reference.wait.selector,
                            &zombie_slot
                        )
                    ) {
                        pm_model_set_wait_result(
                            &reference,
                            zombie_slot
                        );
                        PM_MODEL_EXPECT(
                            outcomes.count == 1
                            && outcomes.entries[0].action
                                == MICROS_PM_WAIT_ACTION_REPLY
                            && outcomes.entries[0].reply_token
                                == reference.wait.reply_token
                            && outcomes.entries[0].child_pid
                                == reference.records[zombie_slot].pid
                            && pm_handles_equal(
                                outcomes.entries[0].parent,
                                init
                            )
                        );
                    } else {
                        PM_MODEL_EXPECT(outcomes.count == 0);
                    }
                }
            }
        } else if (operation == 5) {
            struct micros_pm_wait_outcome outcome;
            uint64_t selector;
            uint32_t flags;
            uint64_t reply_token = UINT64_C(0x300000) + step;
            uint16_t selected = pm_model_select_present(
                &reference,
                choice >> 8
            );
            uint16_t zombie_slot;
            bool has_child;

            if ((choice & UINT64_C(0x100)) == 0) {
                selector = 0;
            } else if (selected != PM_MODEL_NO_SLOT) {
                selector = reference.records[selected].pid;
            } else {
                selector = reference.last_pid + 100;
            }
            flags = (
                (choice & UINT64_C(0x200)) != 0
                    ? MICROS_PM_WAIT_NOHANG
                    : 0
            );
            if (
                pm_model_select_state(
                    &reference,
                    MICROS_PM_PROCESS_SPAWNING,
                    0
                ) != PM_MODEL_NO_SLOT
            ) {
                flags = MICROS_PM_WAIT_NOHANG;
            }
            operation_trace[step] |= (uint32_t)flags << 16;
            PM_MODEL_EXPECT(
                micros_pm_wait_begin(
                    &table,
                    init,
                    selector,
                    flags,
                    reply_token,
                    &outcome
                ) == MICROS_PM_MODEL_OK
            );
            if (reference.wait.active) {
                PM_MODEL_EXPECT(
                    outcome.action == MICROS_PM_WAIT_ACTION_REPLY
                    && outcome.result == MICROS_PM_RESULT_BUSY
                    && outcome.reply_token == reply_token
                );
            } else if (
                pm_model_find_zombie(
                    &reference,
                    selector,
                    &zombie_slot
                )
            ) {
                reference.wait.active = true;
                reference.wait.result_ready = true;
                reference.wait.flags = flags;
                reference.wait.selector = selector;
                reference.wait.reply_token = reply_token;
                reference.wait.pending_slot = zombie_slot;
                reference.records[zombie_slot].reap_pending = true;
                PM_MODEL_EXPECT(
                    outcome.action == MICROS_PM_WAIT_ACTION_REPLY
                    && outcome.result == MICROS_PM_RESULT_OK
                    && outcome.child_pid
                        == reference.records[zombie_slot].pid
                );
            } else {
                has_child = pm_model_has_child(&reference, selector);
                if (!has_child) {
                    PM_MODEL_EXPECT(
                        outcome.action
                            == MICROS_PM_WAIT_ACTION_REPLY
                        && outcome.result
                            == MICROS_PM_RESULT_NO_CHILD
                    );
                } else if ((flags & MICROS_PM_WAIT_NOHANG) != 0) {
                    PM_MODEL_EXPECT(
                        outcome.action
                            == MICROS_PM_WAIT_ACTION_REPLY
                        && outcome.result == MICROS_PM_RESULT_OK
                        && outcome.child_pid == 0
                    );
                } else {
                    reference.wait.active = true;
                    reference.wait.flags = flags;
                    reference.wait.selector = selector;
                    reference.wait.reply_token = reply_token;
                    reference.wait.pending_slot = PM_MODEL_NO_SLOT;
                    PM_MODEL_EXPECT(
                        outcome.action
                            == MICROS_PM_WAIT_ACTION_BLOCKED
                    );
                }
            }
        } else if (operation == 6) {
            if (reference.wait.active) {
                struct micros_pm_wait_outcome outcome;

                if (reference.wait.result_ready) {
                    bool accepted = (choice & UINT64_C(0x100)) != 0;
                    uint16_t pending_slot
                        = reference.wait.pending_slot;

                    operation_trace[step] |= (
                        accepted ? UINT32_C(0x00010000) : 0
                    );
                    PM_MODEL_EXPECT(
                        micros_pm_wait_reply_complete(
                            &table,
                            init,
                            reference.wait.reply_token,
                            accepted
                        ) == MICROS_PM_MODEL_OK
                    );
                    if (accepted) {
                        memset(
                            &reference.records[pending_slot],
                            0,
                            sizeof(reference.records[pending_slot])
                        );
                        memset(
                            &reference.wait,
                            0,
                            sizeof(reference.wait)
                        );
                        reference.wait.pending_slot
                            = PM_MODEL_NO_SLOT;
                    } else {
                        reference.records[pending_slot].reap_pending
                            = false;
                        reference.wait.result_ready = false;
                        reference.wait.pending_slot
                            = PM_MODEL_NO_SLOT;
                    }
                } else {
                    uint16_t zombie_slot;
                    bool found = pm_model_find_zombie(
                        &reference,
                        reference.wait.selector,
                        &zombie_slot
                    );

                    PM_MODEL_EXPECT(
                        micros_pm_wait_resume(
                            &table,
                            init,
                            &outcome
                        ) == MICROS_PM_MODEL_OK
                    );
                    if (found) {
                        pm_model_set_wait_result(
                            &reference,
                            zombie_slot
                        );
                        PM_MODEL_EXPECT(
                            outcome.action
                                == MICROS_PM_WAIT_ACTION_REPLY
                            && outcome.child_pid
                                == reference.records[zombie_slot].pid
                        );
                    } else {
                        PM_MODEL_EXPECT(
                            outcome.action
                                == MICROS_PM_WAIT_ACTION_BLOCKED
                        );
                    }
                }
            }
        } else {
            slot = pm_model_select_state(
                &reference,
                MICROS_PM_PROCESS_RUNNING,
                choice >> 8
            );
            if (slot != PM_MODEL_NO_SLOT) {
                struct micros_pm_process_handle process;
                uint64_t pid;

                operation_trace[step] |= (uint32_t)slot << 16;
                PM_MODEL_EXPECT(
                    micros_pm_find_running_by_endpoint(
                        &table,
                        reference.records[slot].endpoint,
                        &process,
                        &pid
                    ) == MICROS_PM_MODEL_OK
                    && process.slot == slot
                    && process.generation
                        == reference.records[slot].handle.generation
                    && pid == reference.records[slot].pid
                );
            }
        }

        PM_MODEL_EXPECT(
            micros_pm_table_validate(&table) == MICROS_PM_MODEL_OK
            && pm_model_compare(&table, &reference, init)
        );
    }
    return true;
}

int main(void)
{
    if (
        !micros_pm_control_test_run()
        || !micros_pm_control_execution_test_run()
        || !test_protocol_contract()
        || !test_runtime_gate_and_spawn()
        || !test_reverse_rollback_and_identity()
        || !test_rollback_acknowledgment_identity()
        || !test_generation_and_pid_exhaustion()
        || !test_failed_init_is_terminal()
        || !test_endpoint_lookup_and_failure_preservation()
        || !test_orphaned_lifecycle_is_invalid()
        || !test_exit_wait_and_reply_restore()
        || !test_wait_selection()
        || !test_parent_exit_reparents_and_aborts()
        || !test_nested_parent_exit_stages_independent_waits()
        || !test_replayable_transition_model()
    ) {
        return 1;
    }
    return 0;
}

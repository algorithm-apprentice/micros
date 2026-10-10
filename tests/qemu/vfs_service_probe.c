#include "micros/runtime.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/bootstrap_control.h"
#include "micros/vfs.h"
#include "tests/qemu/vfs_service_protocol.h"

#define MICROS_VFS_TEST_DIRECTORY_MODE UINT32_C(0x000041ed)
#define MICROS_VFS_TEST_REGULAR_MODE UINT32_C(0x000081a4)
#define MICROS_VFS_TEST_CREATE_PERMISSIONS UINT32_C(0x000001a4)
#define MICROS_VFS_TEST_DIRECTORY_PERMISSIONS UINT32_C(0x000001ed)
#define MICROS_VFS_TEST_PROBE_DATA_MAGIC \
    UINT64_C(0x56465350524f4245)
enum {
    MICROS_VFS_TEST_PROBE_DATA_WORDS = 4096 / sizeof(uint64_t),
};

volatile struct micros_bootstrap_service_config
    micros_bootstrap_service_config;

static volatile uint64_t
    probe_initialized_data[MICROS_VFS_TEST_PROBE_DATA_WORDS] = {
        MICROS_VFS_TEST_PROBE_DATA_MAGIC,
    };
static _Alignas(4096) uint8_t probe_buffer[MICROS_VFS_TRANSFER_MAX];

struct probe_vfs_result {
    enum micros_vfs_result result;
    uint32_t descriptor;
    uint32_t mode;
    uint64_t transferred_count;
    uint64_t position;
};

static void probe_clear_bytes(void *storage, size_t size)
{
    uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static void probe_copy_bytes(
    uint8_t *destination,
    const uint8_t *source,
    size_t size
)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        destination[index] = source[index];
    }
}

static bool probe_bytes_equal(
    const uint8_t *left,
    const uint8_t *right,
    size_t size
)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        if (left[index] != right[index]) {
            return false;
        }
    }
    return true;
}

static bool probe_bytes_are_zero(
    const uint8_t *bytes,
    size_t size
)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static size_t probe_string_size(const char *string)
{
    size_t size = 0;

    while (string[size] != '\0') {
        ++size;
    }
    return size;
}

static void probe_write_u32_le(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void probe_write_u64_le(uint8_t *bytes, uint64_t value)
{
    size_t index;

    for (index = 0; index < 8; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}

static uint32_t probe_read_u32_le(const uint8_t *bytes)
{
    return (
        (uint32_t)bytes[0]
        | (uint32_t)bytes[1] << 8
        | (uint32_t)bytes[2] << 16
        | (uint32_t)bytes[3] << 24
    );
}

static uint64_t probe_read_u64_le(const uint8_t *bytes)
{
    uint64_t value = 0;
    size_t index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8);
    }
    return value;
}

static bool probe_endpoint_is_canonical(
    micros_endpoint_t endpoint,
    size_t expected_slot
)
{
    uint32_t slot_mask =
        (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS) - 1;
    uint32_t slot = endpoint & slot_mask;
    uint32_t generation = endpoint >> MICROS_ENDPOINT_SLOT_BITS;

    return (
        endpoint != MICROS_ENDPOINT_NONE
        && endpoint != MICROS_ENDPOINT_ANY
        && slot == expected_slot
        && generation != 0
        && generation <= MICROS_ENDPOINT_GENERATION_MAX
    );
}

static bool probe_validate_configuration(void)
{
    size_t index;
    size_t other;

    if (
        micros_bootstrap_service_config.version
            != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || micros_bootstrap_service_config.manifest_version
            != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || micros_bootstrap_service_config.service_id
            != MICROS_VFS_TEST_APPLICATION_SERVICE_ID
        || micros_bootstrap_service_config.service_count
            != MICROS_VFS_TEST_SERVICE_COUNT
        || micros_bootstrap_service_config.self_endpoint
            != micros_bootstrap_service_config.services[
                MICROS_VFS_TEST_APPLICATION_SERVICE_ID - 1
            ].endpoint
        || micros_bootstrap_service_config.launcher_endpoint
            != micros_bootstrap_service_config.services[
                MICROS_VFS_TEST_LAUNCHER_SERVICE_ID - 1
            ].endpoint
        || micros_bootstrap_service_config.manifest_view_address != 0
    ) {
        return false;
    }
    for (index = 0; index < MICROS_VFS_TEST_SERVICE_COUNT; ++index) {
        if (
            micros_bootstrap_service_config.services[index].service_id
                != index + 1
            || !probe_endpoint_is_canonical(
                micros_bootstrap_service_config.services[index].endpoint,
                index
            )
        ) {
            return false;
        }
        for (other = 0; other < index; ++other) {
            if (
                micros_bootstrap_service_config.services[index].endpoint
                    == micros_bootstrap_service_config
                        .services[other].endpoint
            ) {
                return false;
            }
        }
    }
    for (
        index = 0;
        index < sizeof(micros_bootstrap_service_config.reserved)
                / sizeof(micros_bootstrap_service_config.reserved[0]);
        ++index
    ) {
        if (micros_bootstrap_service_config.reserved[index] != 0) {
            return false;
        }
    }
    return true;
}

static bool probe_send_ready(void)
{
    struct micros_ipc_message message;

    probe_clear_bytes(&message, sizeof(message));
    message.type = MICROS_BOOTSTRAP_MESSAGE_READY;
    probe_write_u32_le(
        &message.payload[0],
        MICROS_BOOTSTRAP_MANIFEST_VERSION
    );
    probe_write_u32_le(
        &message.payload[4],
        MICROS_VFS_TEST_APPLICATION_SERVICE_ID
    );
    probe_write_u32_le(
        &message.payload[8],
        MICROS_BOOTSTRAP_MANIFEST_VERSION
    );
    probe_write_u32_le(
        &message.payload[16],
        micros_bootstrap_service_config.self_endpoint
    );
    if (
        micros_runtime_call(
            micros_bootstrap_service_config.launcher_endpoint,
            &message
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        return false;
    }
    return (
        message.source
            == micros_bootstrap_service_config.launcher_endpoint
        && message.type == MICROS_BOOTSTRAP_MESSAGE_READY_ACK
        && message.reply_token == 0
        && probe_read_u32_le(&message.payload[0])
            == MICROS_BOOTSTRAP_MANIFEST_VERSION
        && probe_read_u32_le(&message.payload[4])
            == MICROS_VFS_TEST_APPLICATION_SERVICE_ID
        && probe_read_u32_le(&message.payload[8])
            == MICROS_BOOTSTRAP_MANIFEST_VERSION
        && probe_read_u32_le(&message.payload[12]) == 0
        && probe_read_u32_le(&message.payload[16])
            == micros_bootstrap_service_config.self_endpoint
        && probe_bytes_are_zero(&message.payload[20], 28)
    );
}

static bool probe_vfs_result_matches(
    const struct micros_ipc_message *message,
    micros_endpoint_t vfs_endpoint,
    uint32_t request_type,
    struct probe_vfs_result *result
)
{
    int32_t result_code;

    if (
        message->source != vfs_endpoint
        || message->type != MICROS_VFS_MESSAGE_RESULT
        || message->reply_token != 0
        || probe_read_u32_le(&message->payload[0])
            != MICROS_VFS_PROTOCOL_VERSION
        || probe_read_u32_le(&message->payload[4]) != request_type
        || probe_read_u32_le(&message->payload[12]) != 0
        || !probe_bytes_are_zero(&message->payload[40], 8)
    ) {
        return false;
    }
    result_code = (int32_t)probe_read_u32_le(&message->payload[8]);
    if (
        result_code < MICROS_VFS_RESULT_RANGE
        || result_code > MICROS_VFS_RESULT_OK
    ) {
        return false;
    }
    result->result = (enum micros_vfs_result)result_code;
    result->descriptor = probe_read_u32_le(&message->payload[16]);
    result->mode = probe_read_u32_le(&message->payload[20]);
    result->transferred_count =
        probe_read_u64_le(&message->payload[24]);
    result->position = probe_read_u64_le(&message->payload[32]);
    if (result->result != MICROS_VFS_RESULT_OK) {
        return (
            result->descriptor == 0
            && result->mode == 0
            && result->transferred_count == 0
            && result->position == 0
        );
    }
    if (request_type == MICROS_VFS_MESSAGE_OPEN) {
        return (
            result->transferred_count == 0
            && result->position == 0
        );
    }
    if (
        request_type == MICROS_VFS_MESSAGE_READ
        || request_type == MICROS_VFS_MESSAGE_WRITE
        || request_type == MICROS_VFS_MESSAGE_GETDENTS
    ) {
        return (
            result->descriptor == 0
            && result->mode == 0
            && result->transferred_count <= MICROS_VFS_TRANSFER_MAX
        );
    }
    return (
        result->descriptor == 0
        && result->mode == 0
        && result->transferred_count == 0
        && result->position == 0
    );
}

static bool probe_call_vfs(
    micros_endpoint_t vfs_endpoint,
    struct micros_ipc_message *message,
    uint32_t request_type,
    struct probe_vfs_result *result
)
{
    return (
        micros_runtime_call(vfs_endpoint, message)
            == MICROS_SYSCALL_ABI_OK
        && probe_vfs_result_matches(
            message,
            vfs_endpoint,
            request_type,
            result
        )
    );
}

static bool probe_create_grant(
    micros_endpoint_t vfs_endpoint,
    size_t length,
    uint32_t permissions,
    micros_grant_t *grant
)
{
    return (
        length > 0
        && length <= sizeof(probe_buffer)
        && micros_runtime_grant_create(
            vfs_endpoint,
            (uintptr_t)probe_buffer,
            length,
            permissions,
            grant
        ) == MICROS_SYSCALL_ABI_OK
        && *grant != MICROS_GRANT_NONE
    );
}

static bool probe_open(
    micros_endpoint_t vfs_endpoint,
    const char *path,
    uint32_t open_flags,
    uint32_t mode,
    struct probe_vfs_result *result
)
{
    struct micros_ipc_message message;
    micros_grant_t grant = MICROS_GRANT_NONE;
    size_t path_length = probe_string_size(path) + 1;
    bool call_ok;
    bool revoke_ok;

    if (path_length > sizeof(probe_buffer)) {
        return false;
    }
    probe_clear_bytes(probe_buffer, sizeof(probe_buffer));
    probe_copy_bytes(
        probe_buffer,
        (const uint8_t *)path,
        path_length
    );
    if (
        !probe_create_grant(
            vfs_endpoint,
            path_length,
            MICROS_GRANT_PERMISSION_READ,
            &grant
        )
    ) {
        return false;
    }
    probe_clear_bytes(&message, sizeof(message));
    message.type = MICROS_VFS_MESSAGE_OPEN;
    probe_write_u32_le(
        &message.payload[0],
        MICROS_VFS_PROTOCOL_VERSION
    );
    probe_write_u32_le(&message.payload[8], grant);
    probe_write_u32_le(
        &message.payload[12],
        (uint32_t)path_length
    );
    probe_write_u64_le(&message.payload[16], 0);
    probe_write_u32_le(&message.payload[24], open_flags);
    probe_write_u32_le(&message.payload[28], mode);
    call_ok = probe_call_vfs(
        vfs_endpoint,
        &message,
        MICROS_VFS_MESSAGE_OPEN,
        result
    );
    revoke_ok = (
        micros_runtime_grant_revoke(grant) == MICROS_SYSCALL_ABI_OK
    );
    return call_ok && revoke_ok;
}

static bool probe_path_call(
    micros_endpoint_t vfs_endpoint,
    uint32_t type,
    const char *path,
    uint32_t mode,
    struct probe_vfs_result *result
)
{
    struct micros_ipc_message message;
    micros_grant_t grant = MICROS_GRANT_NONE;
    size_t path_length = probe_string_size(path) + 1;
    bool call_ok;
    bool revoke_ok;

    if (path_length > sizeof(probe_buffer)) {
        return false;
    }
    probe_clear_bytes(probe_buffer, sizeof(probe_buffer));
    probe_copy_bytes(
        probe_buffer,
        (const uint8_t *)path,
        path_length
    );
    if (
        !probe_create_grant(
            vfs_endpoint,
            path_length,
            MICROS_GRANT_PERMISSION_READ,
            &grant
        )
    ) {
        return false;
    }
    probe_clear_bytes(&message, sizeof(message));
    message.type = type;
    probe_write_u32_le(
        &message.payload[0],
        MICROS_VFS_PROTOCOL_VERSION
    );
    probe_write_u32_le(&message.payload[8], grant);
    probe_write_u32_le(
        &message.payload[12],
        (uint32_t)path_length
    );
    probe_write_u64_le(&message.payload[16], 0);
    if (type == MICROS_VFS_MESSAGE_MKDIR) {
        probe_write_u32_le(&message.payload[24], mode);
    }
    call_ok = probe_call_vfs(
        vfs_endpoint,
        &message,
        type,
        result
    );
    revoke_ok = (
        micros_runtime_grant_revoke(grant) == MICROS_SYSCALL_ABI_OK
    );
    return call_ok && revoke_ok;
}

static bool probe_close(
    micros_endpoint_t vfs_endpoint,
    uint32_t descriptor
)
{
    struct micros_ipc_message message;
    struct probe_vfs_result result;

    probe_clear_bytes(&message, sizeof(message));
    message.type = MICROS_VFS_MESSAGE_CLOSE;
    probe_write_u32_le(
        &message.payload[0],
        MICROS_VFS_PROTOCOL_VERSION
    );
    probe_write_u32_le(&message.payload[8], descriptor);
    return (
        probe_call_vfs(
            vfs_endpoint,
            &message,
            MICROS_VFS_MESSAGE_CLOSE,
            &result
        )
        && result.result == MICROS_VFS_RESULT_OK
        && result.descriptor == 0
        && result.mode == 0
        && result.transferred_count == 0
        && result.position == 0
    );
}

static bool probe_transfer(
    micros_endpoint_t vfs_endpoint,
    uint32_t type,
    uint32_t descriptor,
    uint32_t count,
    struct probe_vfs_result *result
)
{
    struct micros_ipc_message message;
    micros_grant_t grant = MICROS_GRANT_NONE;
    uint32_t permissions;
    bool call_ok;
    bool revoke_ok;

    permissions = type == MICROS_VFS_MESSAGE_WRITE
        ? MICROS_GRANT_PERMISSION_READ
        : MICROS_GRANT_PERMISSION_WRITE;
    if (!probe_create_grant(vfs_endpoint, count, permissions, &grant)) {
        return false;
    }
    probe_clear_bytes(&message, sizeof(message));
    message.type = type;
    probe_write_u32_le(
        &message.payload[0],
        MICROS_VFS_PROTOCOL_VERSION
    );
    probe_write_u32_le(&message.payload[8], descriptor);
    probe_write_u32_le(&message.payload[12], grant);
    probe_write_u64_le(&message.payload[16], 0);
    probe_write_u32_le(&message.payload[24], count);
    call_ok = probe_call_vfs(
        vfs_endpoint,
        &message,
        type,
        result
    );
    revoke_ok = (
        micros_runtime_grant_revoke(grant) == MICROS_SYSCALL_ABI_OK
    );
    return call_ok && revoke_ok;
}

static bool probe_write(
    micros_endpoint_t vfs_endpoint,
    uint32_t descriptor,
    const uint8_t *bytes,
    size_t size
)
{
    struct probe_vfs_result result;

    if (size == 0 || size > sizeof(probe_buffer)) {
        return false;
    }
    probe_clear_bytes(probe_buffer, sizeof(probe_buffer));
    probe_copy_bytes(probe_buffer, bytes, size);
    return (
        probe_transfer(
            vfs_endpoint,
            MICROS_VFS_MESSAGE_WRITE,
            descriptor,
            (uint32_t)size,
            &result
        )
        && result.result == MICROS_VFS_RESULT_OK
        && result.descriptor == 0
        && result.mode == 0
        && result.transferred_count == size
        && result.position == 0
    );
}

static bool probe_read_exact(
    micros_endpoint_t vfs_endpoint,
    uint32_t descriptor,
    const uint8_t *expected,
    size_t expected_size,
    bool require_eof
)
{
    struct probe_vfs_result result;
    uint64_t position;

    probe_clear_bytes(probe_buffer, sizeof(probe_buffer));
    if (
        !probe_transfer(
            vfs_endpoint,
            MICROS_VFS_MESSAGE_READ,
            descriptor,
            sizeof(probe_buffer),
            &result
        )
        || result.result != MICROS_VFS_RESULT_OK
        || result.descriptor != 0
        || result.mode != 0
        || result.transferred_count != expected_size
        || result.position != expected_size
        || !probe_bytes_equal(probe_buffer, expected, expected_size)
    ) {
        return false;
    }
    position = result.position;
    if (!require_eof) {
        return true;
    }
    probe_clear_bytes(probe_buffer, sizeof(probe_buffer));
    return (
        probe_transfer(
            vfs_endpoint,
            MICROS_VFS_MESSAGE_READ,
            descriptor,
            sizeof(probe_buffer),
            &result
        )
        && result.result == MICROS_VFS_RESULT_OK
        && result.transferred_count == 0
        && result.position == position
    );
}

static bool probe_directory_record_matches(
    const uint8_t *record,
    const char *name,
    uint32_t mode
)
{
    size_t name_length = probe_string_size(name);

    return (
        name_length >= 1
        && name_length <= MICROS_VFS_NAME_MAX
        && probe_read_u32_le(&record[0])
            == MICROS_VFS_DIRECTORY_RECORD_SIZE
        && probe_read_u32_le(&record[4]) == mode
        && probe_read_u32_le(&record[8]) == name_length
        && probe_read_u32_le(&record[12]) == 0
        && probe_bytes_equal(
            &record[16],
            (const uint8_t *)name,
            name_length
        )
        && probe_bytes_are_zero(
            &record[16 + name_length],
            MICROS_VFS_NAME_MAX - name_length
        )
        && probe_bytes_are_zero(&record[76], 4)
    );
}

static bool probe_enumerate_root(
    micros_endpoint_t vfs_endpoint
)
{
    static const char *const names[] = {".", "..", "etc", "tmp"};
    struct probe_vfs_result result;
    uint32_t descriptor;
    uint64_t previous_position = 0;
    size_t index;

    if (
        !probe_open(
            vfs_endpoint,
            "/",
            MICROS_VFS_OPEN_READ | MICROS_VFS_OPEN_DIRECTORY,
            0,
            &result
        )
        || result.result != MICROS_VFS_RESULT_OK
        || result.mode != MICROS_VFS_TEST_DIRECTORY_MODE
        || result.descriptor < 3
    ) {
        return false;
    }
    descriptor = result.descriptor;
    for (index = 0; index < sizeof(names) / sizeof(names[0]); ++index) {
        probe_clear_bytes(probe_buffer, sizeof(probe_buffer));
        if (
            !probe_transfer(
                vfs_endpoint,
                MICROS_VFS_MESSAGE_GETDENTS,
                descriptor,
                MICROS_VFS_DIRECTORY_RECORD_SIZE,
                &result
            )
            || result.result != MICROS_VFS_RESULT_OK
            || result.transferred_count
                != MICROS_VFS_DIRECTORY_RECORD_SIZE
            || result.position == previous_position
            || !probe_directory_record_matches(
                probe_buffer,
                names[index],
                MICROS_VFS_TEST_DIRECTORY_MODE
            )
        ) {
            return false;
        }
        previous_position = result.position;
    }
    probe_clear_bytes(probe_buffer, sizeof(probe_buffer));
    return (
        probe_transfer(
            vfs_endpoint,
            MICROS_VFS_MESSAGE_GETDENTS,
            descriptor,
            MICROS_VFS_DIRECTORY_RECORD_SIZE,
            &result
        )
        && result.result == MICROS_VFS_RESULT_OK
        && result.transferred_count == 0
        && result.position == previous_position
        && probe_close(vfs_endpoint, descriptor)
    );
}

static bool probe_test_filesystem(
    micros_endpoint_t vfs_endpoint
)
{
    static const uint8_t motd[] = MICROS_VFS_TEST_MOTD;
    static const uint8_t note[] = MICROS_VFS_TEST_NOTE_DATA;
    struct probe_vfs_result result;
    uint32_t descriptor;

    if (
        !probe_open(
            vfs_endpoint,
            "/etc/motd",
            MICROS_VFS_OPEN_READ,
            0,
            &result
        )
        || result.result != MICROS_VFS_RESULT_OK
        || result.mode != MICROS_VFS_TEST_REGULAR_MODE
        || result.descriptor < 3
    ) {
        return false;
    }
    descriptor = result.descriptor;
    if (
        !probe_read_exact(
            vfs_endpoint,
            descriptor,
            motd,
            sizeof(motd) - 1,
            true
        )
        || !probe_close(vfs_endpoint, descriptor)
        || !probe_path_call(
            vfs_endpoint,
            MICROS_VFS_MESSAGE_MKDIR,
            "tmp",
            MICROS_VFS_TEST_DIRECTORY_PERMISSIONS,
            &result
        )
        || result.result != MICROS_VFS_RESULT_OK
        || !probe_open(
            vfs_endpoint,
            "tmp/note",
            MICROS_VFS_OPEN_WRITE
                | MICROS_VFS_OPEN_CREATE
                | MICROS_VFS_OPEN_EXCLUSIVE,
            MICROS_VFS_TEST_CREATE_PERMISSIONS,
            &result
        )
        || result.result != MICROS_VFS_RESULT_OK
        || result.mode != MICROS_VFS_TEST_REGULAR_MODE
        || result.descriptor < 3
    ) {
        return false;
    }
    descriptor = result.descriptor;
    probe_clear_bytes(probe_buffer, sizeof(probe_buffer));
    probe_copy_bytes(probe_buffer, note, sizeof(note) - 1);
    if (
        !probe_transfer(
            vfs_endpoint,
            MICROS_VFS_MESSAGE_WRITE,
            descriptor,
            sizeof(note) - 1,
            &result
        )
        || result.result != MICROS_VFS_RESULT_OK
        || result.transferred_count != sizeof(note) - 1
        || result.position != sizeof(note) - 1
        || !probe_close(vfs_endpoint, descriptor)
        || !probe_open(
            vfs_endpoint,
            "/tmp/note",
            MICROS_VFS_OPEN_READ,
            0,
            &result
        )
        || result.result != MICROS_VFS_RESULT_OK
    ) {
        return false;
    }
    descriptor = result.descriptor;
    if (
        !probe_read_exact(
            vfs_endpoint,
            descriptor,
            note,
            sizeof(note) - 1,
            false
        )
        || !probe_close(vfs_endpoint, descriptor)
        || !probe_path_call(
            vfs_endpoint,
            MICROS_VFS_MESSAGE_CHDIR,
            "/tmp",
            0,
            &result
        )
        || result.result != MICROS_VFS_RESULT_OK
        || !probe_open(
            vfs_endpoint,
            "note",
            MICROS_VFS_OPEN_READ,
            0,
            &result
        )
        || result.result != MICROS_VFS_RESULT_OK
    ) {
        return false;
    }
    descriptor = result.descriptor;
    return (
        probe_read_exact(
            vfs_endpoint,
            descriptor,
            note,
            sizeof(note) - 1,
            false
        )
        && probe_close(vfs_endpoint, descriptor)
        && probe_path_call(
            vfs_endpoint,
            MICROS_VFS_MESSAGE_CHDIR,
            "/",
            0,
            &result
        )
        && result.result == MICROS_VFS_RESULT_OK
    );
}

static bool probe_test_console(
    micros_endpoint_t vfs_endpoint
)
{
    static const uint8_t input_trigger[] =
        MICROS_VFS_TEST_INPUT_TRIGGER;
    static const uint8_t expected_input[] =
        MICROS_VFS_TEST_EXPECTED_INPUT;
    static const uint8_t pass_marker[] = MICROS_VFS_TEST_PASS;
    struct probe_vfs_result result;

    if (
        !probe_write(
            vfs_endpoint,
            1,
            input_trigger,
            sizeof(input_trigger) - 1
        )
    ) {
        return false;
    }
    probe_clear_bytes(probe_buffer, sizeof(probe_buffer));
    if (
        !probe_transfer(
            vfs_endpoint,
            MICROS_VFS_MESSAGE_READ,
            0,
            sizeof(probe_buffer),
            &result
        )
        || result.result != MICROS_VFS_RESULT_OK
        || result.transferred_count != sizeof(expected_input) - 1
        || result.position != 0
        || !probe_bytes_equal(
            probe_buffer,
            expected_input,
            sizeof(expected_input) - 1
        )
    ) {
        return false;
    }
    return probe_write(
        vfs_endpoint,
        1,
        pass_marker,
        sizeof(pass_marker) - 1
    );
}

static bool probe_request_drain(
    micros_endpoint_t vfs_endpoint
)
{
    struct micros_ipc_message message;
    struct probe_vfs_result result;

    probe_clear_bytes(&message, sizeof(message));
    message.type = MICROS_VFS_TEST_MESSAGE_DRAIN;
    probe_write_u32_le(
        &message.payload[0],
        MICROS_VFS_PROTOCOL_VERSION
    );
    return (
        probe_call_vfs(
            vfs_endpoint,
            &message,
            MICROS_VFS_TEST_MESSAGE_DRAIN,
            &result
        )
        && result.result == MICROS_VFS_RESULT_OK
        && result.descriptor == 0
        && result.mode == 0
        && result.transferred_count == 0
        && result.position == 0
    );
}

void micros_vfs_service_test_report(uint64_t magic);

static void probe_report_failure(uint64_t magic)
{
    micros_vfs_service_test_report(magic);
    __builtin_trap();
}

void micros_service_main(void)
{
    micros_endpoint_t vfs_endpoint;

    if (
        probe_initialized_data[0]
            != MICROS_VFS_TEST_PROBE_DATA_MAGIC
        || !probe_validate_configuration()
    ) {
        probe_report_failure(MICROS_VFS_TEST_FAILURE_CONFIGURATION);
    }
    vfs_endpoint = micros_bootstrap_service_config.services[
        MICROS_VFS_TEST_VFS_SERVICE_ID - 1
    ].endpoint;
    if (!probe_send_ready()) {
        probe_report_failure(MICROS_VFS_TEST_FAILURE_READY);
    }
    if (!probe_test_filesystem(vfs_endpoint)) {
        probe_report_failure(MICROS_VFS_TEST_FAILURE_FILESYSTEM);
    }
    if (!probe_enumerate_root(vfs_endpoint)) {
        probe_report_failure(MICROS_VFS_TEST_FAILURE_DIRECTORY);
    }
    if (!probe_test_console(vfs_endpoint)) {
        probe_report_failure(MICROS_VFS_TEST_FAILURE_CONSOLE);
    }
    if (!probe_request_drain(vfs_endpoint)) {
        probe_report_failure(MICROS_VFS_TEST_FAILURE_DRAIN);
    }
    micros_vfs_service_test_report(MICROS_VFS_TEST_REPORT_MAGIC);
    __builtin_trap();
}

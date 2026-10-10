#include "micros/runtime.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/bootstrap_control.h"
#include "micros/ramfs.h"
#include "micros/tty.h"
#include "tests/qemu/ramfs_service_protocol.h"

volatile struct micros_bootstrap_service_config
    micros_bootstrap_service_config;

static uint8_t vfs_ramfs_buffer[MICROS_RAMFS_TRANSFER_MAX];
static uint8_t vfs_tty_buffer[256];
static volatile uint64_t vfs_initialized_data =
    UINT64_C(0x52414d4653564653);
static micros_endpoint_t vfs_ramfs_endpoint;
static micros_endpoint_t vfs_tty_endpoint;

struct vfs_ramfs_result {
    enum micros_ramfs_result result;
    uint64_t node;
    uint64_t file_size;
    uint64_t position;
    uint32_t count;
    uint32_t mode;
};

struct vfs_tty_result {
    enum micros_tty_result result;
    uint64_t request_id;
    uint64_t transferred_count;
};

static void vfs_clear_bytes(void *storage, size_t size)
{
    uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static void vfs_fill_bytes(uint8_t *bytes, uint8_t value, size_t size)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = value;
    }
}

static void vfs_copy_bytes(
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

static bool vfs_bytes_equal(
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

static bool vfs_bytes_are_zero(const uint8_t *bytes, size_t size)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static void vfs_write_u32_le(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void vfs_write_u64_le(uint8_t *bytes, uint64_t value)
{
    size_t index;

    for (index = 0; index < 8; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}

static uint32_t vfs_read_u32_le(const uint8_t *bytes)
{
    return (
        (uint32_t)bytes[0]
        | (uint32_t)bytes[1] << 8
        | (uint32_t)bytes[2] << 16
        | (uint32_t)bytes[3] << 24
    );
}

static uint64_t vfs_read_u64_le(const uint8_t *bytes)
{
    uint64_t value = 0;
    size_t index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8);
    }
    return value;
}

static bool vfs_endpoint_is_canonical(
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

static bool vfs_validate_configuration(void)
{
    size_t index;
    size_t other;

    if (
        micros_bootstrap_service_config.version
            != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || micros_bootstrap_service_config.manifest_version
            != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || micros_bootstrap_service_config.service_id
            != MICROS_RAMFS_TEST_VFS_SERVICE_ID
        || micros_bootstrap_service_config.service_count
            != MICROS_RAMFS_TEST_SERVICE_COUNT
        || micros_bootstrap_service_config.self_endpoint
            != micros_bootstrap_service_config.services[
                MICROS_RAMFS_TEST_VFS_SERVICE_ID - 1
            ].endpoint
        || micros_bootstrap_service_config.launcher_endpoint
            != micros_bootstrap_service_config.services[
                MICROS_RAMFS_TEST_LAUNCHER_SERVICE_ID - 1
            ].endpoint
        || micros_bootstrap_service_config.manifest_view_address != 0
    ) {
        return false;
    }
    for (index = 0; index < MICROS_RAMFS_TEST_SERVICE_COUNT; ++index) {
        if (
            micros_bootstrap_service_config.services[index].service_id
                != index + 1
            || !vfs_endpoint_is_canonical(
                micros_bootstrap_service_config.services[index].endpoint,
                index
            )
        ) {
            return false;
        }
        for (other = 0; other < index; ++other) {
            if (
                micros_bootstrap_service_config.services[index].endpoint
                    == micros_bootstrap_service_config.services[other].endpoint
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
    vfs_tty_endpoint = micros_bootstrap_service_config.services[
        MICROS_RAMFS_TEST_TTY_SERVICE_ID - 1
    ].endpoint;
    vfs_ramfs_endpoint = micros_bootstrap_service_config.services[
        MICROS_RAMFS_TEST_RAMFS_SERVICE_ID - 1
    ].endpoint;
    return true;
}

static bool vfs_send_ready(void)
{
    struct micros_ipc_message message;

    vfs_clear_bytes(&message, sizeof(message));
    message.type = MICROS_BOOTSTRAP_MESSAGE_READY;
    vfs_write_u32_le(
        &message.payload[0],
        MICROS_BOOTSTRAP_MANIFEST_VERSION
    );
    vfs_write_u32_le(
        &message.payload[4],
        MICROS_RAMFS_TEST_VFS_SERVICE_ID
    );
    vfs_write_u32_le(
        &message.payload[8],
        MICROS_BOOTSTRAP_MANIFEST_VERSION
    );
    vfs_write_u32_le(
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
        && vfs_read_u32_le(&message.payload[0])
            == MICROS_BOOTSTRAP_MANIFEST_VERSION
        && vfs_read_u32_le(&message.payload[4])
            == MICROS_RAMFS_TEST_VFS_SERVICE_ID
        && vfs_read_u32_le(&message.payload[8])
            == MICROS_BOOTSTRAP_MANIFEST_VERSION
        && vfs_read_u32_le(&message.payload[12]) == 0
        && vfs_read_u32_le(&message.payload[16])
            == micros_bootstrap_service_config.self_endpoint
        && vfs_bytes_are_zero(
            &message.payload[20],
            sizeof(message.payload) - 20
        )
    );
}

static void vfs_initialize_ramfs_request(
    struct micros_ipc_message *message,
    uint32_t type,
    uint32_t version
)
{
    vfs_clear_bytes(message, sizeof(*message));
    message->type = type;
    vfs_write_u32_le(&message->payload[0], version);
}

static bool vfs_ramfs_result_matches(
    const struct micros_ipc_message *message,
    uint32_t request_type,
    struct vfs_ramfs_result *result
)
{
    if (
        message == NULL
        || result == NULL
        || message->source != vfs_ramfs_endpoint
        || message->type != MICROS_RAMFS_MESSAGE_RESULT
        || message->reply_token != 0
        || vfs_read_u32_le(&message->payload[0])
            != MICROS_RAMFS_PROTOCOL_VERSION
        || vfs_read_u32_le(&message->payload[4]) != request_type
        || vfs_read_u32_le(&message->payload[12]) != 0
    ) {
        return false;
    }
    result->result = (enum micros_ramfs_result)(
        (int32_t)vfs_read_u32_le(&message->payload[8])
    );
    result->node = vfs_read_u64_le(&message->payload[16]);
    result->file_size = vfs_read_u64_le(&message->payload[24]);
    result->position = vfs_read_u64_le(&message->payload[32]);
    result->count = vfs_read_u32_le(&message->payload[40]);
    result->mode = vfs_read_u32_le(&message->payload[44]);
    if (
        result->result != MICROS_RAMFS_RESULT_OK
        && (
            result->node != 0
            || result->file_size != 0
            || result->position != 0
            || result->count != 0
            || result->mode != 0
        )
    ) {
        return false;
    }
    return true;
}

static bool vfs_call_ramfs(
    struct micros_ipc_message *message,
    uint32_t request_type,
    struct vfs_ramfs_result *result
)
{
    return (
        micros_runtime_call(vfs_ramfs_endpoint, message)
            == MICROS_SYSCALL_ABI_OK
        && vfs_ramfs_result_matches(message, request_type, result)
    );
}

static bool vfs_call_ramfs_with_grant(
    struct micros_ipc_message *message,
    uint32_t request_type,
    size_t grant_field_offset,
    uint32_t permissions,
    struct vfs_ramfs_result *result
)
{
    micros_grant_t grant = MICROS_GRANT_NONE;
    bool call_ok;
    bool revoke_ok;

    if (
        micros_runtime_grant_create(
            vfs_ramfs_endpoint,
            (uintptr_t)vfs_ramfs_buffer,
            sizeof(vfs_ramfs_buffer),
            permissions,
            &grant
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        return false;
    }
    vfs_write_u32_le(&message->payload[grant_field_offset], grant);
    call_ok = vfs_call_ramfs(message, request_type, result);
    revoke_ok = (
        micros_runtime_grant_revoke(grant) == MICROS_SYSCALL_ABI_OK
    );
    return call_ok && revoke_ok;
}

static bool vfs_result_has_no_fields(
    const struct vfs_ramfs_result *result
)
{
    return (
        result->node == 0
        && result->file_size == 0
        && result->position == 0
        && result->count == 0
        && result->mode == 0
    );
}

static bool vfs_mount(uint64_t *root)
{
    struct micros_ipc_message message;
    struct vfs_ramfs_result result;

    vfs_initialize_ramfs_request(
        &message,
        MICROS_RAMFS_MESSAGE_MOUNT,
        MICROS_RAMFS_PROTOCOL_VERSION
    );
    if (
        root == NULL
        || !vfs_call_ramfs(
            &message,
            MICROS_RAMFS_MESSAGE_MOUNT,
            &result
        )
        || result.result != MICROS_RAMFS_RESULT_OK
        || result.node == 0
        || result.file_size != 0
        || result.position != 0
        || result.count != 0
        || result.mode
            != (MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0755))
    ) {
        return false;
    }
    *root = result.node;
    return true;
}

static bool vfs_lookup(
    uint64_t start,
    uint64_t root,
    const uint8_t *path,
    size_t length,
    struct vfs_ramfs_result *result
)
{
    struct micros_ipc_message message;

    if (
        path == NULL
        || length == 0
        || length > sizeof(vfs_ramfs_buffer)
    ) {
        return false;
    }
    vfs_clear_bytes(vfs_ramfs_buffer, sizeof(vfs_ramfs_buffer));
    vfs_copy_bytes(vfs_ramfs_buffer, path, length);
    vfs_initialize_ramfs_request(
        &message,
        MICROS_RAMFS_MESSAGE_LOOKUP,
        MICROS_RAMFS_PROTOCOL_VERSION
    );
    vfs_write_u64_le(&message.payload[8], start);
    vfs_write_u64_le(&message.payload[16], root);
    vfs_write_u32_le(&message.payload[28], (uint32_t)length);
    return vfs_call_ramfs_with_grant(
        &message,
        MICROS_RAMFS_MESSAGE_LOOKUP,
        24,
        MICROS_GRANT_PERMISSION_READ,
        result
    );
}

static bool vfs_create_node(
    uint32_t type,
    uint64_t parent,
    uint32_t mode,
    const uint8_t *name,
    size_t length,
    struct vfs_ramfs_result *result
)
{
    struct micros_ipc_message message;

    if (
        name == NULL
        || length == 0
        || length > sizeof(vfs_ramfs_buffer)
    ) {
        return false;
    }
    vfs_clear_bytes(vfs_ramfs_buffer, sizeof(vfs_ramfs_buffer));
    vfs_copy_bytes(vfs_ramfs_buffer, name, length);
    vfs_initialize_ramfs_request(
        &message,
        type,
        MICROS_RAMFS_PROTOCOL_VERSION
    );
    vfs_write_u64_le(&message.payload[8], parent);
    vfs_write_u32_le(&message.payload[16], mode);
    vfs_write_u64_le(&message.payload[24], 0);
    vfs_write_u32_le(&message.payload[32], (uint32_t)length);
    return vfs_call_ramfs_with_grant(
        &message,
        type,
        20,
        MICROS_GRANT_PERMISSION_READ,
        result
    );
}

static bool vfs_transfer(
    uint32_t type,
    uint64_t node,
    uint64_t file_offset,
    uint32_t count,
    uint32_t permissions,
    struct vfs_ramfs_result *result
)
{
    struct micros_ipc_message message;

    vfs_initialize_ramfs_request(
        &message,
        type,
        MICROS_RAMFS_PROTOCOL_VERSION
    );
    vfs_write_u64_le(&message.payload[8], node);
    vfs_write_u64_le(&message.payload[16], file_offset);
    vfs_write_u32_le(&message.payload[28], count);
    return vfs_call_ramfs_with_grant(
        &message,
        type,
        24,
        permissions,
        result
    );
}

static bool vfs_transfer_without_grant(
    uint32_t type,
    uint64_t node,
    uint64_t file_offset,
    uint32_t count,
    struct vfs_ramfs_result *result
)
{
    struct micros_ipc_message message;

    vfs_initialize_ramfs_request(
        &message,
        type,
        MICROS_RAMFS_PROTOCOL_VERSION
    );
    vfs_write_u64_le(&message.payload[8], node);
    vfs_write_u64_le(&message.payload[16], file_offset);
    vfs_write_u32_le(&message.payload[24], MICROS_GRANT_NONE);
    vfs_write_u32_le(&message.payload[28], count);
    return vfs_call_ramfs(&message, type, result);
}

static bool vfs_getdent(
    uint64_t node,
    uint64_t cursor,
    struct vfs_ramfs_result *result
)
{
    struct micros_ipc_message message;

    vfs_fill_bytes(
        vfs_ramfs_buffer,
        UINT8_C(0xa5),
        sizeof(vfs_ramfs_buffer)
    );
    vfs_initialize_ramfs_request(
        &message,
        MICROS_RAMFS_MESSAGE_GETDENTS,
        MICROS_RAMFS_PROTOCOL_VERSION
    );
    vfs_write_u64_le(&message.payload[8], node);
    vfs_write_u64_le(&message.payload[16], cursor);
    vfs_write_u32_le(
        &message.payload[28],
        MICROS_RAMFS_DIRECTORY_RECORD_SIZE
    );
    return vfs_call_ramfs_with_grant(
        &message,
        MICROS_RAMFS_MESSAGE_GETDENTS,
        24,
        MICROS_GRANT_PERMISSION_WRITE,
        result
    );
}

static bool vfs_putnode(
    uint64_t node,
    uint32_t count,
    enum micros_ramfs_result expected
)
{
    struct micros_ipc_message message;
    struct vfs_ramfs_result result;

    vfs_initialize_ramfs_request(
        &message,
        MICROS_RAMFS_MESSAGE_PUTNODE,
        MICROS_RAMFS_PROTOCOL_VERSION
    );
    vfs_write_u64_le(&message.payload[8], node);
    vfs_write_u32_le(&message.payload[16], count);
    return (
        vfs_call_ramfs(
            &message,
            MICROS_RAMFS_MESSAGE_PUTNODE,
            &result
        )
        && result.result == expected
        && vfs_result_has_no_fields(&result)
    );
}

static bool vfs_malformed_version_is_exact(uint64_t root)
{
    struct micros_ipc_message message;
    struct vfs_ramfs_result result;

    vfs_initialize_ramfs_request(
        &message,
        MICROS_RAMFS_MESSAGE_PUTNODE,
        MICROS_RAMFS_PROTOCOL_VERSION + 1
    );
    vfs_write_u64_le(&message.payload[8], root);
    vfs_write_u32_le(&message.payload[16], 1);
    return (
        vfs_call_ramfs(
            &message,
            MICROS_RAMFS_MESSAGE_PUTNODE,
            &result
        )
        && result.result == MICROS_RAMFS_RESULT_BAD_VERSION
        && vfs_result_has_no_fields(&result)
    );
}

static bool vfs_directory_record_matches(
    const uint8_t *name,
    size_t name_length
)
{
    if (
        vfs_read_u64_le(&vfs_ramfs_buffer[0]) == 0
        || (
            vfs_read_u32_le(&vfs_ramfs_buffer[8])
            & MICROS_RAMFS_MODE_TYPE_MASK
        ) != MICROS_RAMFS_MODE_DIRECTORY
        || vfs_read_u32_le(&vfs_ramfs_buffer[12]) != name_length
        || !vfs_bytes_equal(
            &vfs_ramfs_buffer[16],
            name,
            name_length
        )
    ) {
        return false;
    }
    return vfs_bytes_are_zero(
        &vfs_ramfs_buffer[16 + name_length],
        MICROS_RAMFS_DIRECTORY_RECORD_SIZE - 16 - name_length
    );
}

static bool vfs_enumerate_root(uint64_t root)
{
    static const uint8_t *const names[] = {
        (const uint8_t *)".",
        (const uint8_t *)"..",
        (const uint8_t *)"etc",
        (const uint8_t *)"tmp",
    };
    static const size_t lengths[] = {1, 2, 3, 3};
    struct vfs_ramfs_result result;
    uint64_t cursor = 0;
    size_t index;

    for (index = 0; index < sizeof(lengths) / sizeof(lengths[0]); ++index) {
        if (
            !vfs_getdent(root, cursor, &result)
            || result.result != MICROS_RAMFS_RESULT_OK
            || result.node != 0
            || result.file_size != 0
            || result.count != MICROS_RAMFS_DIRECTORY_RECORD_SIZE
            || result.mode != 0
            || !vfs_directory_record_matches(names[index], lengths[index])
        ) {
            return false;
        }
        cursor = result.position;
    }
    return cursor == MICROS_RAMFS_DIRECTORY_CURSOR_END;
}

static bool vfs_run_filesystem_scenario(void)
{
    static const uint8_t motd_path[] = "/etc/motd";
    static const uint8_t motd[] = MICROS_RAMFS_TEST_MOTD;
    static const uint8_t tmp_name[] = "tmp";
    static const uint8_t note_name[] = "note";
    static const uint8_t note_data[] = MICROS_RAMFS_TEST_NOTE_DATA;
    static const uint8_t sparse_read[] = {
        0,
        0,
        0,
        'n',
        'o',
        't',
        'e',
    };
    struct vfs_ramfs_result result;
    uint64_t root;
    uint64_t motd_node;
    uint64_t tmp_node;
    uint64_t note_node;
    uint64_t stale_node;

    if (
        !vfs_mount(&root)
        || !vfs_malformed_version_is_exact(root)
        || !vfs_lookup(
            root,
            root,
            motd_path,
            sizeof(motd_path),
            &result
        )
        || result.result != MICROS_RAMFS_RESULT_OK
        || result.file_size != sizeof(motd) - 1
        || result.mode
            != (MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644))
    ) {
        return false;
    }
    motd_node = result.node;
    vfs_fill_bytes(
        vfs_ramfs_buffer,
        UINT8_C(0xa5),
        sizeof(vfs_ramfs_buffer)
    );
    if (
        !vfs_transfer(
            MICROS_RAMFS_MESSAGE_READ,
            motd_node,
            0,
            sizeof(motd) - 1,
            MICROS_GRANT_PERMISSION_READ,
            &result
        )
        || result.result != MICROS_RAMFS_RESULT_GRANT
        || !vfs_result_has_no_fields(&result)
        || !vfs_transfer(
            MICROS_RAMFS_MESSAGE_READ,
            motd_node,
            0,
            sizeof(motd),
            MICROS_GRANT_PERMISSION_WRITE,
            &result
        )
        || result.result != MICROS_RAMFS_RESULT_OK
        || result.file_size != sizeof(motd) - 1
        || result.position != sizeof(motd) - 1
        || result.count != sizeof(motd) - 1
        || !vfs_bytes_equal(
            vfs_ramfs_buffer,
            motd,
            sizeof(motd) - 1
        )
        || !vfs_transfer(
            MICROS_RAMFS_MESSAGE_READ,
            motd_node,
            sizeof(motd) - 1,
            1,
            MICROS_GRANT_PERMISSION_WRITE,
            &result
        )
        || result.result != MICROS_RAMFS_RESULT_OK
        || result.file_size != sizeof(motd) - 1
        || result.position != sizeof(motd) - 1
        || result.count != 0
    ) {
        return false;
    }
    stale_node = motd_node + (UINT64_C(1) << 32);
    if (
        !vfs_transfer_without_grant(
            MICROS_RAMFS_MESSAGE_READ,
            stale_node,
            0,
            1,
            &result
        )
        || result.result != MICROS_RAMFS_RESULT_NODE
        || !vfs_result_has_no_fields(&result)
        || !vfs_putnode(motd_node, 1, MICROS_RAMFS_RESULT_OK)
        || !vfs_create_node(
            MICROS_RAMFS_MESSAGE_MKDIR,
            root,
            MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0755),
            tmp_name,
            sizeof(tmp_name),
            &result
        )
        || result.result != MICROS_RAMFS_RESULT_OK
        || !vfs_result_has_no_fields(&result)
        || !vfs_lookup(
            root,
            root,
            tmp_name,
            sizeof(tmp_name),
            &result
        )
        || result.result != MICROS_RAMFS_RESULT_OK
        || result.file_size != 0
        || result.mode
            != (MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0755))
    ) {
        return false;
    }
    tmp_node = result.node;
    if (
        !vfs_create_node(
            MICROS_RAMFS_MESSAGE_CREATE,
            tmp_node,
            MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
            note_name,
            sizeof(note_name),
            &result
        )
        || result.result != MICROS_RAMFS_RESULT_OK
        || result.file_size != 0
        || result.position != 0
        || result.count != 0
        || result.mode
            != (MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644))
    ) {
        return false;
    }
    note_node = result.node;
    if (
        !vfs_putnode(note_node, 2, MICROS_RAMFS_RESULT_REFERENCE)
    ) {
        return false;
    }
    vfs_clear_bytes(vfs_ramfs_buffer, sizeof(vfs_ramfs_buffer));
    vfs_copy_bytes(
        vfs_ramfs_buffer,
        note_data,
        sizeof(note_data) - 1
    );
    if (
        !vfs_transfer(
            MICROS_RAMFS_MESSAGE_WRITE,
            note_node,
            MICROS_RAMFS_TEST_SPARSE_OFFSET,
            sizeof(note_data) - 1,
            MICROS_GRANT_PERMISSION_READ,
            &result
        )
        || result.result != MICROS_RAMFS_RESULT_OK
        || result.file_size
            != MICROS_RAMFS_TEST_SPARSE_OFFSET
                + sizeof(note_data) - 1
        || result.position != result.file_size
        || result.count != sizeof(note_data) - 1
        || !vfs_transfer(
            MICROS_RAMFS_MESSAGE_READ,
            note_node,
            MICROS_RAMFS_BLOCK_SIZE,
            sizeof(sparse_read),
            MICROS_GRANT_PERMISSION_WRITE,
            &result
        )
        || result.result != MICROS_RAMFS_RESULT_OK
        || result.file_size
            != MICROS_RAMFS_TEST_SPARSE_OFFSET
                + sizeof(note_data) - 1
        || result.position
            != MICROS_RAMFS_BLOCK_SIZE + sizeof(sparse_read)
        || result.count != sizeof(sparse_read)
        || !vfs_bytes_equal(
            vfs_ramfs_buffer,
            sparse_read,
            sizeof(sparse_read)
        )
        || !vfs_enumerate_root(root)
        || !vfs_putnode(tmp_node, 1, MICROS_RAMFS_RESULT_OK)
        || !vfs_putnode(note_node, 1, MICROS_RAMFS_RESULT_OK)
    ) {
        return false;
    }
    return true;
}

static bool vfs_tty_result_matches(
    const struct micros_ipc_message *message,
    uint32_t request_type,
    struct vfs_tty_result *result
)
{
    if (
        message == NULL
        || result == NULL
        || message->source != vfs_tty_endpoint
        || message->type != MICROS_TTY_MESSAGE_RESULT
        || message->reply_token != 0
        || vfs_read_u32_le(&message->payload[0])
            != MICROS_TTY_PROTOCOL_VERSION
        || vfs_read_u32_le(&message->payload[4]) != request_type
        || vfs_read_u32_le(&message->payload[12]) != 0
        || !vfs_bytes_are_zero(
            &message->payload[32],
            sizeof(message->payload) - 32
        )
    ) {
        return false;
    }
    result->result = (enum micros_tty_result)(
        (int32_t)vfs_read_u32_le(&message->payload[8])
    );
    result->request_id = vfs_read_u64_le(&message->payload[16]);
    result->transferred_count =
        vfs_read_u64_le(&message->payload[24]);
    return true;
}

static bool vfs_call_tty(
    uint32_t type,
    uint64_t request_id,
    micros_grant_t grant,
    uint64_t count,
    struct vfs_tty_result *result
)
{
    struct micros_ipc_message message;

    vfs_clear_bytes(&message, sizeof(message));
    message.type = type;
    vfs_write_u32_le(
        &message.payload[0],
        MICROS_TTY_PROTOCOL_VERSION
    );
    vfs_write_u64_le(&message.payload[8], request_id);
    if (
        type == MICROS_TTY_MESSAGE_SUBMIT_READ
        || type == MICROS_TTY_MESSAGE_SUBMIT_WRITE
    ) {
        vfs_write_u32_le(&message.payload[16], grant);
        vfs_write_u64_le(&message.payload[24], 0);
        vfs_write_u64_le(&message.payload[32], count);
    }
    return (
        micros_runtime_call(vfs_tty_endpoint, &message)
            == MICROS_SYSCALL_ABI_OK
        && vfs_tty_result_matches(&message, type, result)
        && result->request_id == request_id
    );
}

static bool vfs_receive_tty_events(uint64_t *events)
{
    struct micros_ipc_message message;
    uint64_t observed;

    vfs_clear_bytes(&message, sizeof(message));
    if (
        events == NULL
        || micros_runtime_receive(MICROS_ENDPOINT_ANY, &message)
            != MICROS_SYSCALL_ABI_OK
    ) {
        return false;
    }
    observed = vfs_read_u64_le(&message.payload[0]);
    if (
        message.source != vfs_tty_endpoint
        || message.type != MICROS_IPC_TYPE_KERNEL_NOTIFICATION
        || message.reply_token != 0
        || observed == 0
        || (
            observed
            & ~(
                MICROS_TTY_EVENT_COMPLETION
                | MICROS_TTY_EVENT_WRITABLE
            )
        ) != 0
        || !vfs_bytes_are_zero(
            &message.payload[8],
            sizeof(message.payload) - 8
        )
    ) {
        return false;
    }
    *events = observed;
    return true;
}

static bool vfs_collect_tty_write(
    uint64_t request_id,
    uint64_t expected_count
)
{
    struct vfs_tty_result result;
    uint64_t events;

    for (;;) {
        if (
            !vfs_call_tty(
                MICROS_TTY_MESSAGE_COLLECT,
                request_id,
                MICROS_GRANT_NONE,
                0,
                &result
            )
        ) {
            return false;
        }
        if (result.result == MICROS_TTY_RESULT_OK) {
            return result.transferred_count == expected_count;
        }
        if (
            result.result != MICROS_TTY_RESULT_PENDING
            || result.transferred_count != 0
            || !vfs_receive_tty_events(&events)
            || (events & MICROS_TTY_EVENT_COMPLETION) == 0
        ) {
            return false;
        }
    }
}

static bool vfs_wait_for_tty_drain(void)
{
    struct vfs_tty_result result;
    uint64_t events;

    if (
        !vfs_call_tty(
            MICROS_TTY_MESSAGE_SUBMIT_WRITE,
            UINT64_C(2),
            MICROS_GRANT_NONE,
            1,
            &result
        )
    ) {
        return false;
    }
    if (result.result == MICROS_TTY_RESULT_GRANT) {
        return result.transferred_count == 0;
    }
    if (
        result.result != MICROS_TTY_RESULT_BUSY
        || result.transferred_count != 0
    ) {
        return false;
    }
    do {
        if (!vfs_receive_tty_events(&events)) {
            return false;
        }
    } while ((events & MICROS_TTY_EVENT_WRITABLE) == 0);
    return (
        vfs_call_tty(
            MICROS_TTY_MESSAGE_SUBMIT_WRITE,
            UINT64_C(2),
            MICROS_GRANT_NONE,
            1,
            &result
        )
        && result.result == MICROS_TTY_RESULT_GRANT
        && result.transferred_count == 0
    );
}

static bool vfs_write_pass_marker(void)
{
    static const uint8_t marker[] = MICROS_RAMFS_TEST_PASS;
    struct vfs_tty_result result;
    micros_grant_t grant = MICROS_GRANT_NONE;
    bool write_ok;
    bool revoke_ok;

    if (sizeof(marker) - 1 > sizeof(vfs_tty_buffer)) {
        return false;
    }
    vfs_clear_bytes(vfs_tty_buffer, sizeof(vfs_tty_buffer));
    vfs_copy_bytes(vfs_tty_buffer, marker, sizeof(marker) - 1);
    if (
        micros_runtime_grant_create(
            vfs_tty_endpoint,
            (uintptr_t)vfs_tty_buffer,
            sizeof(vfs_tty_buffer),
            MICROS_GRANT_PERMISSION_READ,
            &grant
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        return false;
    }
    write_ok = (
        vfs_call_tty(
            MICROS_TTY_MESSAGE_SUBMIT_WRITE,
            UINT64_C(1),
            grant,
            sizeof(marker) - 1,
            &result
        )
        && result.result == MICROS_TTY_RESULT_OK
        && result.transferred_count == 0
        && vfs_collect_tty_write(
            UINT64_C(1),
            sizeof(marker) - 1
        )
        && vfs_wait_for_tty_drain()
    );
    revoke_ok = (
        micros_runtime_grant_revoke(grant) == MICROS_SYSCALL_ABI_OK
    );
    return write_ok && revoke_ok;
}

void micros_ramfs_service_test_report(uint64_t magic);

static _Noreturn void vfs_report_and_stop(uint64_t magic)
{
    micros_ramfs_service_test_report(magic);
    for (;;) {
    }
}

void micros_service_main(void)
{
    if (
        vfs_initialized_data != UINT64_C(0x52414d4653564653)
        || !vfs_validate_configuration()
    ) {
        vfs_report_and_stop(
            MICROS_RAMFS_TEST_FAILURE_CONFIGURATION
        );
    }
    micros_ramfs_service_test_report(
        MICROS_RAMFS_TEST_REPORT_PRE_READY
    );
    if (!vfs_run_filesystem_scenario()) {
        vfs_report_and_stop(
            MICROS_RAMFS_TEST_FAILURE_FILESYSTEM
        );
    }
    if (!vfs_send_ready()) {
        vfs_report_and_stop(
            MICROS_RAMFS_TEST_FAILURE_READY
        );
    }
    if (!vfs_write_pass_marker()) {
        vfs_report_and_stop(
            MICROS_RAMFS_TEST_FAILURE_TTY
        );
    }
    vfs_report_and_stop(
        MICROS_RAMFS_TEST_REPORT_MAGIC
    );
}

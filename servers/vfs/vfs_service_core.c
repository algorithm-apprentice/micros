#include "servers/vfs/vfs_service_core.h"

#include <stddef.h>
#include <stdint.h>

static void clear_bytes(void *storage, size_t size)
{
    uint8_t *bytes = storage;
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

static bool endpoint_is_canonical(micros_endpoint_t endpoint)
{
    uint32_t slot_mask =
        (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS) - 1;
    uint32_t generation = endpoint >> MICROS_ENDPOINT_SLOT_BITS;

    return (
        endpoint != MICROS_ENDPOINT_NONE
        && endpoint != MICROS_ENDPOINT_ANY
        && (endpoint & slot_mask) < MICROS_PROCESS_CAPACITY
        && generation != 0
        && generation <= MICROS_ENDPOINT_GENERATION_MAX
    );
}

static bool application_type_is_known(uint32_t type)
{
    switch (type) {
    case MICROS_VFS_MESSAGE_OPEN:
    case MICROS_VFS_MESSAGE_CLOSE:
    case MICROS_VFS_MESSAGE_READ:
    case MICROS_VFS_MESSAGE_WRITE:
    case MICROS_VFS_MESSAGE_GETDENTS:
    case MICROS_VFS_MESSAGE_MKDIR:
    case MICROS_VFS_MESSAGE_CHDIR:
        return true;
    default:
        return false;
    }
}

static bool vfs_result_is_defined(enum micros_vfs_result result)
{
    return (
        result >= MICROS_VFS_RESULT_RANGE
        && result <= MICROS_VFS_RESULT_OK
    );
}

static bool ramfs_result_is_defined(enum micros_ramfs_result result)
{
    return (
        result >= MICROS_RAMFS_RESULT_REFERENCE
        && result <= MICROS_RAMFS_RESULT_OK
    );
}

static bool tty_result_is_defined(enum micros_tty_result result)
{
    return (
        result >= MICROS_TTY_RESULT_PENDING
        && result <= MICROS_TTY_RESULT_OK
    );
}

static bool result_fields_are_zero(
    const struct micros_vfs_result_action *result
)
{
    return (
        result->descriptor == 0
        && result->mode == 0
        && result->transferred_count == 0
        && result->position == 0
    );
}

static uint32_t ramfs_message_type(
    enum micros_vfs_ramfs_operation operation
)
{
    switch (operation) {
    case MICROS_VFS_RAMFS_MOUNT:
        return MICROS_RAMFS_MESSAGE_MOUNT;
    case MICROS_VFS_RAMFS_LOOKUP:
        return MICROS_RAMFS_MESSAGE_LOOKUP;
    case MICROS_VFS_RAMFS_CREATE:
        return MICROS_RAMFS_MESSAGE_CREATE;
    case MICROS_VFS_RAMFS_MKDIR:
        return MICROS_RAMFS_MESSAGE_MKDIR;
    case MICROS_VFS_RAMFS_READ:
        return MICROS_RAMFS_MESSAGE_READ;
    case MICROS_VFS_RAMFS_WRITE:
        return MICROS_RAMFS_MESSAGE_WRITE;
    case MICROS_VFS_RAMFS_GETDENTS:
        return MICROS_RAMFS_MESSAGE_GETDENTS;
    case MICROS_VFS_RAMFS_PUTNODE:
        return MICROS_RAMFS_MESSAGE_PUTNODE;
    }
    return 0;
}

static uint32_t tty_message_type(
    enum micros_vfs_tty_operation operation
)
{
    switch (operation) {
    case MICROS_VFS_TTY_SUBMIT_READ:
        return MICROS_TTY_MESSAGE_SUBMIT_READ;
    case MICROS_VFS_TTY_SUBMIT_WRITE:
        return MICROS_TTY_MESSAGE_SUBMIT_WRITE;
    case MICROS_VFS_TTY_CANCEL:
        return MICROS_TTY_MESSAGE_CANCEL;
    case MICROS_VFS_TTY_COLLECT:
        return MICROS_TTY_MESSAGE_COLLECT;
    }
    return 0;
}

static bool ramfs_mode_is_canonical(
    uint32_t mode,
    uint32_t type
)
{
    return (
        (mode & MICROS_RAMFS_MODE_TYPE_MASK) == type
        && (
            mode
            & ~(
                MICROS_RAMFS_MODE_TYPE_MASK
                | MICROS_RAMFS_MODE_PERMISSIONS
            )
        ) == 0
    );
}

static bool ramfs_request_is_exact(
    const struct micros_vfs_ramfs_request *request
)
{
    if (request == NULL) {
        return false;
    }
    switch (request->operation) {
    case MICROS_VFS_RAMFS_MOUNT:
        return (
            request->node == 0
            && request->start == 0
            && request->root == 0
            && request->file_offset == 0
            && request->cursor == 0
            && request->grant_offset == 0
            && request->mode == 0
            && request->grant == 0
            && request->length == 0
            && request->count == 0
        );
    case MICROS_VFS_RAMFS_LOOKUP:
        return (
            request->node == 0
            && request->start != 0
            && request->root != 0
            && request->file_offset == 0
            && request->cursor == 0
            && request->mode == 0
            && request->grant != 0
            && request->grant != MICROS_GRANT_NONE
            && request->length >= 2
            && request->length <= MICROS_RAMFS_PATH_MAX
            && request->count == 0
        );
    case MICROS_VFS_RAMFS_CREATE:
    case MICROS_VFS_RAMFS_MKDIR:
        return (
            request->node != 0
            && request->start == 0
            && request->root == 0
            && request->file_offset == 0
            && request->cursor == 0
            && ramfs_mode_is_canonical(
                request->mode,
                request->operation == MICROS_VFS_RAMFS_CREATE
                    ? MICROS_RAMFS_MODE_REGULAR
                    : MICROS_RAMFS_MODE_DIRECTORY
            )
            && request->grant != 0
            && request->grant != MICROS_GRANT_NONE
            && request->length >= 2
            && request->length <= MICROS_RAMFS_NAME_MAX + 1
            && request->count == 0
        );
    case MICROS_VFS_RAMFS_READ:
    case MICROS_VFS_RAMFS_WRITE:
        return (
            request->node != 0
            && request->start == 0
            && request->root == 0
            && request->cursor == 0
            && request->mode == 0
            && request->grant != 0
            && request->grant != MICROS_GRANT_NONE
            && request->length == 0
            && request->count >= 1
            && request->count <= MICROS_RAMFS_TRANSFER_MAX
        );
    case MICROS_VFS_RAMFS_GETDENTS:
        return (
            request->node != 0
            && request->start == 0
            && request->root == 0
            && request->file_offset == 0
            && request->cursor <= MICROS_RAMFS_DIRECTORY_CURSOR_END
            && request->mode == 0
            && request->grant != 0
            && request->grant != MICROS_GRANT_NONE
            && request->length == 0
            && request->count
                >= MICROS_RAMFS_DIRECTORY_RECORD_SIZE
            && request->count <= MICROS_RAMFS_TRANSFER_MAX
            && request->count
                % MICROS_RAMFS_DIRECTORY_RECORD_SIZE == 0
        );
    case MICROS_VFS_RAMFS_PUTNODE:
        return (
            request->node != 0
            && request->start == 0
            && request->root == 0
            && request->file_offset == 0
            && request->cursor == 0
            && request->grant_offset == 0
            && request->mode == 0
            && request->grant == 0
            && request->length == 0
            && request->count != 0
        );
    }
    return false;
}

static bool ramfs_response_fields_are_exact(
    const struct micros_vfs_ramfs_request *request,
    const struct micros_vfs_ramfs_response *response
)
{
    if (
        request == NULL
        || response == NULL
        || response->result != MICROS_RAMFS_RESULT_OK
    ) {
        return false;
    }
    switch (request->operation) {
    case MICROS_VFS_RAMFS_MOUNT:
        return (
            response->node != 0
            && response->file_size == 0
            && response->position == 0
            && response->count == 0
            && ramfs_mode_is_canonical(
                response->mode,
                MICROS_RAMFS_MODE_DIRECTORY
            )
        );
    case MICROS_VFS_RAMFS_LOOKUP:
        return (
            response->node != 0
            && response->position == 0
            && response->count == 0
            && (
                ramfs_mode_is_canonical(
                    response->mode,
                    MICROS_RAMFS_MODE_REGULAR
                )
                || ramfs_mode_is_canonical(
                    response->mode,
                    MICROS_RAMFS_MODE_DIRECTORY
                )
            )
        );
    case MICROS_VFS_RAMFS_CREATE:
        return (
            response->node != 0
            && response->file_size == 0
            && response->position == 0
            && response->count == 0
            && ramfs_mode_is_canonical(
                response->mode,
                MICROS_RAMFS_MODE_REGULAR
            )
        );
    case MICROS_VFS_RAMFS_MKDIR:
    case MICROS_VFS_RAMFS_PUTNODE:
        return (
            response->node == 0
            && response->file_size == 0
            && response->position == 0
            && response->count == 0
            && response->mode == 0
        );
    case MICROS_VFS_RAMFS_READ:
    case MICROS_VFS_RAMFS_WRITE:
        return (
            response->node == 0
            && response->count <= MICROS_RAMFS_TRANSFER_MAX
            && response->mode == 0
        );
    case MICROS_VFS_RAMFS_GETDENTS:
        return (
            response->node == 0
            && response->file_size == 0
            && response->count <= MICROS_RAMFS_TRANSFER_MAX
            && response->count
                % MICROS_RAMFS_DIRECTORY_RECORD_SIZE == 0
            && response->mode == 0
        );
    }
    return false;
}

static bool tty_request_is_exact(
    const struct micros_vfs_tty_request *request
)
{
    if (request == NULL || request->request_id == 0) {
        return false;
    }
    if (
        request->operation == MICROS_VFS_TTY_SUBMIT_READ
        || request->operation == MICROS_VFS_TTY_SUBMIT_WRITE
    ) {
        return (
            request->count >= 1
            && request->count <= MICROS_TTY_TRANSFER_MAX
        );
    }
    if (
        request->operation == MICROS_VFS_TTY_CANCEL
        || request->operation == MICROS_VFS_TTY_COLLECT
    ) {
        return (
            request->grant == 0
            && request->count == 0
        );
    }
    return false;
}

bool micros_vfs_service_validate_configuration(
    const volatile struct micros_bootstrap_service_config *config,
    struct micros_vfs_service_endpoints *endpoints
)
{
    struct micros_vfs_service_endpoints selected;
    size_t index;
    size_t other;

    if (
        config == NULL
        || endpoints == NULL
        || config->version != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || config->manifest_version
            != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || config->service_id != MICROS_VFS_SERVICE_ID
        || config->service_count != MICROS_VFS_SERVICE_ID
        || config->self_endpoint
            != config->services[MICROS_VFS_SERVICE_ID - 1].endpoint
        || config->launcher_endpoint != config->services[0].endpoint
        || config->manifest_view_address != 0
    ) {
        return false;
    }
    for (index = 0; index < MICROS_VFS_SERVICE_ID; ++index) {
        const volatile struct micros_bootstrap_service_endpoint *service =
            &config->services[index];

        if (
            service->service_id != index + 1
            || !endpoint_is_canonical(service->endpoint)
        ) {
            return false;
        }
        for (other = 0; other < index; ++other) {
            if (
                service->endpoint
                    == config->services[other].endpoint
            ) {
                return false;
            }
        }
    }
    for (
        index = MICROS_VFS_SERVICE_ID;
        index < MICROS_BOOTSTRAP_SERVICE_CAPACITY;
        ++index
    ) {
        if (
            config->services[index].service_id != 0
            || config->services[index].endpoint != 0
        ) {
            return false;
        }
    }
    for (
        index = 0;
        index < sizeof(config->reserved) / sizeof(config->reserved[0]);
        ++index
    ) {
        if (config->reserved[index] != 0) {
            return false;
        }
    }
    selected.launcher = config->services[0].endpoint;
    selected.tty = config->services[MICROS_TTY_SERVICE_ID - 1].endpoint;
    selected.ramfs =
        config->services[MICROS_RAMFS_SERVICE_ID - 1].endpoint;
    selected.self =
        config->services[MICROS_VFS_SERVICE_ID - 1].endpoint;
    if (
        selected.launcher != config->launcher_endpoint
        || selected.self != config->self_endpoint
    ) {
        return false;
    }
    *endpoints = selected;
    return true;
}

enum micros_vfs_protocol_status micros_vfs_service_decode_request(
    const struct micros_ipc_message *message,
    struct micros_vfs_request *request
)
{
    struct micros_vfs_request decoded;
    uint32_t version;
    uint32_t flags;

    if (message == NULL || request == NULL) {
        return MICROS_VFS_PROTOCOL_INVARIANT;
    }
    if ((message->type & MICROS_IPC_TYPE_KERNEL_MASK) != 0) {
        return MICROS_VFS_PROTOCOL_INVARIANT;
    }
    if (!application_type_is_known(message->type)) {
        return MICROS_VFS_PROTOCOL_BAD_TYPE;
    }
    version = read_u32_le(&message->payload[0]);
    if (version != MICROS_VFS_PROTOCOL_VERSION) {
        return MICROS_VFS_PROTOCOL_BAD_VERSION;
    }
    flags = read_u32_le(&message->payload[4]);
    if (flags != 0) {
        return MICROS_VFS_PROTOCOL_MALFORMED;
    }

    clear_bytes(&decoded, sizeof(decoded));
    decoded.type = message->type;
    decoded.version = version;
    decoded.flags = flags;
    decoded.source = message->source;
    decoded.reply_token = message->reply_token;
    switch (message->type) {
    case MICROS_VFS_MESSAGE_OPEN:
        if (!bytes_are_zero(&message->payload[32], 16)) {
            return MICROS_VFS_PROTOCOL_MALFORMED;
        }
        decoded.grant = read_u32_le(&message->payload[8]);
        decoded.path_length = read_u32_le(&message->payload[12]);
        decoded.grant_offset = read_u64_le(&message->payload[16]);
        decoded.open_flags = read_u32_le(&message->payload[24]);
        decoded.mode = read_u32_le(&message->payload[28]);
        break;
    case MICROS_VFS_MESSAGE_CLOSE:
        if (!bytes_are_zero(&message->payload[12], 36)) {
            return MICROS_VFS_PROTOCOL_MALFORMED;
        }
        decoded.descriptor = read_u32_le(&message->payload[8]);
        break;
    case MICROS_VFS_MESSAGE_READ:
    case MICROS_VFS_MESSAGE_WRITE:
    case MICROS_VFS_MESSAGE_GETDENTS:
        if (!bytes_are_zero(&message->payload[28], 20)) {
            return MICROS_VFS_PROTOCOL_MALFORMED;
        }
        decoded.descriptor = read_u32_le(&message->payload[8]);
        decoded.grant = read_u32_le(&message->payload[12]);
        decoded.grant_offset = read_u64_le(&message->payload[16]);
        decoded.count = read_u32_le(&message->payload[24]);
        break;
    case MICROS_VFS_MESSAGE_MKDIR:
        if (!bytes_are_zero(&message->payload[28], 20)) {
            return MICROS_VFS_PROTOCOL_MALFORMED;
        }
        decoded.grant = read_u32_le(&message->payload[8]);
        decoded.path_length = read_u32_le(&message->payload[12]);
        decoded.grant_offset = read_u64_le(&message->payload[16]);
        decoded.mode = read_u32_le(&message->payload[24]);
        break;
    case MICROS_VFS_MESSAGE_CHDIR:
        if (!bytes_are_zero(&message->payload[24], 24)) {
            return MICROS_VFS_PROTOCOL_MALFORMED;
        }
        decoded.grant = read_u32_le(&message->payload[8]);
        decoded.path_length = read_u32_le(&message->payload[12]);
        decoded.grant_offset = read_u64_le(&message->payload[16]);
        break;
    default:
        return MICROS_VFS_PROTOCOL_INVARIANT;
    }
    if (message->reply_token == 0) {
        return MICROS_VFS_PROTOCOL_INVARIANT;
    }
    *request = decoded;
    return MICROS_VFS_PROTOCOL_OK;
}

enum micros_vfs_service_error micros_vfs_service_build_result(
    const struct micros_vfs_result_action *result,
    struct micros_ipc_message *message
)
{
    struct micros_ipc_message built;

    if (
        result == NULL
        || message == NULL
        || !result->active
        || result->reply_token == 0
        || (
            result->request_type & MICROS_IPC_TYPE_KERNEL_MASK
        ) != 0
        || !vfs_result_is_defined(result->result)
    ) {
        return MICROS_VFS_SERVICE_ERROR_ARGUMENT;
    }
    if (
        (
            result->result != MICROS_VFS_RESULT_OK
            && !result_fields_are_zero(result)
        )
        || (
            result->result == MICROS_VFS_RESULT_OK
            && (
                (
                    result->request_type == MICROS_VFS_MESSAGE_OPEN
                    && (
                        result->transferred_count != 0
                        || result->position != 0
                    )
                )
                || (
                    (
                        result->request_type == MICROS_VFS_MESSAGE_READ
                        || result->request_type
                            == MICROS_VFS_MESSAGE_WRITE
                        || result->request_type
                            == MICROS_VFS_MESSAGE_GETDENTS
                    )
                    && (
                        result->descriptor != 0
                        || result->mode != 0
                        || result->transferred_count
                            > MICROS_VFS_TRANSFER_MAX
                    )
                )
                || (
                    result->request_type != MICROS_VFS_MESSAGE_OPEN
                    && result->request_type != MICROS_VFS_MESSAGE_READ
                    && result->request_type != MICROS_VFS_MESSAGE_WRITE
                    && result->request_type
                        != MICROS_VFS_MESSAGE_GETDENTS
                    && !result_fields_are_zero(result)
                )
                || !application_type_is_known(result->request_type)
            )
        )
    ) {
        return MICROS_VFS_SERVICE_ERROR_INVARIANT;
    }
    clear_bytes(&built, sizeof(built));
    built.type = MICROS_VFS_MESSAGE_RESULT;
    write_u32_le(
        &built.payload[0],
        MICROS_VFS_PROTOCOL_VERSION
    );
    write_u32_le(&built.payload[4], result->request_type);
    write_u32_le(
        &built.payload[8],
        (uint32_t)(int32_t)result->result
    );
    write_u32_le(&built.payload[16], result->descriptor);
    write_u32_le(&built.payload[20], result->mode);
    write_u64_le(
        &built.payload[24],
        result->transferred_count
    );
    write_u64_le(&built.payload[32], result->position);
    *message = built;
    return MICROS_VFS_SERVICE_OK;
}

enum micros_vfs_service_error micros_vfs_service_build_ramfs_call(
    const struct micros_vfs_ramfs_request *request,
    struct micros_ipc_message *message
)
{
    struct micros_ipc_message built;
    uint32_t type;

    if (request == NULL || message == NULL) {
        return MICROS_VFS_SERVICE_ERROR_ARGUMENT;
    }
    if (!ramfs_request_is_exact(request)) {
        return MICROS_VFS_SERVICE_ERROR_INVARIANT;
    }
    type = ramfs_message_type(request->operation);
    if (type == 0) {
        return MICROS_VFS_SERVICE_ERROR_INVARIANT;
    }
    clear_bytes(&built, sizeof(built));
    built.type = type;
    write_u32_le(
        &built.payload[0],
        MICROS_RAMFS_PROTOCOL_VERSION
    );
    switch (request->operation) {
    case MICROS_VFS_RAMFS_MOUNT:
        break;
    case MICROS_VFS_RAMFS_LOOKUP:
        write_u64_le(&built.payload[8], request->start);
        write_u64_le(&built.payload[16], request->root);
        write_u32_le(&built.payload[24], request->grant);
        write_u32_le(&built.payload[28], request->length);
        write_u64_le(
            &built.payload[32],
            request->grant_offset
        );
        break;
    case MICROS_VFS_RAMFS_CREATE:
    case MICROS_VFS_RAMFS_MKDIR:
        write_u64_le(&built.payload[8], request->node);
        write_u32_le(&built.payload[16], request->mode);
        write_u32_le(&built.payload[20], request->grant);
        write_u64_le(
            &built.payload[24],
            request->grant_offset
        );
        write_u32_le(&built.payload[32], request->length);
        break;
    case MICROS_VFS_RAMFS_READ:
    case MICROS_VFS_RAMFS_WRITE:
        write_u64_le(&built.payload[8], request->node);
        write_u64_le(
            &built.payload[16],
            request->file_offset
        );
        write_u32_le(&built.payload[24], request->grant);
        write_u32_le(&built.payload[28], request->count);
        write_u64_le(
            &built.payload[32],
            request->grant_offset
        );
        break;
    case MICROS_VFS_RAMFS_GETDENTS:
        write_u64_le(&built.payload[8], request->node);
        write_u64_le(&built.payload[16], request->cursor);
        write_u32_le(&built.payload[24], request->grant);
        write_u32_le(&built.payload[28], request->count);
        write_u64_le(
            &built.payload[32],
            request->grant_offset
        );
        break;
    case MICROS_VFS_RAMFS_PUTNODE:
        write_u64_le(&built.payload[8], request->node);
        write_u32_le(&built.payload[16], request->count);
        break;
    }
    *message = built;
    return MICROS_VFS_SERVICE_OK;
}

enum micros_vfs_service_error micros_vfs_service_decode_ramfs_result(
    micros_endpoint_t ramfs_endpoint,
    const struct micros_vfs_ramfs_request *request,
    const struct micros_ipc_message *message,
    struct micros_vfs_ramfs_response *response
)
{
    struct micros_vfs_ramfs_response decoded;
    enum micros_ramfs_result result;

    if (
        request == NULL
        || message == NULL
        || response == NULL
        || !endpoint_is_canonical(ramfs_endpoint)
    ) {
        return MICROS_VFS_SERVICE_ERROR_ARGUMENT;
    }
    result = (enum micros_ramfs_result)(int32_t)read_u32_le(
        &message->payload[8]
    );
    if (
        message->source != ramfs_endpoint
        || message->type != MICROS_RAMFS_MESSAGE_RESULT
        || message->reply_token != 0
        || read_u32_le(&message->payload[0])
            != MICROS_RAMFS_PROTOCOL_VERSION
        || read_u32_le(&message->payload[4])
            != ramfs_message_type(request->operation)
        || !ramfs_result_is_defined(result)
        || read_u32_le(&message->payload[12]) != 0
    ) {
        return MICROS_VFS_SERVICE_ERROR_INVARIANT;
    }
    clear_bytes(&decoded, sizeof(decoded));
    decoded.result = result;
    decoded.node = read_u64_le(&message->payload[16]);
    decoded.file_size = read_u64_le(&message->payload[24]);
    decoded.position = read_u64_le(&message->payload[32]);
    decoded.count = read_u32_le(&message->payload[40]);
    decoded.mode = read_u32_le(&message->payload[44]);
    if (
        result != MICROS_RAMFS_RESULT_OK
        && (
            decoded.node != 0
            || decoded.file_size != 0
            || decoded.position != 0
            || decoded.count != 0
            || decoded.mode != 0
        )
    ) {
        return MICROS_VFS_SERVICE_ERROR_INVARIANT;
    }
    if (
        result == MICROS_RAMFS_RESULT_OK
        && !ramfs_response_fields_are_exact(
            request,
            &decoded
        )
    ) {
        return MICROS_VFS_SERVICE_ERROR_INVARIANT;
    }
    *response = decoded;
    return MICROS_VFS_SERVICE_OK;
}

enum micros_vfs_service_error micros_vfs_service_build_tty_call(
    const struct micros_vfs_tty_request *request,
    struct micros_ipc_message *message
)
{
    struct micros_ipc_message built;
    uint32_t type;

    if (request == NULL || message == NULL) {
        return MICROS_VFS_SERVICE_ERROR_ARGUMENT;
    }
    if (!tty_request_is_exact(request)) {
        return MICROS_VFS_SERVICE_ERROR_INVARIANT;
    }
    type = tty_message_type(request->operation);
    if (type == 0) {
        return MICROS_VFS_SERVICE_ERROR_INVARIANT;
    }
    clear_bytes(&built, sizeof(built));
    built.type = type;
    write_u32_le(&built.payload[0], MICROS_TTY_PROTOCOL_VERSION);
    write_u64_le(&built.payload[8], request->request_id);
    if (
        request->operation == MICROS_VFS_TTY_SUBMIT_READ
        || request->operation == MICROS_VFS_TTY_SUBMIT_WRITE
    ) {
        write_u32_le(&built.payload[16], request->grant);
        write_u64_le(&built.payload[32], request->count);
    } else if (request->grant != 0 || request->count != 0) {
        return MICROS_VFS_SERVICE_ERROR_INVARIANT;
    }
    *message = built;
    return MICROS_VFS_SERVICE_OK;
}

enum micros_vfs_service_error micros_vfs_service_decode_tty_result(
    micros_endpoint_t tty_endpoint,
    const struct micros_vfs_tty_request *request,
    const struct micros_ipc_message *message,
    struct micros_vfs_tty_response *response
)
{
    struct micros_vfs_tty_response decoded;
    enum micros_tty_result result;

    if (
        request == NULL
        || message == NULL
        || response == NULL
        || !endpoint_is_canonical(tty_endpoint)
    ) {
        return MICROS_VFS_SERVICE_ERROR_ARGUMENT;
    }
    result = (enum micros_tty_result)(int32_t)read_u32_le(
        &message->payload[8]
    );
    if (
        message->source != tty_endpoint
        || message->type != MICROS_TTY_MESSAGE_RESULT
        || message->reply_token != 0
        || read_u32_le(&message->payload[0])
            != MICROS_TTY_PROTOCOL_VERSION
        || read_u32_le(&message->payload[4])
            != tty_message_type(request->operation)
        || !tty_result_is_defined(result)
        || read_u32_le(&message->payload[12]) != 0
        || read_u64_le(&message->payload[16])
            != request->request_id
        || !bytes_are_zero(&message->payload[32], 16)
    ) {
        return MICROS_VFS_SERVICE_ERROR_INVARIANT;
    }
    clear_bytes(&decoded, sizeof(decoded));
    decoded.result = result;
    decoded.request_id = read_u64_le(&message->payload[16]);
    decoded.transferred_count =
        read_u64_le(&message->payload[24]);
    if (
        result != MICROS_TTY_RESULT_OK
        && decoded.transferred_count != 0
    ) {
        return MICROS_VFS_SERVICE_ERROR_INVARIANT;
    }
    if (
        result == MICROS_TTY_RESULT_OK
        && request->operation != MICROS_VFS_TTY_COLLECT
        && decoded.transferred_count != 0
    ) {
        return MICROS_VFS_SERVICE_ERROR_INVARIANT;
    }
    *response = decoded;
    return MICROS_VFS_SERVICE_OK;
}

static enum micros_vfs_result protocol_result(
    enum micros_vfs_protocol_status status
)
{
    switch (status) {
    case MICROS_VFS_PROTOCOL_BAD_TYPE:
        return MICROS_VFS_RESULT_BAD_TYPE;
    case MICROS_VFS_PROTOCOL_BAD_VERSION:
        return MICROS_VFS_RESULT_BAD_VERSION;
    case MICROS_VFS_PROTOCOL_MALFORMED:
        return MICROS_VFS_RESULT_MALFORMED;
    case MICROS_VFS_PROTOCOL_INVARIANT:
    case MICROS_VFS_PROTOCOL_OK:
        return MICROS_VFS_RESULT_STATE;
    }
    return MICROS_VFS_RESULT_STATE;
}

static enum micros_vfs_service_error publish_result(
    const struct micros_vfs_result_action *result,
    struct micros_vfs_service_reply_action *action
)
{
    struct micros_vfs_service_reply_action candidate;

    clear_bytes(&candidate, sizeof(candidate));
    if (
        micros_vfs_service_build_result(
            result,
            &candidate.message
        ) != MICROS_VFS_SERVICE_OK
    ) {
        return MICROS_VFS_SERVICE_ERROR_INVARIANT;
    }
    candidate.active = true;
    candidate.reply_token = result->reply_token;
    *action = candidate;
    return MICROS_VFS_SERVICE_OK;
}

enum micros_vfs_service_error micros_vfs_service_handle_message(
    struct micros_vfs_state *state,
    const struct micros_ipc_message *message,
    const struct micros_vfs_io *io,
    struct micros_vfs_service_reply_action *action
)
{
    struct micros_vfs_result_action result;

    if (
        state == NULL
        || message == NULL
        || io == NULL
        || action == NULL
    ) {
        return MICROS_VFS_SERVICE_ERROR_ARGUMENT;
    }
    clear_bytes(action, sizeof(*action));
    if (message->type == MICROS_IPC_TYPE_KERNEL_NOTIFICATION) {
        uint64_t events = read_u64_le(&message->payload[0]);

        if (
            message->source != state->tty_endpoint
            || message->reply_token != 0
            || events == 0
            || (
                events
                & ~(
                    MICROS_TTY_EVENT_COMPLETION
                    | MICROS_TTY_EVENT_WRITABLE
                )
            ) != 0
            || !bytes_are_zero(&message->payload[8], 40)
            || micros_vfs_handle_tty_notification(
                state,
                events,
                io,
                &result
            ) != MICROS_VFS_CORE_OK
        ) {
            return MICROS_VFS_SERVICE_ERROR_INVARIANT;
        }
    } else {
        struct micros_vfs_request request;
        enum micros_vfs_protocol_status status;

        if (message->reply_token == 0) {
            return MICROS_VFS_SERVICE_ERROR_INVARIANT;
        }
        status = micros_vfs_service_decode_request(
            message,
            &request
        );
        if (status == MICROS_VFS_PROTOCOL_INVARIANT) {
            return MICROS_VFS_SERVICE_ERROR_INVARIANT;
        }
        if (status != MICROS_VFS_PROTOCOL_OK) {
            clear_bytes(&result, sizeof(result));
            result.active = true;
            result.reply_token = message->reply_token;
            result.request_type = message->type;
            result.result = protocol_result(status);
        } else if (
            micros_vfs_handle_request(
                state,
                &request,
                io,
                &result
            ) != MICROS_VFS_CORE_OK
        ) {
            return MICROS_VFS_SERVICE_ERROR_INVARIANT;
        }
    }
    if (!result.active) {
        return MICROS_VFS_SERVICE_OK;
    }
    return publish_result(&result, action);
}

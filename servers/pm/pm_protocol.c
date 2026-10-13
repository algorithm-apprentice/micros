#include "servers/pm/pm_internal.h"

#include "micros/spawn.h"

static uint32_t read_u32_le(const uint8_t *bytes)
{
    return (
        (uint32_t)bytes[0]
        | (uint32_t)bytes[1] << 8
        | (uint32_t)bytes[2] << 16
        | (uint32_t)bytes[3] << 24
    );
}

static void write_u32_le(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static bool result_is_defined(enum micros_pm_result result)
{
    return (
        result >= MICROS_PM_RESULT_DESCRIPTOR
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
        && message->type != MICROS_PM_MESSAGE_SPAWN
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
            || !pm_common_bytes_are_zero(&message->payload[12], 36)
        ) {
            return MICROS_PM_PROTOCOL_MALFORMED;
        }
    } else if (message->type == MICROS_PM_MESSAGE_WAIT) {
        if (
            (flags & ~MICROS_PM_WAIT_NOHANG) != 0
            || !pm_common_bytes_are_zero(&message->payload[16], 32)
        ) {
            return MICROS_PM_PROTOCOL_MALFORMED;
        }
    } else {
        uint32_t request_size = read_u32_le(&message->payload[12]);

        if (
            flags != 0
            || request_size < MICROS_SPAWN_REQUEST_SIZE_MIN
            || request_size > MICROS_SPAWN_REQUEST_SIZE_MAX
            || !pm_common_bytes_are_zero(&message->payload[16], 32)
        ) {
            return MICROS_PM_PROTOCOL_MALFORMED;
        }
    }
    if (message->reply_token == 0) {
        return MICROS_PM_PROTOCOL_INVARIANT;
    }

    pm_common_zero_bytes(&decoded, sizeof(decoded));
    decoded.type = message->type;
    decoded.flags = flags;
    decoded.source = message->source;
    decoded.reply_token = message->reply_token;
    if (message->type == MICROS_PM_MESSAGE_EXIT) {
        decoded.exit_code = (int32_t)read_u32_le(
            &message->payload[8]
        );
    } else if (message->type == MICROS_PM_MESSAGE_WAIT) {
        decoded.child_pid = pm_common_read_u64_le(&message->payload[8]);
    } else {
        decoded.grant = read_u32_le(&message->payload[8]);
        decoded.request_size = read_u32_le(&message->payload[12]);
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
        if (request_type == MICROS_PM_MESSAGE_WAIT) {
            if (
                (
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
            request_type != MICROS_PM_MESSAGE_SPAWN
            || child_pid == 0
            || exit_kind != MICROS_PM_EXIT_NONE
            || exit_code != 0
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

    pm_common_zero_bytes(&built, sizeof(built));
    built.type = MICROS_PM_MESSAGE_RESULT;
    write_u32_le(&built.payload[0], MICROS_PM_PROTOCOL_VERSION);
    write_u32_le(&built.payload[4], request_type);
    write_u32_le(&built.payload[8], (uint32_t)(int32_t)result);
    pm_common_write_u64_le(&built.payload[16], child_pid);
    write_u32_le(&built.payload[24], (uint32_t)exit_kind);
    write_u32_le(&built.payload[28], (uint32_t)exit_code);
    *message = built;
    return MICROS_PM_MODEL_OK;
}

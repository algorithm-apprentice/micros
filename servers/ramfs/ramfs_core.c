#include "servers/ramfs/ramfs_internal.h"

static enum micros_ramfs_result protocol_result(
    enum micros_ramfs_protocol_status status
)
{
    switch (status) {
    case MICROS_RAMFS_PROTOCOL_BAD_TYPE:
        return MICROS_RAMFS_RESULT_BAD_TYPE;
    case MICROS_RAMFS_PROTOCOL_BAD_VERSION:
        return MICROS_RAMFS_RESULT_BAD_VERSION;
    case MICROS_RAMFS_PROTOCOL_MALFORMED:
        return MICROS_RAMFS_RESULT_MALFORMED;
    case MICROS_RAMFS_PROTOCOL_OK:
    case MICROS_RAMFS_PROTOCOL_INVARIANT:
    default:
        return MICROS_RAMFS_RESULT_STATE;
    }
}

enum micros_ramfs_core_error micros_ramfs_handle_call(
    struct micros_ramfs_state *state,
    const struct micros_ipc_message *message,
    const struct micros_ramfs_io *io,
    struct micros_ramfs_reply_action *action
)
{
    struct micros_ramfs_request request;
    struct ramfs_result_fields fields;
    enum micros_ramfs_protocol_status protocol_status;
    enum micros_ramfs_result result;
    enum micros_ramfs_core_error error;

    if (state == NULL || message == NULL || action == NULL) {
        return MICROS_RAMFS_CORE_ERROR_ARGUMENT;
    }
    if (
        micros_ramfs_state_validate(state) != MICROS_RAMFS_CORE_OK
    ) {
        return MICROS_RAMFS_CORE_ERROR_INVARIANT;
    }
    protocol_status = micros_ramfs_decode_request(message, &request);
    if (protocol_status == MICROS_RAMFS_PROTOCOL_INVARIANT) {
        return MICROS_RAMFS_CORE_ERROR_INVARIANT;
    }
    ramfs_zero_bytes(&fields, sizeof(fields));
    if (protocol_status != MICROS_RAMFS_PROTOCOL_OK) {
        return ramfs_prepare_reply(
            message,
            protocol_result(protocol_status),
            &fields,
            action
        );
    }
    if (request.source != state->vfs_endpoint) {
        return ramfs_prepare_reply(
            message,
            MICROS_RAMFS_RESULT_CALLER,
            &fields,
            action
        );
    }
    if (
        (
            request.type == MICROS_RAMFS_MESSAGE_MOUNT
            && state->phase != MICROS_RAMFS_PHASE_READY_UNMOUNTED
        )
        || (
            request.type != MICROS_RAMFS_MESSAGE_MOUNT
            && state->phase != MICROS_RAMFS_PHASE_MOUNTED
        )
    ) {
        return ramfs_prepare_reply(
            message,
            MICROS_RAMFS_RESULT_STATE,
            &fields,
            action
        );
    }

    result = MICROS_RAMFS_RESULT_OK;
    error = MICROS_RAMFS_CORE_OK;
    switch (request.type) {
    case MICROS_RAMFS_MESSAGE_MOUNT:
        result = ramfs_handle_mount(state, &fields);
        break;
    case MICROS_RAMFS_MESSAGE_LOOKUP:
        error = ramfs_handle_lookup(
            state,
            &request,
            io,
            &fields,
            &result
        );
        break;
    case MICROS_RAMFS_MESSAGE_CREATE:
    case MICROS_RAMFS_MESSAGE_MKDIR:
        error = ramfs_handle_create_or_mkdir(
            state,
            &request,
            io,
            &fields,
            &result
        );
        break;
    case MICROS_RAMFS_MESSAGE_READ:
        error = ramfs_handle_read(
            state,
            &request,
            io,
            &fields,
            &result
        );
        break;
    case MICROS_RAMFS_MESSAGE_WRITE:
        error = ramfs_handle_write(
            state,
            &request,
            io,
            &fields,
            &result
        );
        break;
    case MICROS_RAMFS_MESSAGE_GETDENTS:
        error = ramfs_handle_getdents(
            state,
            &request,
            io,
            &fields,
            &result
        );
        break;
    case MICROS_RAMFS_MESSAGE_PUTNODE:
        result = ramfs_handle_putnode(state, &request);
        break;
    default:
        return MICROS_RAMFS_CORE_ERROR_INVARIANT;
    }
    if (error != MICROS_RAMFS_CORE_OK) {
        return error;
    }
    if (
        micros_ramfs_state_validate(state) != MICROS_RAMFS_CORE_OK
    ) {
        return MICROS_RAMFS_CORE_ERROR_INVARIANT;
    }
    return ramfs_prepare_reply(message, result, &fields, action);
}

_Static_assert(
    sizeof(((struct micros_ramfs_state *)0)->data)
        == MICROS_RAMFS_FILE_SIZE_MAX,
    "RAMFS mutable data arena drift"
);
_Static_assert(
    sizeof(((struct micros_ramfs_state *)0)->scratch)
        == MICROS_RAMFS_TRANSFER_MAX,
    "RAMFS scratch page drift"
);

#include "servers/vfs/vfs_internal.h"

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

static void vfs_write_u32_le(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static bool map_write_result(
    const struct micros_vfs_ramfs_response *response,
    enum micros_vfs_result *result
)
{
    if (!vfs_ramfs_response_fields_are_zero(response)) {
        return false;
    }
    switch (response->result) {
    case MICROS_RAMFS_RESULT_NO_SPACE:
        *result = MICROS_VFS_RESULT_NO_SPACE;
        return true;
    case MICROS_RAMFS_RESULT_RANGE:
        *result = MICROS_VFS_RESULT_RANGE;
        return true;
    case MICROS_RAMFS_RESULT_BAD_TYPE:
    case MICROS_RAMFS_RESULT_BAD_VERSION:
    case MICROS_RAMFS_RESULT_MALFORMED:
    case MICROS_RAMFS_RESULT_CALLER:
    case MICROS_RAMFS_RESULT_STATE:
    case MICROS_RAMFS_RESULT_NODE:
    case MICROS_RAMFS_RESULT_NOT_FOUND:
    case MICROS_RAMFS_RESULT_EXISTS:
    case MICROS_RAMFS_RESULT_NOT_DIRECTORY:
    case MICROS_RAMFS_RESULT_IS_DIRECTORY:
    case MICROS_RAMFS_RESULT_GRANT:
    case MICROS_RAMFS_RESULT_REFERENCE:
    case MICROS_RAMFS_RESULT_OK:
        return false;
    }
    return false;
}

enum micros_vfs_core_error vfs_io_handle_close(
    struct micros_vfs_state *state,
    size_t process_slot,
    const struct micros_vfs_request *request,
    const struct micros_vfs_io *io,
    struct micros_vfs_result_action *action
)
{
    struct micros_vfs_open_file *open_file;
    enum micros_vfs_result result;
    size_t open_file_slot;

    if (
        request->grant != 0
        || request->grant_offset != 0
        || request->count != 0
        || request->path_length != 0
        || request->open_flags != 0
        || request->mode != 0
    ) {
        vfs_prepare_result(
            request,
            MICROS_VFS_RESULT_MALFORMED,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    result = vfs_resolve_descriptor(
        state,
        process_slot,
        request->descriptor,
        &open_file_slot,
        &open_file
    );
    if (result != MICROS_VFS_RESULT_OK) {
        vfs_prepare_result(request, result, action);
        return MICROS_VFS_CORE_OK;
    }
    (void)open_file;
    if (state->pending.state != MICROS_VFS_PENDING_NONE) {
        vfs_prepare_result(request, MICROS_VFS_RESULT_BUSY, action);
        return MICROS_VFS_CORE_OK;
    }
    state->processes[process_slot]
        .descriptors[request->descriptor] = 0;
    if (
        vfs_release_open_file_reference(
            state,
            open_file_slot,
            io
        ) != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    vfs_prepare_result(request, MICROS_VFS_RESULT_OK, action);
    return MICROS_VFS_CORE_OK;
}

enum micros_vfs_core_error vfs_io_handle_regular_read(
    struct micros_vfs_state *state,
    struct micros_vfs_open_file *open_file,
    const struct micros_vfs_request *request,
    const struct micros_vfs_io *io,
    struct micros_vfs_result_action *action
)
{
    struct micros_vfs_vnode *vnode =
        &state->vnodes[open_file->vnode - 1];
    struct micros_vfs_ramfs_request ramfs_request = {
        .operation = MICROS_VFS_RAMFS_READ,
        .node = vnode->node,
        .file_offset = open_file->position,
        .count = request->count,
    };
    struct micros_vfs_ramfs_response response = {0};
    enum micros_vfs_result result;
    enum micros_vfs_client_result copy_result;
    micros_grant_t grant = MICROS_GRANT_NONE;

    if (open_file->position >= vnode->size) {
        vfs_prepare_result(request, MICROS_VFS_RESULT_OK, action);
        action->position = open_file->position;
        return MICROS_VFS_CORE_OK;
    }
    if (
        vfs_validate_application_range(
            request,
            io,
            MICROS_GRANT_PERMISSION_WRITE,
            &result
        ) != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (result != MICROS_VFS_RESULT_OK) {
        vfs_prepare_result(request, result, action);
        return MICROS_VFS_CORE_OK;
    }
    state->backend_page_owner = MICROS_VFS_PAGE_SYNCHRONOUS;
    if (
        vfs_create_backend_grant(
            state,
            io,
            state->ramfs_endpoint,
            state->backend_page,
            request->count,
            MICROS_GRANT_PERMISSION_WRITE,
            &grant,
            &result
        ) != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (result != MICROS_VFS_RESULT_OK) {
        state->backend_page_owner = MICROS_VFS_PAGE_FREE;
        vfs_prepare_result(request, result, action);
        return MICROS_VFS_CORE_OK;
    }
    ramfs_request.grant = grant;
    if (
        io->ramfs_call == NULL
        || io->ramfs_call(
            io->context,
            &ramfs_request,
            &response
        ) != MICROS_VFS_BACKEND_OK
        || vfs_revoke_backend_grant(io, grant)
            != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (response.result != MICROS_RAMFS_RESULT_OK) {
        state->backend_page_owner = MICROS_VFS_PAGE_FREE;
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (
        response.node != 0
        || response.mode != 0
        || response.file_size != vnode->size
        || response.count
            != (
                request->count < vnode->size - open_file->position
                    ? request->count
                    : vnode->size - open_file->position
            )
        || response.position
            != open_file->position + response.count
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (response.count != 0) {
        if (io->client_copy == NULL) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        copy_result = io->client_copy(
            io->context,
            MICROS_VFS_CLIENT_TO_APPLICATION,
            request->source,
            request->grant,
            request->grant_offset,
            state->backend_page,
            response.count
        );
        if (copy_result == MICROS_VFS_CLIENT_REJECTED) {
            state->backend_page_owner = MICROS_VFS_PAGE_FREE;
            vfs_prepare_result(
                request,
                MICROS_VFS_RESULT_GRANT,
                action
            );
            return MICROS_VFS_CORE_OK;
        }
        if (copy_result != MICROS_VFS_CLIENT_OK) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
    }
    state->backend_page_owner = MICROS_VFS_PAGE_FREE;
    open_file->position = response.position;
    vfs_prepare_result(request, MICROS_VFS_RESULT_OK, action);
    action->transferred_count = response.count;
    action->position = response.position;
    return MICROS_VFS_CORE_OK;
}

enum micros_vfs_core_error vfs_io_handle_regular_write(
    struct micros_vfs_state *state,
    struct micros_vfs_open_file *open_file,
    const struct micros_vfs_request *request,
    const struct micros_vfs_io *io,
    struct micros_vfs_result_action *action
)
{
    struct micros_vfs_vnode *vnode =
        &state->vnodes[open_file->vnode - 1];
    struct micros_vfs_ramfs_request ramfs_request = {
        .operation = MICROS_VFS_RAMFS_WRITE,
        .node = vnode->node,
        .file_offset = open_file->position,
        .count = request->count,
    };
    struct micros_vfs_ramfs_response response = {0};
    enum micros_vfs_result result;
    enum micros_vfs_client_result copy_result;
    micros_grant_t grant = MICROS_GRANT_NONE;
    uint64_t expected_position;
    uint64_t expected_size;

    if (
        io == NULL
        || io->client_copy == NULL
        || state->backend_page_owner != MICROS_VFS_PAGE_FREE
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    state->backend_page_owner = MICROS_VFS_PAGE_SYNCHRONOUS;
    copy_result = io->client_copy(
        io->context,
        MICROS_VFS_CLIENT_FROM_APPLICATION,
        request->source,
        request->grant,
        request->grant_offset,
        state->backend_page,
        request->count
    );
    if (copy_result == MICROS_VFS_CLIENT_REJECTED) {
        state->backend_page_owner = MICROS_VFS_PAGE_FREE;
        vfs_prepare_result(
            request,
            MICROS_VFS_RESULT_GRANT,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    if (copy_result != MICROS_VFS_CLIENT_OK) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (
        vfs_create_backend_grant(
            state,
            io,
            state->ramfs_endpoint,
            state->backend_page,
            request->count,
            MICROS_GRANT_PERMISSION_READ,
            &grant,
            &result
        ) != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (result != MICROS_VFS_RESULT_OK) {
        state->backend_page_owner = MICROS_VFS_PAGE_FREE;
        vfs_prepare_result(request, result, action);
        return MICROS_VFS_CORE_OK;
    }
    ramfs_request.grant = grant;
    if (
        io->ramfs_call == NULL
        || io->ramfs_call(
            io->context,
            &ramfs_request,
            &response
        ) != MICROS_VFS_BACKEND_OK
        || vfs_revoke_backend_grant(io, grant)
            != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    state->backend_page_owner = MICROS_VFS_PAGE_FREE;
    if (response.result != MICROS_RAMFS_RESULT_OK) {
        if (!map_write_result(&response, &result)) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        vfs_prepare_result(request, result, action);
        return MICROS_VFS_CORE_OK;
    }
    if (
        UINT64_MAX - open_file->position < request->count
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    expected_position = open_file->position + request->count;
    expected_size = vnode->size < expected_position
        ? expected_position
        : vnode->size;
    if (
        response.node != 0
        || response.mode != 0
        || response.count != request->count
        || response.position != expected_position
        || response.file_size != expected_size
        || response.file_size > MICROS_RAMFS_FILE_SIZE_MAX
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    vnode->size = response.file_size;
    open_file->position = response.position;
    vfs_prepare_result(request, MICROS_VFS_RESULT_OK, action);
    action->transferred_count = response.count;
    action->position = response.position;
    return MICROS_VFS_CORE_OK;
}

static enum micros_vfs_core_error translate_directory_records(
    struct micros_vfs_state *state,
    size_t backend_count,
    size_t *application_count
)
{
    size_t backend_offset;
    size_t application_offset = 0;

    if (
        backend_count > MICROS_VFS_TRANSFER_MAX
        || (
            backend_count
                % MICROS_RAMFS_DIRECTORY_RECORD_SIZE
        ) != 0
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    state->client_page_owner = MICROS_VFS_PAGE_SYNCHRONOUS;
    for (
        backend_offset = 0;
        backend_offset < backend_count;
        backend_offset += MICROS_RAMFS_DIRECTORY_RECORD_SIZE
    ) {
        const uint8_t *source =
            &state->backend_page[backend_offset];
        uint8_t *destination =
            &state->client_page[application_offset];
        uint64_t node = vfs_read_u64_le(&source[0]);
        uint32_t mode = vfs_read_u32_le(&source[8]);
        uint32_t name_length = vfs_read_u32_le(&source[12]);
        size_t index;

        if (
            !vfs_ramfs_handle_is_canonical(node)
            || !vfs_mode_is_canonical(mode)
            || name_length == 0
            || name_length > MICROS_VFS_NAME_MAX
            || !vfs_bytes_are_zero(
                &source[16 + name_length],
                MICROS_VFS_NAME_MAX - name_length
            )
            || !vfs_bytes_are_zero(
                &source[76],
                MICROS_RAMFS_DIRECTORY_RECORD_SIZE - 76
            )
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        for (index = 0; index < name_length; ++index) {
            if (
                source[16 + index] == 0
                || source[16 + index] == '/'
            ) {
                return MICROS_VFS_CORE_ERROR_INVARIANT;
            }
        }
        vfs_zero_bytes(
            destination,
            MICROS_VFS_DIRECTORY_RECORD_SIZE
        );
        vfs_write_u32_le(
            &destination[0],
            MICROS_VFS_DIRECTORY_RECORD_SIZE
        );
        vfs_write_u32_le(&destination[4], mode);
        vfs_write_u32_le(&destination[8], name_length);
        for (index = 0; index < name_length; ++index) {
            destination[16 + index] = source[16 + index];
        }
        application_offset += MICROS_VFS_DIRECTORY_RECORD_SIZE;
    }
    *application_count = application_offset;
    return MICROS_VFS_CORE_OK;
}

enum micros_vfs_core_error vfs_io_handle_getdents(
    struct micros_vfs_state *state,
    struct micros_vfs_open_file *open_file,
    const struct micros_vfs_request *request,
    const struct micros_vfs_io *io,
    struct micros_vfs_result_action *action
)
{
    struct micros_vfs_vnode *vnode =
        &state->vnodes[open_file->vnode - 1];
    struct micros_vfs_ramfs_request ramfs_request = {
        .operation = MICROS_VFS_RAMFS_GETDENTS,
        .node = vnode->node,
        .cursor = open_file->position,
    };
    struct micros_vfs_ramfs_response response = {0};
    enum micros_vfs_result result;
    enum micros_vfs_client_result copy_result;
    micros_grant_t grant = MICROS_GRANT_NONE;
    size_t record_capacity =
        request->count / MICROS_VFS_DIRECTORY_RECORD_SIZE;
    size_t backend_count;
    size_t application_count;

    if (
        open_file->position
            == MICROS_RAMFS_DIRECTORY_CURSOR_END
    ) {
        vfs_prepare_result(request, MICROS_VFS_RESULT_OK, action);
        action->position = open_file->position;
        return MICROS_VFS_CORE_OK;
    }
    if (record_capacity > 32) {
        record_capacity = 32;
    }
    backend_count =
        record_capacity * MICROS_RAMFS_DIRECTORY_RECORD_SIZE;
    if (
        vfs_validate_application_range(
            request,
            io,
            MICROS_GRANT_PERMISSION_WRITE,
            &result
        ) != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (result != MICROS_VFS_RESULT_OK) {
        vfs_prepare_result(request, result, action);
        return MICROS_VFS_CORE_OK;
    }
    state->backend_page_owner = MICROS_VFS_PAGE_SYNCHRONOUS;
    if (
        vfs_create_backend_grant(
            state,
            io,
            state->ramfs_endpoint,
            state->backend_page,
            backend_count,
            MICROS_GRANT_PERMISSION_WRITE,
            &grant,
            &result
        ) != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (result != MICROS_VFS_RESULT_OK) {
        state->backend_page_owner = MICROS_VFS_PAGE_FREE;
        vfs_prepare_result(request, result, action);
        return MICROS_VFS_CORE_OK;
    }
    ramfs_request.grant = grant;
    ramfs_request.count = (uint32_t)backend_count;
    if (
        io->ramfs_call == NULL
        || io->ramfs_call(
            io->context,
            &ramfs_request,
            &response
        ) != MICROS_VFS_BACKEND_OK
        || vfs_revoke_backend_grant(io, grant)
            != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (response.result != MICROS_RAMFS_RESULT_OK) {
        state->backend_page_owner = MICROS_VFS_PAGE_FREE;
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (
        response.node != 0
        || response.file_size != 0
        || response.mode != 0
        || response.count > backend_count
        || (
            response.count
                % MICROS_RAMFS_DIRECTORY_RECORD_SIZE
        ) != 0
        || response.position <= open_file->position
        || response.position > MICROS_RAMFS_DIRECTORY_CURSOR_END
        || (
            response.count == 0
            && response.position
                != MICROS_RAMFS_DIRECTORY_CURSOR_END
        )
        || translate_directory_records(
            state,
            response.count,
            &application_count
        ) != MICROS_VFS_CORE_OK
        || application_count > request->count
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (application_count != 0) {
        if (io->client_copy == NULL) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        copy_result = io->client_copy(
            io->context,
            MICROS_VFS_CLIENT_TO_APPLICATION,
            request->source,
            request->grant,
            request->grant_offset,
            state->client_page,
            application_count
        );
        if (copy_result == MICROS_VFS_CLIENT_REJECTED) {
            state->client_page_owner = MICROS_VFS_PAGE_FREE;
            state->backend_page_owner = MICROS_VFS_PAGE_FREE;
            vfs_prepare_result(
                request,
                MICROS_VFS_RESULT_GRANT,
                action
            );
            return MICROS_VFS_CORE_OK;
        }
        if (copy_result != MICROS_VFS_CLIENT_OK) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
    }
    state->client_page_owner = MICROS_VFS_PAGE_FREE;
    state->backend_page_owner = MICROS_VFS_PAGE_FREE;
    open_file->position = response.position;
    vfs_prepare_result(request, MICROS_VFS_RESULT_OK, action);
    action->transferred_count = application_count;
    action->position = response.position;
    return MICROS_VFS_CORE_OK;
}

#include "servers/vfs/vfs_internal.h"

enum micros_vfs_core_error vfs_create_backend_grant(
    struct micros_vfs_state *state,
    const struct micros_vfs_io *io,
    micros_endpoint_t endpoint,
    uint8_t *local,
    size_t length,
    uint32_t permission,
    micros_grant_t *grant,
    enum micros_vfs_result *result
)
{
    enum micros_vfs_backend_grant_result grant_result;

    if (
        io == NULL
        || io->backend_grant_create == NULL
        || grant == NULL
        || result == NULL
        || state->backend_page_owner
            != MICROS_VFS_PAGE_SYNCHRONOUS
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    grant_result = io->backend_grant_create(
        io->context,
        endpoint,
        local,
        length,
        permission,
        grant
    );
    if (grant_result == MICROS_VFS_BACKEND_GRANT_CAPACITY) {
        *result = MICROS_VFS_RESULT_NO_SPACE;
        return MICROS_VFS_CORE_OK;
    }
    if (
        grant_result != MICROS_VFS_BACKEND_GRANT_OK
        || *grant == MICROS_GRANT_NONE
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    *result = MICROS_VFS_RESULT_OK;
    return MICROS_VFS_CORE_OK;
}

enum micros_vfs_core_error vfs_revoke_backend_grant(
    const struct micros_vfs_io *io,
    micros_grant_t grant
)
{
    if (
        io == NULL
        || io->backend_grant_revoke == NULL
        || grant == MICROS_GRANT_NONE
        || io->backend_grant_revoke(
            io->context,
            grant
        ) != MICROS_VFS_BACKEND_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    return MICROS_VFS_CORE_OK;
}

enum micros_vfs_core_error vfs_copy_application_path(
    struct micros_vfs_state *state,
    const struct micros_vfs_request *request,
    const struct micros_vfs_io *io,
    enum micros_vfs_result *result
)
{
    enum micros_vfs_client_result copy_result;
    size_t index;

    if (
        io == NULL
        || io->client_copy == NULL
        || state->client_page_owner != MICROS_VFS_PAGE_FREE
        || request->path_length < 2
        || request->path_length > MICROS_VFS_PATH_MAX
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    state->client_page_owner = MICROS_VFS_PAGE_SYNCHRONOUS;
    copy_result = io->client_copy(
        io->context,
        MICROS_VFS_CLIENT_FROM_APPLICATION,
        request->source,
        request->grant,
        request->grant_offset,
        state->client_page,
        request->path_length
    );
    if (copy_result == MICROS_VFS_CLIENT_REJECTED) {
        state->client_page_owner = MICROS_VFS_PAGE_FREE;
        *result = MICROS_VFS_RESULT_GRANT;
        return MICROS_VFS_CORE_OK;
    }
    if (copy_result != MICROS_VFS_CLIENT_OK) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (state->client_page[request->path_length - 1] != 0) {
        state->client_page_owner = MICROS_VFS_PAGE_FREE;
        *result = MICROS_VFS_RESULT_MALFORMED;
        return MICROS_VFS_CORE_OK;
    }
    for (index = 0; index + 1 < request->path_length; ++index) {
        if (state->client_page[index] == 0) {
            state->client_page_owner = MICROS_VFS_PAGE_FREE;
            *result = MICROS_VFS_RESULT_MALFORMED;
            return MICROS_VFS_CORE_OK;
        }
    }
    *result = MICROS_VFS_RESULT_OK;
    return MICROS_VFS_CORE_OK;
}

enum micros_vfs_core_error vfs_validate_application_range(
    const struct micros_vfs_request *request,
    const struct micros_vfs_io *io,
    uint32_t permission,
    enum micros_vfs_result *result
)
{
    enum micros_vfs_client_result validate_result;

    if (io == NULL || io->client_validate == NULL) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    validate_result = io->client_validate(
        io->context,
        request->source,
        request->grant,
        request->grant_offset,
        request->count,
        permission
    );
    if (validate_result == MICROS_VFS_CLIENT_REJECTED) {
        *result = MICROS_VFS_RESULT_GRANT;
        return MICROS_VFS_CORE_OK;
    }
    if (validate_result != MICROS_VFS_CLIENT_OK) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    *result = MICROS_VFS_RESULT_OK;
    return MICROS_VFS_CORE_OK;
}

#include "servers/vfs/vfs_internal.h"

bool vfs_namespace_response_node_is_valid(
    const struct micros_vfs_ramfs_response *response
)
{
    return (
        vfs_ramfs_handle_is_canonical(response->node)
        && vfs_mode_is_canonical(response->mode)
        && response->file_size <= MICROS_RAMFS_FILE_SIZE_MAX
        && (
            !vfs_mode_is_directory(response->mode)
            || response->file_size == 0
        )
    );
}

static bool map_lookup_result(
    const struct micros_vfs_ramfs_response *response,
    enum micros_vfs_result *result
)
{
    if (!vfs_ramfs_response_fields_are_zero(response)) {
        return false;
    }
    switch (response->result) {
    case MICROS_RAMFS_RESULT_MALFORMED:
        *result = MICROS_VFS_RESULT_MALFORMED;
        return true;
    case MICROS_RAMFS_RESULT_NOT_FOUND:
        *result = MICROS_VFS_RESULT_NOT_FOUND;
        return true;
    case MICROS_RAMFS_RESULT_NOT_DIRECTORY:
        *result = MICROS_VFS_RESULT_NOT_DIRECTORY;
        return true;
    case MICROS_RAMFS_RESULT_NO_SPACE:
        *result = MICROS_VFS_RESULT_NO_SPACE;
        return true;
    case MICROS_RAMFS_RESULT_RANGE:
        *result = MICROS_VFS_RESULT_RANGE;
        return true;
    case MICROS_RAMFS_RESULT_BAD_TYPE:
    case MICROS_RAMFS_RESULT_BAD_VERSION:
    case MICROS_RAMFS_RESULT_CALLER:
    case MICROS_RAMFS_RESULT_STATE:
    case MICROS_RAMFS_RESULT_NODE:
    case MICROS_RAMFS_RESULT_GRANT:
    case MICROS_RAMFS_RESULT_REFERENCE:
    case MICROS_RAMFS_RESULT_EXISTS:
    case MICROS_RAMFS_RESULT_IS_DIRECTORY:
    case MICROS_RAMFS_RESULT_OK:
        return false;
    }
    return false;
}

static bool map_create_result(
    const struct micros_vfs_ramfs_response *response,
    enum micros_vfs_result *result
)
{
    if (!vfs_ramfs_response_fields_are_zero(response)) {
        return false;
    }
    switch (response->result) {
    case MICROS_RAMFS_RESULT_EXISTS:
        *result = MICROS_VFS_RESULT_EXISTS;
        return true;
    case MICROS_RAMFS_RESULT_NO_SPACE:
        *result = MICROS_VFS_RESULT_NO_SPACE;
        return true;
    case MICROS_RAMFS_RESULT_BAD_TYPE:
    case MICROS_RAMFS_RESULT_BAD_VERSION:
    case MICROS_RAMFS_RESULT_MALFORMED:
    case MICROS_RAMFS_RESULT_CALLER:
    case MICROS_RAMFS_RESULT_STATE:
    case MICROS_RAMFS_RESULT_NODE:
    case MICROS_RAMFS_RESULT_NOT_FOUND:
    case MICROS_RAMFS_RESULT_NOT_DIRECTORY:
    case MICROS_RAMFS_RESULT_IS_DIRECTORY:
    case MICROS_RAMFS_RESULT_GRANT:
    case MICROS_RAMFS_RESULT_RANGE:
    case MICROS_RAMFS_RESULT_REFERENCE:
    case MICROS_RAMFS_RESULT_OK:
        return false;
    }
    return false;
}

static enum micros_vfs_core_error lookup_path(
    struct micros_vfs_state *state,
    const struct micros_vfs_request *request,
    const struct micros_vfs_process_record *process,
    const struct micros_vfs_io *io,
    struct micros_vfs_ramfs_response *response,
    enum micros_vfs_result *result
)
{
    const struct micros_vfs_vnode *start;
    const struct micros_vfs_vnode *root;
    struct micros_vfs_ramfs_request ramfs_request = {
        .operation = MICROS_VFS_RAMFS_LOOKUP,
        .length = request->path_length,
    };
    micros_grant_t grant = MICROS_GRANT_NONE;
    enum micros_vfs_core_error error;

    if (
        state->client_page_owner
            != MICROS_VFS_PAGE_SYNCHRONOUS
        || state->backend_page_owner != MICROS_VFS_PAGE_FREE
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    root = &state->vnodes[process->root_vnode - 1];
    start = request->path_length > 1
            && state->client_page[0] == '/'
        ? root
        : &state->vnodes[
            process->working_directory_vnode - 1
        ];
    state->backend_page_owner = MICROS_VFS_PAGE_SYNCHRONOUS;
    error = vfs_create_backend_grant(
        state,
        io,
        state->ramfs_endpoint,
        state->client_page,
        request->path_length,
        MICROS_GRANT_PERMISSION_READ,
        &grant,
        result
    );
    if (
        error != MICROS_VFS_CORE_OK
        || *result != MICROS_VFS_RESULT_OK
    ) {
        state->backend_page_owner = MICROS_VFS_PAGE_FREE;
        return error;
    }
    ramfs_request.start = start->node;
    ramfs_request.root = root->node;
    ramfs_request.grant = grant;
    if (
        io->ramfs_call == NULL
        || io->ramfs_call(
            io->context,
            &ramfs_request,
            response
        ) != MICROS_VFS_BACKEND_OK
        || vfs_revoke_backend_grant(io, grant)
            != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    state->backend_page_owner = MICROS_VFS_PAGE_FREE;
    if (response->result == MICROS_RAMFS_RESULT_OK) {
        if (
            !vfs_namespace_response_node_is_valid(response)
            || response->position != 0
            || response->count != 0
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        *result = MICROS_VFS_RESULT_OK;
        return MICROS_VFS_CORE_OK;
    }
    if (!map_lookup_result(response, result)) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    return MICROS_VFS_CORE_OK;
}

enum micros_vfs_core_error vfs_namespace_integrate_vnode(
    struct micros_vfs_state *state,
    const struct micros_vfs_ramfs_response *response,
    size_t reserved_vnode_slot,
    const struct micros_vfs_io *io,
    size_t *vnode_slot
)
{
    struct micros_vfs_vnode *vnode;

    if (vfs_find_vnode_by_node(state, response->node, vnode_slot)) {
        vnode = &state->vnodes[*vnode_slot];
        if (
            vnode->mode != response->mode
            || vnode->size != response->file_size
            || vnode->local_reference_count == UINT32_MAX
            || vnode->backend_reference_count == UINT32_MAX
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        ++vnode->local_reference_count;
        ++vnode->backend_reference_count;
        if (
            vnode->backend_reference_count
                == MICROS_VFS_BACKEND_REFERENCE_THRESHOLD
        ) {
            if (
                vfs_putnode(
                    io,
                    vnode->node,
                    MICROS_VFS_BACKEND_REFERENCE_THRESHOLD - 1
                ) != MICROS_VFS_CORE_OK
            ) {
                return MICROS_VFS_CORE_ERROR_INVARIANT;
            }
            vnode->backend_reference_count = 1;
        }
        return MICROS_VFS_CORE_OK;
    }
    if (
        reserved_vnode_slot >= MICROS_VFS_VNODE_CAPACITY
        || state->vnodes[reserved_vnode_slot].state
            != MICROS_VFS_VNODE_FREE
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    *vnode_slot = reserved_vnode_slot;
    state->vnodes[reserved_vnode_slot] =
        (struct micros_vfs_vnode){
            .state = MICROS_VFS_VNODE_ACTIVE,
            .node = response->node,
            .mode = response->mode,
            .local_reference_count = 1,
            .backend_reference_count = 1,
            .size = response->file_size,
        };
    return MICROS_VFS_CORE_OK;
}

static bool extract_final_component(
    struct micros_vfs_state *state,
    size_t path_length,
    bool strip_trailing_separators,
    uint8_t *name,
    size_t *name_length,
    size_t *parent_length
)
{
    size_t end = path_length - 1;
    size_t start;

    if (strip_trailing_separators) {
        while (end != 0 && state->client_page[end - 1] == '/') {
            --end;
        }
    } else if (
        end != 0
        && state->client_page[end - 1] == '/'
    ) {
        return false;
    }
    if (end == 0) {
        return false;
    }
    start = end;
    while (
        start != 0
        && state->client_page[start - 1] != '/'
    ) {
        --start;
    }
    *name_length = end - start;
    if (
        *name_length == 0
        || *name_length > MICROS_VFS_NAME_MAX
    ) {
        return false;
    }
    while (end != start) {
        --end;
        name[end - start] = state->client_page[end];
    }
    name[*name_length] = 0;
    if (start == 0) {
        state->client_page[0] = '.';
        state->client_page[1] = 0;
        *parent_length = 2;
    } else {
        state->client_page[start] = 0;
        *parent_length = start + 1;
    }
    return true;
}

static bool create_name_is_valid(
    const uint8_t *name,
    size_t name_length
)
{
    if (
        (
            name_length == 1
            && name[0] == '.'
        )
        || (
            name_length == 2
            && name[0] == '.'
            && name[1] == '.'
        )
    ) {
        return false;
    }
    return true;
}

static enum micros_vfs_core_error call_create(
    struct micros_vfs_state *state,
    enum micros_vfs_ramfs_operation operation,
    uint64_t parent,
    const uint8_t *name,
    size_t name_length,
    uint32_t mode,
    const struct micros_vfs_io *io,
    struct micros_vfs_ramfs_response *response,
    enum micros_vfs_result *result
)
{
    struct micros_vfs_ramfs_request request = {
        .operation = operation,
        .node = parent,
        .mode = mode,
        .length = (uint32_t)(name_length + 1),
    };
    micros_grant_t grant = MICROS_GRANT_NONE;
    enum micros_vfs_core_error error;

    if (
        state->client_page_owner
            != MICROS_VFS_PAGE_SYNCHRONOUS
        || state->backend_page_owner != MICROS_VFS_PAGE_FREE
        || name_length == 0
        || name_length > MICROS_VFS_NAME_MAX
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    vfs_zero_bytes(state->client_page, name_length + 1);
    while (name_length != 0) {
        --name_length;
        state->client_page[name_length] = name[name_length];
    }
    state->backend_page_owner = MICROS_VFS_PAGE_SYNCHRONOUS;
    error = vfs_create_backend_grant(
        state,
        io,
        state->ramfs_endpoint,
        state->client_page,
        request.length,
        MICROS_GRANT_PERMISSION_READ,
        &grant,
        result
    );
    if (
        error != MICROS_VFS_CORE_OK
        || *result != MICROS_VFS_RESULT_OK
    ) {
        state->backend_page_owner = MICROS_VFS_PAGE_FREE;
        return error;
    }
    request.grant = grant;
    if (
        io->ramfs_call == NULL
        || io->ramfs_call(
            io->context,
            &request,
            response
        ) != MICROS_VFS_BACKEND_OK
        || vfs_revoke_backend_grant(io, grant)
            != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    state->backend_page_owner = MICROS_VFS_PAGE_FREE;
    if (response->result == MICROS_RAMFS_RESULT_OK) {
        *result = MICROS_VFS_RESULT_OK;
        return MICROS_VFS_CORE_OK;
    }
    if (!map_create_result(response, result)) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    return MICROS_VFS_CORE_OK;
}

static enum micros_vfs_core_error create_regular_file(
    struct micros_vfs_state *state,
    struct micros_vfs_process_record *process,
    const struct micros_vfs_request *request,
    const struct micros_vfs_io *io,
    struct micros_vfs_ramfs_response *response,
    enum micros_vfs_result *result
)
{
    struct micros_vfs_request parent_request = *request;
    struct micros_vfs_ramfs_response parent_response = {0};
    uint8_t name[MICROS_VFS_NAME_MAX + 1] = {0};
    size_t name_length;
    size_t parent_length;
    enum micros_vfs_core_error error;

    if (
        !extract_final_component(
            state,
            request->path_length,
            false,
            name,
            &name_length,
            &parent_length
        )
    ) {
        *result = MICROS_VFS_RESULT_NOT_DIRECTORY;
        return MICROS_VFS_CORE_OK;
    }
    parent_request.path_length = (uint32_t)parent_length;
    error = lookup_path(
        state,
        &parent_request,
        process,
        io,
        &parent_response,
        result
    );
    if (
        error != MICROS_VFS_CORE_OK
        || *result != MICROS_VFS_RESULT_OK
    ) {
        return error;
    }
    if (!vfs_mode_is_directory(parent_response.mode)) {
        if (
            vfs_putnode(io, parent_response.node, 1)
                != MICROS_VFS_CORE_OK
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        *result = MICROS_VFS_RESULT_NOT_DIRECTORY;
        return MICROS_VFS_CORE_OK;
    }
    if (!create_name_is_valid(name, name_length)) {
        if (
            vfs_putnode(io, parent_response.node, 1)
                != MICROS_VFS_CORE_OK
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        *result = MICROS_VFS_RESULT_MALFORMED;
        return MICROS_VFS_CORE_OK;
    }
    error = call_create(
        state,
        MICROS_VFS_RAMFS_CREATE,
        parent_response.node,
        name,
        name_length,
        MICROS_RAMFS_MODE_REGULAR | request->mode,
        io,
        response,
        result
    );
    if (
        vfs_putnode(io, parent_response.node, 1)
            != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (
        error != MICROS_VFS_CORE_OK
        || *result != MICROS_VFS_RESULT_OK
    ) {
        return error;
    }
    if (
        !vfs_namespace_response_node_is_valid(response)
        || response->mode
            != (
                MICROS_RAMFS_MODE_REGULAR
                | request->mode
            )
        || response->file_size != 0
        || response->position != 0
        || response->count != 0
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    return MICROS_VFS_CORE_OK;
}

enum micros_vfs_core_error vfs_namespace_handle_open(
    struct micros_vfs_state *state,
    size_t process_slot,
    const struct micros_vfs_request *request,
    const struct micros_vfs_io *io,
    struct micros_vfs_result_action *action
)
{
    struct micros_vfs_process_record *process =
        &state->processes[process_slot];
    struct micros_vfs_ramfs_response response = {0};
    enum micros_vfs_result result;
    size_t descriptor;
    size_t open_file_slot;
    size_t vnode_scratch;
    size_t vnode_slot;
    uint32_t access =
        request->open_flags & MICROS_VFS_ACCESS_DEFINED_MASK;
    bool created = false;

    if (
        request->descriptor != 0
        || request->count != 0
        || request->path_length < 2
        || request->path_length > MICROS_VFS_PATH_MAX
        || access == 0
        || (
            request->open_flags
                & ~MICROS_VFS_OPEN_DEFINED_MASK
        ) != 0
        || (
            (request->open_flags & MICROS_VFS_OPEN_EXCLUSIVE)
            != 0
            && (
                request->open_flags & MICROS_VFS_OPEN_CREATE
            ) == 0
        )
        || (
            request->open_flags
                & (
                    MICROS_VFS_OPEN_CREATE
                    | MICROS_VFS_OPEN_DIRECTORY
                )
        ) == (
            MICROS_VFS_OPEN_CREATE
            | MICROS_VFS_OPEN_DIRECTORY
        )
        || (
            request->mode & ~MICROS_RAMFS_MODE_PERMISSIONS
        ) != 0
        || (
            (
                request->open_flags & MICROS_VFS_OPEN_CREATE
            ) == 0
            && request->mode != 0
        )
    ) {
        vfs_prepare_result(
            request,
            MICROS_VFS_RESULT_MALFORMED,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    if (state->pending.state != MICROS_VFS_PENDING_NONE) {
        vfs_prepare_result(request, MICROS_VFS_RESULT_BUSY, action);
        return MICROS_VFS_CORE_OK;
    }
    if (
        vfs_copy_application_path(
            state,
            request,
            io,
            &result
        ) != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (result != MICROS_VFS_RESULT_OK) {
        vfs_prepare_result(request, result, action);
        return MICROS_VFS_CORE_OK;
    }
    if (
        !vfs_find_free_descriptor(process, &descriptor)
        || !vfs_find_free_open_files(state, 1, &open_file_slot)
        || !vfs_find_free_vnode(state, &vnode_scratch)
    ) {
        state->client_page_owner = MICROS_VFS_PAGE_FREE;
        vfs_prepare_result(
            request,
            MICROS_VFS_RESULT_NO_SPACE,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    if (
        lookup_path(
            state,
            request,
            process,
            io,
            &response,
            &result
        ) != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (result != MICROS_VFS_RESULT_OK) {
        if (
            result == MICROS_VFS_RESULT_NOT_FOUND
            && (
                request->open_flags & MICROS_VFS_OPEN_CREATE
            ) != 0
        ) {
            if (
                request->path_length > 1
                && state->client_page[
                    request->path_length - 2
                ] == '/'
            ) {
                state->client_page_owner =
                    MICROS_VFS_PAGE_FREE;
                vfs_prepare_result(
                    request,
                    MICROS_VFS_RESULT_NOT_DIRECTORY,
                    action
                );
                return MICROS_VFS_CORE_OK;
            }
            if (vfs_free_vnode_count(state) < 2) {
                state->client_page_owner =
                    MICROS_VFS_PAGE_FREE;
                vfs_prepare_result(
                    request,
                    MICROS_VFS_RESULT_NO_SPACE,
                    action
                );
                return MICROS_VFS_CORE_OK;
            }
            if (
                create_regular_file(
                    state,
                    process,
                    request,
                    io,
                    &response,
                    &result
                ) != MICROS_VFS_CORE_OK
            ) {
                return MICROS_VFS_CORE_ERROR_INVARIANT;
            }
            state->client_page_owner =
                MICROS_VFS_PAGE_FREE;
            if (result != MICROS_VFS_RESULT_OK) {
                vfs_prepare_result(request, result, action);
                return MICROS_VFS_CORE_OK;
            }
            created = true;
        } else {
            state->client_page_owner =
                MICROS_VFS_PAGE_FREE;
            vfs_prepare_result(request, result, action);
            return MICROS_VFS_CORE_OK;
        }
    } else {
        state->client_page_owner = MICROS_VFS_PAGE_FREE;
    }
    if (result != MICROS_VFS_RESULT_OK) {
        vfs_prepare_result(request, result, action);
        return MICROS_VFS_CORE_OK;
    }
    if (
        (
            request->open_flags
                & (
                    MICROS_VFS_OPEN_EXCLUSIVE
                    | MICROS_VFS_OPEN_CREATE
                )
        ) == (
            MICROS_VFS_OPEN_EXCLUSIVE
            | MICROS_VFS_OPEN_CREATE
        )
        && !created
    ) {
        if (
            vfs_putnode(io, response.node, 1)
                != MICROS_VFS_CORE_OK
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        vfs_prepare_result(
            request,
            MICROS_VFS_RESULT_EXISTS,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    if (vfs_mode_is_directory(response.mode)) {
        if (
            (request->open_flags & MICROS_VFS_OPEN_WRITE) != 0
            || (
                request->open_flags & MICROS_VFS_OPEN_CREATE
            ) != 0
        ) {
            if (
                vfs_putnode(io, response.node, 1)
                    != MICROS_VFS_CORE_OK
            ) {
                return MICROS_VFS_CORE_ERROR_INVARIANT;
            }
            vfs_prepare_result(
                request,
                MICROS_VFS_RESULT_IS_DIRECTORY,
                action
            );
            return MICROS_VFS_CORE_OK;
        }
    } else if (
        (
            request->open_flags & MICROS_VFS_OPEN_DIRECTORY
        ) != 0
    ) {
        if (
            vfs_putnode(io, response.node, 1)
                != MICROS_VFS_CORE_OK
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        vfs_prepare_result(
            request,
            MICROS_VFS_RESULT_NOT_DIRECTORY,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    if (
        vfs_namespace_integrate_vnode(
            state,
            &response,
            vnode_scratch,
            io,
            &vnode_slot
        ) != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    state->open_files[open_file_slot] =
        (struct micros_vfs_open_file){
            .state = MICROS_VFS_OPEN_FILE_ACTIVE,
            .kind = MICROS_VFS_OBJECT_RAMFS,
            .access = access,
            .open_flags = request->open_flags,
            .reference_count = 1,
            .vnode = (uint16_t)(vnode_slot + 1),
        };
    process->descriptors[descriptor] =
        (uint16_t)(open_file_slot + 1);
    vfs_prepare_result(request, MICROS_VFS_RESULT_OK, action);
    action->descriptor = (uint32_t)descriptor;
    action->mode = response.mode;
    return MICROS_VFS_CORE_OK;
}

enum micros_vfs_core_error vfs_namespace_handle_mkdir(
    struct micros_vfs_state *state,
    size_t process_slot,
    const struct micros_vfs_request *request,
    const struct micros_vfs_io *io,
    struct micros_vfs_result_action *action
)
{
    struct micros_vfs_process_record *process =
        &state->processes[process_slot];
    struct micros_vfs_ramfs_response response = {0};
    struct micros_vfs_ramfs_response parent_response = {0};
    struct micros_vfs_request parent_request = *request;
    uint8_t name[MICROS_VFS_NAME_MAX + 1] = {0};
    size_t name_length;
    size_t parent_length;
    size_t vnode_scratch;
    enum micros_vfs_result result;
    enum micros_vfs_core_error error;

    if (
        request->descriptor != 0
        || request->count != 0
        || request->open_flags != 0
        || request->path_length < 2
        || request->path_length > MICROS_VFS_PATH_MAX
        || (
            request->mode & ~MICROS_RAMFS_MODE_PERMISSIONS
        ) != 0
    ) {
        vfs_prepare_result(
            request,
            MICROS_VFS_RESULT_MALFORMED,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    if (state->pending.state != MICROS_VFS_PENDING_NONE) {
        vfs_prepare_result(request, MICROS_VFS_RESULT_BUSY, action);
        return MICROS_VFS_CORE_OK;
    }
    error = vfs_copy_application_path(state, request, io, &result);
    if (
        error != MICROS_VFS_CORE_OK
        || result != MICROS_VFS_RESULT_OK
    ) {
        if (error == MICROS_VFS_CORE_OK) {
            vfs_prepare_result(request, result, action);
        }
        return error;
    }
    if (!vfs_find_free_vnode(state, &vnode_scratch)) {
        state->client_page_owner = MICROS_VFS_PAGE_FREE;
        vfs_prepare_result(
            request,
            MICROS_VFS_RESULT_NO_SPACE,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    (void)vnode_scratch;
    error = lookup_path(
        state,
        request,
        process,
        io,
        &response,
        &result
    );
    if (error != MICROS_VFS_CORE_OK) {
        return error;
    }
    if (result == MICROS_VFS_RESULT_OK) {
        state->client_page_owner = MICROS_VFS_PAGE_FREE;
        if (
            vfs_putnode(io, response.node, 1)
                != MICROS_VFS_CORE_OK
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        vfs_prepare_result(
            request,
            MICROS_VFS_RESULT_EXISTS,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    if (result != MICROS_VFS_RESULT_NOT_FOUND) {
        state->client_page_owner = MICROS_VFS_PAGE_FREE;
        vfs_prepare_result(request, result, action);
        return MICROS_VFS_CORE_OK;
    }
    if (
        !extract_final_component(
            state,
            request->path_length,
            true,
            name,
            &name_length,
            &parent_length
        )
    ) {
        state->client_page_owner = MICROS_VFS_PAGE_FREE;
        vfs_prepare_result(
            request,
            MICROS_VFS_RESULT_MALFORMED,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    parent_request.path_length = (uint32_t)parent_length;
    error = lookup_path(
        state,
        &parent_request,
        process,
        io,
        &parent_response,
        &result
    );
    if (
        error != MICROS_VFS_CORE_OK
        || result != MICROS_VFS_RESULT_OK
    ) {
        state->client_page_owner = MICROS_VFS_PAGE_FREE;
        if (error == MICROS_VFS_CORE_OK) {
            vfs_prepare_result(request, result, action);
        }
        return error;
    }
    if (!vfs_mode_is_directory(parent_response.mode)) {
        state->client_page_owner = MICROS_VFS_PAGE_FREE;
        if (
            vfs_putnode(io, parent_response.node, 1)
                != MICROS_VFS_CORE_OK
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        vfs_prepare_result(
            request,
            MICROS_VFS_RESULT_NOT_DIRECTORY,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    if (!create_name_is_valid(name, name_length)) {
        state->client_page_owner = MICROS_VFS_PAGE_FREE;
        if (
            vfs_putnode(io, parent_response.node, 1)
                != MICROS_VFS_CORE_OK
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        vfs_prepare_result(
            request,
            MICROS_VFS_RESULT_MALFORMED,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    error = call_create(
        state,
        MICROS_VFS_RAMFS_MKDIR,
        parent_response.node,
        name,
        name_length,
        MICROS_RAMFS_MODE_DIRECTORY | request->mode,
        io,
        &response,
        &result
    );
    state->client_page_owner = MICROS_VFS_PAGE_FREE;
    if (
        vfs_putnode(io, parent_response.node, 1)
            != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (error != MICROS_VFS_CORE_OK) {
        return error;
    }
    if (result != MICROS_VFS_RESULT_OK) {
        vfs_prepare_result(request, result, action);
        return MICROS_VFS_CORE_OK;
    }
    if (
        response.node != 0
        || response.file_size != 0
        || response.position != 0
        || response.count != 0
        || response.mode != 0
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    vfs_prepare_result(request, MICROS_VFS_RESULT_OK, action);
    return MICROS_VFS_CORE_OK;
}

enum micros_vfs_core_error vfs_namespace_handle_chdir(
    struct micros_vfs_state *state,
    size_t process_slot,
    const struct micros_vfs_request *request,
    const struct micros_vfs_io *io,
    struct micros_vfs_result_action *action
)
{
    struct micros_vfs_process_record *process =
        &state->processes[process_slot];
    struct micros_vfs_ramfs_response response = {0};
    enum micros_vfs_result result;
    size_t vnode_scratch;
    size_t vnode_slot;
    size_t old_vnode_slot;
    enum micros_vfs_core_error error;

    if (
        request->descriptor != 0
        || request->count != 0
        || request->open_flags != 0
        || request->mode != 0
        || request->path_length < 2
        || request->path_length > MICROS_VFS_PATH_MAX
    ) {
        vfs_prepare_result(
            request,
            MICROS_VFS_RESULT_MALFORMED,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    if (state->pending.state != MICROS_VFS_PENDING_NONE) {
        vfs_prepare_result(request, MICROS_VFS_RESULT_BUSY, action);
        return MICROS_VFS_CORE_OK;
    }
    error = vfs_copy_application_path(state, request, io, &result);
    if (
        error != MICROS_VFS_CORE_OK
        || result != MICROS_VFS_RESULT_OK
    ) {
        if (error == MICROS_VFS_CORE_OK) {
            vfs_prepare_result(request, result, action);
        }
        return error;
    }
    if (!vfs_find_free_vnode(state, &vnode_scratch)) {
        state->client_page_owner = MICROS_VFS_PAGE_FREE;
        vfs_prepare_result(
            request,
            MICROS_VFS_RESULT_NO_SPACE,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    error = lookup_path(
        state,
        request,
        process,
        io,
        &response,
        &result
    );
    state->client_page_owner = MICROS_VFS_PAGE_FREE;
    if (error != MICROS_VFS_CORE_OK) {
        return error;
    }
    if (result != MICROS_VFS_RESULT_OK) {
        vfs_prepare_result(request, result, action);
        return MICROS_VFS_CORE_OK;
    }
    if (!vfs_mode_is_directory(response.mode)) {
        if (
            vfs_putnode(io, response.node, 1)
                != MICROS_VFS_CORE_OK
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        vfs_prepare_result(
            request,
            MICROS_VFS_RESULT_NOT_DIRECTORY,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    if (
        vfs_namespace_integrate_vnode(
            state,
            &response,
            vnode_scratch,
            io,
            &vnode_slot
        ) != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    old_vnode_slot = process->working_directory_vnode - 1;
    process->working_directory_vnode =
        (uint16_t)(vnode_slot + 1);
    if (
        vfs_release_vnode_reference(
            state,
            old_vnode_slot,
            io
        ) != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    vfs_prepare_result(request, MICROS_VFS_RESULT_OK, action);
    return MICROS_VFS_CORE_OK;
}

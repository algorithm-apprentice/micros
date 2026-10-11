#include "servers/vfs/vfs_internal.h"

#include <stddef.h>
#include <stdint.h>

#include "lib/spawn/spawn_request.h"

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

static bool request_type_is_known(uint32_t type)
{
    return (
        type >= MICROS_VFS_MESSAGE_OPEN
        && type <= MICROS_VFS_MESSAGE_CHDIR
    );
}

static void prepare_result(
    const struct micros_vfs_request *request,
    enum micros_vfs_result result,
    struct micros_vfs_result_action *action
)
{
    vfs_zero_bytes(action, sizeof(*action));
    action->active = true;
    action->reply_token = request->reply_token;
    action->request_type = request->type;
    action->result = result;
}

static bool response_node_is_valid(
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

static bool ramfs_response_fields_are_zero(
    const struct micros_vfs_ramfs_response *response
)
{
    return (
        response->node == 0
        && response->file_size == 0
        && response->position == 0
        && response->count == 0
        && response->mode == 0
    );
}

static bool map_lookup_result(
    const struct micros_vfs_ramfs_response *response,
    enum micros_vfs_result *result
)
{
    if (!ramfs_response_fields_are_zero(response)) {
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
    if (!ramfs_response_fields_are_zero(response)) {
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

static bool map_write_result(
    const struct micros_vfs_ramfs_response *response,
    enum micros_vfs_result *result
)
{
    if (!ramfs_response_fields_are_zero(response)) {
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
            !response_node_is_valid(response)
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

static enum micros_vfs_core_error integrate_vnode(
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
        !response_node_is_valid(response)
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

static enum micros_vfs_core_error handle_open(
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
        prepare_result(
            request,
            MICROS_VFS_RESULT_MALFORMED,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    if (state->pending.state != MICROS_VFS_PENDING_NONE) {
        prepare_result(request, MICROS_VFS_RESULT_BUSY, action);
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
        prepare_result(request, result, action);
        return MICROS_VFS_CORE_OK;
    }
    if (
        !vfs_find_free_descriptor(process, &descriptor)
        || !vfs_find_free_open_files(state, 1, &open_file_slot)
        || !vfs_find_free_vnode(state, &vnode_scratch)
    ) {
        state->client_page_owner = MICROS_VFS_PAGE_FREE;
        prepare_result(
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
                prepare_result(
                    request,
                    MICROS_VFS_RESULT_NOT_DIRECTORY,
                    action
                );
                return MICROS_VFS_CORE_OK;
            }
            if (vfs_free_vnode_count(state) < 2) {
                state->client_page_owner =
                    MICROS_VFS_PAGE_FREE;
                prepare_result(
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
                prepare_result(request, result, action);
                return MICROS_VFS_CORE_OK;
            }
            created = true;
        } else {
            state->client_page_owner =
                MICROS_VFS_PAGE_FREE;
            prepare_result(request, result, action);
            return MICROS_VFS_CORE_OK;
        }
    } else {
        state->client_page_owner = MICROS_VFS_PAGE_FREE;
    }
    if (result != MICROS_VFS_RESULT_OK) {
        prepare_result(request, result, action);
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
        prepare_result(
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
            prepare_result(
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
        prepare_result(
            request,
            MICROS_VFS_RESULT_NOT_DIRECTORY,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    if (
        integrate_vnode(
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
    prepare_result(request, MICROS_VFS_RESULT_OK, action);
    action->descriptor = (uint32_t)descriptor;
    action->mode = response.mode;
    return MICROS_VFS_CORE_OK;
}

static enum micros_vfs_core_error handle_close(
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
        prepare_result(
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
        prepare_result(request, result, action);
        return MICROS_VFS_CORE_OK;
    }
    (void)open_file;
    if (state->pending.state != MICROS_VFS_PENDING_NONE) {
        prepare_result(request, MICROS_VFS_RESULT_BUSY, action);
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
    prepare_result(request, MICROS_VFS_RESULT_OK, action);
    return MICROS_VFS_CORE_OK;
}

static enum micros_vfs_core_error handle_regular_read(
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
        prepare_result(request, MICROS_VFS_RESULT_OK, action);
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
        prepare_result(request, result, action);
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
        prepare_result(request, result, action);
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
            prepare_result(
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
    prepare_result(request, MICROS_VFS_RESULT_OK, action);
    action->transferred_count = response.count;
    action->position = response.position;
    return MICROS_VFS_CORE_OK;
}

static enum micros_vfs_core_error handle_regular_write(
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
        prepare_result(
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
        prepare_result(request, result, action);
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
        prepare_result(request, result, action);
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
    prepare_result(request, MICROS_VFS_RESULT_OK, action);
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

static enum micros_vfs_core_error handle_getdents(
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
        prepare_result(request, MICROS_VFS_RESULT_OK, action);
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
        prepare_result(request, result, action);
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
        prepare_result(request, result, action);
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
            prepare_result(
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
    prepare_result(request, MICROS_VFS_RESULT_OK, action);
    action->transferred_count = application_count;
    action->position = response.position;
    return MICROS_VFS_CORE_OK;
}

static void advance_tty_request_id(
    struct micros_vfs_state *state,
    uint64_t request_id
)
{
    if (state->next_tty_request_id != request_id) {
        return;
    }
    state->next_tty_request_id =
        vfs_tty_request_id_successor(request_id);
}

static enum micros_vfs_core_error call_tty(
    const struct micros_vfs_io *io,
    const struct micros_vfs_tty_request *request,
    struct micros_vfs_tty_response *response
)
{
    if (
        io == NULL
        || io->tty_call == NULL
        || io->tty_call(
            io->context,
            request,
            response
        ) != MICROS_VFS_BACKEND_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    return MICROS_VFS_CORE_OK;
}

static enum micros_vfs_core_error handle_console_read(
    struct micros_vfs_state *state,
    size_t process_slot,
    size_t open_file_slot,
    const struct micros_vfs_request *request,
    const struct micros_vfs_io *io,
    enum micros_vfs_result *immediate_result
)
{
    struct micros_vfs_tty_request tty_request = {
        .operation = MICROS_VFS_TTY_SUBMIT_READ,
        .request_id = state->next_tty_request_id,
        .count = request->count,
    };
    struct micros_vfs_tty_response response = {0};
    enum micros_vfs_result result;
    micros_grant_t grant = MICROS_GRANT_NONE;

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
        *immediate_result = result;
        return MICROS_VFS_CORE_OK;
    }
    if (state->next_tty_request_id == 0) {
        *immediate_result = MICROS_VFS_RESULT_NO_SPACE;
        return MICROS_VFS_CORE_OK;
    }
    state->backend_page_owner = MICROS_VFS_PAGE_SYNCHRONOUS;
    if (
        vfs_create_backend_grant(
            state,
            io,
            state->tty_endpoint,
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
        *immediate_result = result;
        return MICROS_VFS_CORE_OK;
    }
    tty_request.grant = grant;
    if (
        call_tty(io, &tty_request, &response)
            != MICROS_VFS_CORE_OK
        || response.result != MICROS_TTY_RESULT_OK
        || response.request_id != tty_request.request_id
        || response.transferred_count != 0
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    advance_tty_request_id(state, tty_request.request_id);
    state->pending =
        (struct micros_vfs_pending_operation){
            .state =
                MICROS_VFS_PENDING_TTY_READ_WAIT_COMPLETION,
            .process_slot = (uint16_t)process_slot,
            .descriptor = (uint16_t)request->descriptor,
            .open_file = (uint16_t)open_file_slot,
            .endpoint = request->source,
            .reply_token = request->reply_token,
            .application_grant = request->grant,
            .tty_grant = grant,
            .application_offset = request->grant_offset,
            .count = request->count,
            .request_id = tty_request.request_id,
        };
    state->backend_page_owner = MICROS_VFS_PAGE_TTY_READ;
    *immediate_result = MICROS_VFS_RESULT_OK;
    return MICROS_VFS_CORE_OK;
}

static enum micros_vfs_core_error handle_console_write(
    struct micros_vfs_state *state,
    size_t process_slot,
    size_t open_file_slot,
    const struct micros_vfs_request *request,
    const struct micros_vfs_io *io,
    enum micros_vfs_result *immediate_result
)
{
    struct micros_vfs_tty_request tty_request = {
        .operation = MICROS_VFS_TTY_SUBMIT_WRITE,
        .request_id = state->next_tty_request_id,
        .count = request->count,
    };
    struct micros_vfs_tty_response response = {0};
    enum micros_vfs_client_result copy_result;
    enum micros_vfs_result result;
    micros_grant_t grant = MICROS_GRANT_NONE;

    if (io == NULL || io->client_copy == NULL) {
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
        *immediate_result = MICROS_VFS_RESULT_GRANT;
        return MICROS_VFS_CORE_OK;
    }
    if (copy_result != MICROS_VFS_CLIENT_OK) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (state->next_tty_request_id == 0) {
        state->backend_page_owner = MICROS_VFS_PAGE_FREE;
        *immediate_result = MICROS_VFS_RESULT_NO_SPACE;
        return MICROS_VFS_CORE_OK;
    }
    if (
        vfs_create_backend_grant(
            state,
            io,
            state->tty_endpoint,
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
        *immediate_result = result;
        return MICROS_VFS_CORE_OK;
    }
    tty_request.grant = grant;
    if (
        call_tty(io, &tty_request, &response)
            != MICROS_VFS_CORE_OK
        || response.request_id != tty_request.request_id
        || response.transferred_count != 0
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (response.result == MICROS_TTY_RESULT_BUSY) {
        state->pending =
            (struct micros_vfs_pending_operation){
                .state =
                    MICROS_VFS_PENDING_TTY_WRITE_WAIT_WRITABLE,
                .process_slot = (uint16_t)process_slot,
                .descriptor = (uint16_t)request->descriptor,
                .open_file = (uint16_t)open_file_slot,
                .endpoint = request->source,
                .reply_token = request->reply_token,
                .application_grant = MICROS_GRANT_NONE,
                .tty_grant = grant,
                .count = request->count,
                .request_id = tty_request.request_id,
            };
        state->backend_page_owner =
            MICROS_VFS_PAGE_TTY_WRITE_RETRY;
        *immediate_result = MICROS_VFS_RESULT_OK;
        return MICROS_VFS_CORE_OK;
    }
    if (response.result != MICROS_TTY_RESULT_OK) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (
        vfs_revoke_backend_grant(io, grant)
            != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    state->backend_page_owner = MICROS_VFS_PAGE_FREE;
    advance_tty_request_id(state, tty_request.request_id);
    state->pending =
        (struct micros_vfs_pending_operation){
            .state =
                MICROS_VFS_PENDING_TTY_WRITE_WAIT_COMPLETION,
            .process_slot = (uint16_t)process_slot,
            .descriptor = (uint16_t)request->descriptor,
            .open_file = (uint16_t)open_file_slot,
            .endpoint = request->source,
            .reply_token = request->reply_token,
            .application_grant = MICROS_GRANT_NONE,
            .tty_grant = MICROS_GRANT_NONE,
            .count = request->count,
            .request_id = tty_request.request_id,
        };
    *immediate_result = MICROS_VFS_RESULT_OK;
    return MICROS_VFS_CORE_OK;
}

static enum micros_vfs_core_error handle_transfer(
    struct micros_vfs_state *state,
    size_t process_slot,
    const struct micros_vfs_request *request,
    const struct micros_vfs_io *io,
    struct micros_vfs_result_action *action
)
{
    struct micros_vfs_open_file *open_file;
    struct micros_vfs_vnode *vnode;
    enum micros_vfs_result result;
    size_t open_file_slot;
    uint32_t required_access =
        request->type == MICROS_VFS_MESSAGE_WRITE
            ? MICROS_VFS_ACCESS_WRITE
            : MICROS_VFS_ACCESS_READ;

    if (
        request->path_length != 0
        || request->open_flags != 0
        || request->mode != 0
    ) {
        prepare_result(
            request,
            MICROS_VFS_RESULT_MALFORMED,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    if (request->count > MICROS_VFS_TRANSFER_MAX) {
        prepare_result(
            request,
            MICROS_VFS_RESULT_RANGE,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    if (
        request->count == 0
        && (
            request->grant != 0
            || request->grant_offset != 0
        )
    ) {
        prepare_result(
            request,
            MICROS_VFS_RESULT_MALFORMED,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    if (
        request->type == MICROS_VFS_MESSAGE_GETDENTS
        && request->count != 0
        && (
            request->count < MICROS_VFS_DIRECTORY_RECORD_SIZE
            || (
                request->count
                    % MICROS_VFS_DIRECTORY_RECORD_SIZE
            ) != 0
        )
    ) {
        prepare_result(
            request,
            MICROS_VFS_RESULT_RANGE,
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
        prepare_result(request, result, action);
        return MICROS_VFS_CORE_OK;
    }
    (void)open_file_slot;
    if ((open_file->access & required_access) == 0) {
        prepare_result(
            request,
            MICROS_VFS_RESULT_ACCESS,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    if (request->type == MICROS_VFS_MESSAGE_GETDENTS) {
        if (
            open_file->kind != MICROS_VFS_OBJECT_RAMFS
            || !vfs_mode_is_directory(
                state->vnodes[open_file->vnode - 1].mode
            )
        ) {
            prepare_result(
                request,
                MICROS_VFS_RESULT_NOT_DIRECTORY,
                action
            );
            return MICROS_VFS_CORE_OK;
        }
    } else if (
        open_file->kind == MICROS_VFS_OBJECT_RAMFS
    ) {
        vnode = &state->vnodes[open_file->vnode - 1];
        if (vfs_mode_is_directory(vnode->mode)) {
            prepare_result(
                request,
                MICROS_VFS_RESULT_IS_DIRECTORY,
                action
            );
            return MICROS_VFS_CORE_OK;
        }
    }
    if (state->pending.state != MICROS_VFS_PENDING_NONE) {
        prepare_result(request, MICROS_VFS_RESULT_BUSY, action);
        return MICROS_VFS_CORE_OK;
    }
    if (request->count == 0) {
        prepare_result(request, MICROS_VFS_RESULT_OK, action);
        action->position = open_file->position;
        return MICROS_VFS_CORE_OK;
    }
    if (
        request->type == MICROS_VFS_MESSAGE_READ
        && open_file->kind == MICROS_VFS_OBJECT_CONSOLE
    ) {
        if (
            handle_console_read(
                state,
                process_slot,
                open_file_slot,
                request,
                io,
                &result
            ) != MICROS_VFS_CORE_OK
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        if (result != MICROS_VFS_RESULT_OK) {
            prepare_result(request, result, action);
        }
        return MICROS_VFS_CORE_OK;
    }
    if (
        request->type == MICROS_VFS_MESSAGE_WRITE
        && open_file->kind == MICROS_VFS_OBJECT_CONSOLE
    ) {
        if (
            handle_console_write(
                state,
                process_slot,
                open_file_slot,
                request,
                io,
                &result
            ) != MICROS_VFS_CORE_OK
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        if (result != MICROS_VFS_RESULT_OK) {
            prepare_result(request, result, action);
        }
        return MICROS_VFS_CORE_OK;
    }
    if (
        request->type == MICROS_VFS_MESSAGE_READ
        && open_file->kind == MICROS_VFS_OBJECT_RAMFS
    ) {
        return handle_regular_read(
            state,
            open_file,
            request,
            io,
            action
        );
    }
    if (
        request->type == MICROS_VFS_MESSAGE_WRITE
        && open_file->kind == MICROS_VFS_OBJECT_RAMFS
    ) {
        return handle_regular_write(
            state,
            open_file,
            request,
            io,
            action
        );
    }
    if (request->type == MICROS_VFS_MESSAGE_GETDENTS) {
        return handle_getdents(
            state,
            open_file,
            request,
            io,
            action
        );
    }
    prepare_result(
        request,
        MICROS_VFS_RESULT_STATE,
        action
    );
    return MICROS_VFS_CORE_OK;
}

static enum micros_vfs_core_error handle_mkdir(
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
        prepare_result(
            request,
            MICROS_VFS_RESULT_MALFORMED,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    if (state->pending.state != MICROS_VFS_PENDING_NONE) {
        prepare_result(request, MICROS_VFS_RESULT_BUSY, action);
        return MICROS_VFS_CORE_OK;
    }
    error = vfs_copy_application_path(state, request, io, &result);
    if (
        error != MICROS_VFS_CORE_OK
        || result != MICROS_VFS_RESULT_OK
    ) {
        if (error == MICROS_VFS_CORE_OK) {
            prepare_result(request, result, action);
        }
        return error;
    }
    if (!vfs_find_free_vnode(state, &vnode_scratch)) {
        state->client_page_owner = MICROS_VFS_PAGE_FREE;
        prepare_result(
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
        prepare_result(
            request,
            MICROS_VFS_RESULT_EXISTS,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    if (result != MICROS_VFS_RESULT_NOT_FOUND) {
        state->client_page_owner = MICROS_VFS_PAGE_FREE;
        prepare_result(request, result, action);
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
        prepare_result(
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
            prepare_result(request, result, action);
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
        prepare_result(
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
        prepare_result(
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
        prepare_result(request, result, action);
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
    prepare_result(request, MICROS_VFS_RESULT_OK, action);
    return MICROS_VFS_CORE_OK;
}

static enum micros_vfs_core_error handle_chdir(
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
        prepare_result(
            request,
            MICROS_VFS_RESULT_MALFORMED,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    if (state->pending.state != MICROS_VFS_PENDING_NONE) {
        prepare_result(request, MICROS_VFS_RESULT_BUSY, action);
        return MICROS_VFS_CORE_OK;
    }
    error = vfs_copy_application_path(state, request, io, &result);
    if (
        error != MICROS_VFS_CORE_OK
        || result != MICROS_VFS_RESULT_OK
    ) {
        if (error == MICROS_VFS_CORE_OK) {
            prepare_result(request, result, action);
        }
        return error;
    }
    if (!vfs_find_free_vnode(state, &vnode_scratch)) {
        state->client_page_owner = MICROS_VFS_PAGE_FREE;
        prepare_result(
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
        prepare_result(request, result, action);
        return MICROS_VFS_CORE_OK;
    }
    if (!vfs_mode_is_directory(response.mode)) {
        if (
            vfs_putnode(io, response.node, 1)
                != MICROS_VFS_CORE_OK
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        prepare_result(
            request,
            MICROS_VFS_RESULT_NOT_DIRECTORY,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    if (
        integrate_vnode(
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
    prepare_result(request, MICROS_VFS_RESULT_OK, action);
    return MICROS_VFS_CORE_OK;
}

static void clear_pending(struct micros_vfs_state *state)
{
    vfs_zero_bytes(&state->pending, sizeof(state->pending));
}

static void set_pending_debt(
    struct micros_vfs_state *state,
    enum micros_vfs_pending_state debt
)
{
    clear_pending(state);
    state->pending.state = debt;
}

static void prepare_pending_action(
    const struct micros_vfs_pending_operation *pending,
    uint32_t request_type,
    uint64_t transferred_count,
    struct micros_vfs_result_action *action
)
{
    vfs_zero_bytes(action, sizeof(*action));
    action->active = true;
    action->reply_token = pending->reply_token;
    action->request_type = request_type;
    action->result = MICROS_VFS_RESULT_OK;
    action->transferred_count = transferred_count;
}

static enum micros_vfs_core_error collect_tty_completion(
    struct micros_vfs_state *state,
    const struct micros_vfs_io *io,
    struct micros_vfs_result_action *action
)
{
    struct micros_vfs_pending_operation pending =
        state->pending;
    struct micros_vfs_tty_request request = {
        .operation = MICROS_VFS_TTY_COLLECT,
        .request_id = pending.request_id,
    };
    struct micros_vfs_tty_response response = {0};
    enum micros_vfs_client_result copy_result;

    if (
        call_tty(io, &request, &response)
            != MICROS_VFS_CORE_OK
        || response.result != MICROS_TTY_RESULT_OK
        || response.request_id != pending.request_id
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (
        pending.state
            == MICROS_VFS_PENDING_TTY_READ_WAIT_COMPLETION
    ) {
        if (
            response.transferred_count == 0
            || response.transferred_count > pending.count
            || vfs_revoke_backend_grant(
                io,
                pending.tty_grant
            ) != MICROS_VFS_CORE_OK
            || io->client_copy == NULL
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        state->backend_page_owner = MICROS_VFS_PAGE_FREE;
        copy_result = io->client_copy(
            io->context,
            MICROS_VFS_CLIENT_TO_APPLICATION,
            pending.endpoint,
            pending.application_grant,
            pending.application_offset,
            state->backend_page,
            response.transferred_count
        );
        if (copy_result != MICROS_VFS_CLIENT_OK) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        clear_pending(state);
        prepare_pending_action(
            &pending,
            MICROS_VFS_MESSAGE_READ,
            response.transferred_count,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    if (
        pending.state
            == MICROS_VFS_PENDING_TTY_WRITE_WAIT_COMPLETION
    ) {
        if (response.transferred_count != pending.count) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        clear_pending(state);
        prepare_pending_action(
            &pending,
            MICROS_VFS_MESSAGE_WRITE,
            response.transferred_count,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    return MICROS_VFS_CORE_ERROR_INVARIANT;
}

static enum micros_vfs_core_error retry_tty_write(
    struct micros_vfs_state *state,
    const struct micros_vfs_io *io
)
{
    struct micros_vfs_tty_request request = {
        .operation = MICROS_VFS_TTY_SUBMIT_WRITE,
        .grant = state->pending.tty_grant,
        .request_id = state->pending.request_id,
        .count = state->pending.count,
    };
    struct micros_vfs_tty_response response = {0};

    if (
        call_tty(io, &request, &response)
            != MICROS_VFS_CORE_OK
        || response.request_id != request.request_id
        || response.transferred_count != 0
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (response.result == MICROS_TTY_RESULT_BUSY) {
        return MICROS_VFS_CORE_OK;
    }
    if (
        response.result != MICROS_TTY_RESULT_OK
        || vfs_revoke_backend_grant(
            io,
            state->pending.tty_grant
        ) != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    state->backend_page_owner = MICROS_VFS_PAGE_FREE;
    advance_tty_request_id(state, request.request_id);
    state->pending.state =
        MICROS_VFS_PENDING_TTY_WRITE_WAIT_COMPLETION;
    state->pending.application_grant = MICROS_GRANT_NONE;
    state->pending.tty_grant = MICROS_GRANT_NONE;
    state->pending.application_offset = 0;
    return MICROS_VFS_CORE_OK;
}

#if defined(MICROS_BUILD_VFS_SERVICE_TEST)
static void prepare_test_drain_action(
    uint64_t reply_token,
    enum micros_vfs_result result,
    struct micros_vfs_result_action *action
)
{
    vfs_zero_bytes(action, sizeof(*action));
    action->active = true;
    action->reply_token = reply_token;
    action->request_type = MICROS_VFS_TEST_MESSAGE_DRAIN;
    action->result = result;
}

static enum micros_vfs_core_error submit_test_drain(
    struct micros_vfs_state *state,
    uint64_t reply_token,
    uint64_t request_id,
    const struct micros_vfs_io *io,
    struct micros_vfs_result_action *action
)
{
    struct micros_vfs_tty_request request = {
        .operation = MICROS_VFS_TTY_SUBMIT_WRITE,
        .grant = MICROS_GRANT_NONE,
        .request_id = request_id,
        .count = 1,
    };
    struct micros_vfs_tty_response response = {0};

    if (
        call_tty(io, &request, &response)
            != MICROS_VFS_CORE_OK
        || response.request_id != request_id
        || response.transferred_count != 0
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (response.result == MICROS_TTY_RESULT_BUSY) {
        state->pending =
            (struct micros_vfs_pending_operation){
                .state =
                    MICROS_VFS_PENDING_TTY_TEST_DRAIN_WAIT_WRITABLE,
                .reply_token = reply_token,
                .application_grant = MICROS_GRANT_NONE,
                .tty_grant = MICROS_GRANT_NONE,
                .request_id = request_id,
            };
        return MICROS_VFS_CORE_OK;
    }
    if (response.result != MICROS_TTY_RESULT_GRANT) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    advance_tty_request_id(state, request_id);
    clear_pending(state);
    prepare_test_drain_action(
        reply_token,
        MICROS_VFS_RESULT_OK,
        action
    );
    return MICROS_VFS_CORE_OK;
}
#endif

static enum micros_vfs_core_error cleanup_pending_operation(
    struct micros_vfs_state *state,
    micros_endpoint_t endpoint,
    const struct micros_vfs_io *io,
    bool *debt
)
{
    struct micros_vfs_pending_operation pending =
        state->pending;
    struct micros_vfs_tty_request request = {
        .request_id = pending.request_id,
    };
    struct micros_vfs_tty_response response = {0};

    *debt = false;
    if (
        pending.state == MICROS_VFS_PENDING_NONE
        || pending.endpoint != endpoint
    ) {
        return MICROS_VFS_CORE_OK;
    }
    switch (pending.state) {
    case MICROS_VFS_PENDING_TTY_READ_WAIT_COMPLETION:
        request.operation = MICROS_VFS_TTY_CANCEL;
        if (
            call_tty(io, &request, &response)
                != MICROS_VFS_CORE_OK
            || response.request_id != pending.request_id
            || response.transferred_count != 0
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        if (response.result == MICROS_TTY_RESULT_REQUEST) {
            request.operation = MICROS_VFS_TTY_COLLECT;
            if (
                call_tty(io, &request, &response)
                    != MICROS_VFS_CORE_OK
                || response.result != MICROS_TTY_RESULT_OK
                || response.request_id != pending.request_id
                || response.transferred_count == 0
                || response.transferred_count > pending.count
            ) {
                return MICROS_VFS_CORE_ERROR_INVARIANT;
            }
            *debt = true;
        } else if (response.result != MICROS_TTY_RESULT_OK) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        if (
            vfs_revoke_backend_grant(io, pending.tty_grant)
                != MICROS_VFS_CORE_OK
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        state->backend_page_owner = MICROS_VFS_PAGE_FREE;
        if (*debt) {
            set_pending_debt(
                state,
                MICROS_VFS_PENDING_TTY_COMPLETION_NOTICE_DEBT
            );
        } else {
            clear_pending(state);
        }
        return MICROS_VFS_CORE_OK;
    case MICROS_VFS_PENDING_TTY_WRITE_WAIT_WRITABLE:
        if (
            vfs_revoke_backend_grant(io, pending.tty_grant)
                != MICROS_VFS_CORE_OK
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        state->backend_page_owner = MICROS_VFS_PAGE_FREE;
        advance_tty_request_id(state, pending.request_id);
        set_pending_debt(
            state,
            MICROS_VFS_PENDING_TTY_WRITABLE_NOTICE_DEBT
        );
        *debt = true;
        return MICROS_VFS_CORE_OK;
    case MICROS_VFS_PENDING_TTY_WRITE_WAIT_COMPLETION:
        request.operation = MICROS_VFS_TTY_COLLECT;
        if (
            call_tty(io, &request, &response)
                != MICROS_VFS_CORE_OK
            || response.result != MICROS_TTY_RESULT_OK
            || response.request_id != pending.request_id
            || response.transferred_count != pending.count
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        set_pending_debt(
            state,
            MICROS_VFS_PENDING_TTY_COMPLETION_NOTICE_DEBT
        );
        *debt = true;
        return MICROS_VFS_CORE_OK;
    case MICROS_VFS_PENDING_TTY_COMPLETION_NOTICE_DEBT:
    case MICROS_VFS_PENDING_TTY_WRITABLE_NOTICE_DEBT:
#if defined(MICROS_BUILD_VFS_SERVICE_TEST)
    case MICROS_VFS_PENDING_TTY_TEST_DRAIN_WAIT_WRITABLE:
#endif
    case MICROS_VFS_PENDING_NONE:
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    return MICROS_VFS_CORE_ERROR_INVARIANT;
}

enum micros_vfs_core_error micros_vfs_mount(
    struct micros_vfs_state *state,
    const struct micros_vfs_io *io
)
{
    struct micros_vfs_ramfs_request request = {
        .operation = MICROS_VFS_RAMFS_MOUNT,
    };
    struct micros_vfs_ramfs_response response = {0};
    struct micros_vfs_vnode *root;

    if (
        state == NULL
        || io == NULL
        || io->ramfs_call == NULL
        || micros_vfs_state_validate(state) != MICROS_VFS_CORE_OK
        || state->phase != MICROS_VFS_PHASE_READY_UNMOUNTED
    ) {
        return MICROS_VFS_CORE_ERROR_ARGUMENT;
    }
    if (
        io->ramfs_call(
            io->context,
            &request,
            &response
        ) != MICROS_VFS_BACKEND_OK
        || response.result != MICROS_RAMFS_RESULT_OK
        || response.node != MICROS_VFS_RAMFS_ROOT_NODE
        || !vfs_mode_is_directory(response.mode)
        || !vfs_mode_is_canonical(response.mode)
        || response.file_size != 0
        || response.position != 0
        || response.count != 0
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    root = &state->vnodes[0];
    root->state = MICROS_VFS_VNODE_ACTIVE;
    root->mount_root = true;
    root->node = response.node;
    root->mode = response.mode;
    root->local_reference_count = 1;
    root->backend_reference_count = 1;
    state->phase = MICROS_VFS_PHASE_MOUNTED;
    if (micros_vfs_state_validate(state) != MICROS_VFS_CORE_OK) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    return MICROS_VFS_CORE_OK;
}

enum micros_vfs_core_error micros_vfs_attach_console(
    struct micros_vfs_state *state,
    micros_endpoint_t endpoint,
    uint16_t root_vnode,
    enum micros_vfs_trusted_result *result
)
{
    size_t process_slot;
    size_t open_file_slots[3];
    struct micros_vfs_process_record *process;
    struct micros_vfs_vnode *root;

    if (
        state == NULL
        || result == NULL
        || !vfs_endpoint_is_canonical(endpoint)
        || endpoint == state->self_endpoint
        || endpoint == state->vm_endpoint
        || endpoint == state->pm_endpoint
        || endpoint == state->ramfs_endpoint
        || endpoint == state->tty_endpoint
        || root_vnode >= MICROS_VFS_VNODE_CAPACITY
        || micros_vfs_state_validate(state) != MICROS_VFS_CORE_OK
        || state->phase != MICROS_VFS_PHASE_MOUNTED
    ) {
        return MICROS_VFS_CORE_ERROR_ARGUMENT;
    }
    if (vfs_find_process(state, endpoint, &process_slot)) {
        *result = MICROS_VFS_TRUSTED_EXISTS;
        return MICROS_VFS_CORE_OK;
    }
    if (
        state->vnodes[root_vnode].state
            != MICROS_VFS_VNODE_ACTIVE
        || !state->vnodes[root_vnode].mount_root
    ) {
        return MICROS_VFS_CORE_ERROR_ARGUMENT;
    }
    if (
        !vfs_find_free_process(state, &process_slot)
        || !vfs_find_free_open_files(state, 3, open_file_slots)
        || state->vnodes[root_vnode].local_reference_count
            > UINT32_MAX - 2
    ) {
        *result = MICROS_VFS_TRUSTED_NO_SPACE;
        return MICROS_VFS_CORE_OK;
    }
    process = &state->processes[process_slot];
    process->state = MICROS_VFS_PROCESS_ACTIVE;
    process->endpoint = endpoint;
    process->root_vnode = (uint16_t)(root_vnode + 1);
    process->working_directory_vnode =
        (uint16_t)(root_vnode + 1);
    process->descriptors[0] =
        (uint16_t)(open_file_slots[0] + 1);
    process->descriptors[1] =
        (uint16_t)(open_file_slots[1] + 1);
    process->descriptors[2] =
        (uint16_t)(open_file_slots[2] + 1);
    state->open_files[open_file_slots[0]] =
        (struct micros_vfs_open_file){
            .state = MICROS_VFS_OPEN_FILE_ACTIVE,
            .kind = MICROS_VFS_OBJECT_CONSOLE,
            .access = MICROS_VFS_ACCESS_READ,
            .open_flags = MICROS_VFS_OPEN_READ,
            .reference_count = 1,
        };
    state->open_files[open_file_slots[1]] =
        (struct micros_vfs_open_file){
            .state = MICROS_VFS_OPEN_FILE_ACTIVE,
            .kind = MICROS_VFS_OBJECT_CONSOLE,
            .access = MICROS_VFS_ACCESS_WRITE,
            .open_flags = MICROS_VFS_OPEN_WRITE,
            .reference_count = 1,
        };
    state->open_files[open_file_slots[2]] =
        state->open_files[open_file_slots[1]];
    root = &state->vnodes[root_vnode];
    root->local_reference_count += 2;
    if (micros_vfs_state_validate(state) != MICROS_VFS_CORE_OK) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    *result = MICROS_VFS_TRUSTED_OK;
    return MICROS_VFS_CORE_OK;
}

enum micros_vfs_core_error micros_vfs_share_descriptor(
    struct micros_vfs_state *state,
    micros_endpoint_t source_endpoint,
    uint32_t source_descriptor,
    micros_endpoint_t destination_endpoint,
    uint32_t destination_descriptor,
    enum micros_vfs_trusted_result *result
)
{
    size_t source_process_slot;
    size_t destination_process_slot;
    uint16_t reference;
    struct micros_vfs_open_file *open_file;

    if (
        state == NULL
        || result == NULL
        || !vfs_endpoint_is_canonical(source_endpoint)
        || !vfs_endpoint_is_canonical(destination_endpoint)
        || source_descriptor >= MICROS_VFS_DESCRIPTOR_CAPACITY
        || destination_descriptor >= MICROS_VFS_DESCRIPTOR_CAPACITY
        || micros_vfs_state_validate(state) != MICROS_VFS_CORE_OK
        || state->phase != MICROS_VFS_PHASE_MOUNTED
    ) {
        return MICROS_VFS_CORE_ERROR_ARGUMENT;
    }
    if (
        !vfs_find_process(
            state,
            source_endpoint,
            &source_process_slot
        )
        || !vfs_find_process(
            state,
            destination_endpoint,
            &destination_process_slot
        )
    ) {
        *result = MICROS_VFS_TRUSTED_NOT_FOUND;
        return MICROS_VFS_CORE_OK;
    }
    if (
        state->spawn.phase
            == MICROS_VFS_SPAWN_HIDDEN_COMMITTED
        && (
            source_endpoint == state->spawn.child_endpoint
            || destination_endpoint
                == state->spawn.child_endpoint
        )
    ) {
        *result = MICROS_VFS_TRUSTED_BUSY;
        return MICROS_VFS_CORE_OK;
    }
    reference = state->processes[source_process_slot]
        .descriptors[source_descriptor];
    if (reference == 0) {
        *result = MICROS_VFS_TRUSTED_NOT_FOUND;
        return MICROS_VFS_CORE_OK;
    }
    if (
        state->processes[destination_process_slot]
            .descriptors[destination_descriptor] != 0
    ) {
        *result = MICROS_VFS_TRUSTED_EXISTS;
        return MICROS_VFS_CORE_OK;
    }
    open_file = &state->open_files[reference - 1];
    if (open_file->reference_count == UINT32_MAX) {
        *result = MICROS_VFS_TRUSTED_NO_SPACE;
        return MICROS_VFS_CORE_OK;
    }
    state->processes[destination_process_slot]
        .descriptors[destination_descriptor] = reference;
    ++open_file->reference_count;
    if (micros_vfs_state_validate(state) != MICROS_VFS_CORE_OK) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    *result = MICROS_VFS_TRUSTED_OK;
    return MICROS_VFS_CORE_OK;
}

enum micros_vfs_core_error micros_vfs_detach(
    struct micros_vfs_state *state,
    micros_endpoint_t endpoint,
    const struct micros_vfs_io *io,
    enum micros_vfs_trusted_result *result
)
{
    size_t process_slot;
    struct micros_vfs_process_record *process;
    size_t descriptor;
    size_t root_vnode;
    size_t working_directory_vnode;
    bool debt;

    if (
        state == NULL
        || io == NULL
        || io->ramfs_call == NULL
        || result == NULL
        || !vfs_endpoint_is_canonical(endpoint)
        || micros_vfs_state_validate(state) != MICROS_VFS_CORE_OK
        || state->phase != MICROS_VFS_PHASE_MOUNTED
    ) {
        return MICROS_VFS_CORE_ERROR_ARGUMENT;
    }
    if (!vfs_find_process(state, endpoint, &process_slot)) {
        *result = MICROS_VFS_TRUSTED_NOT_FOUND;
        return MICROS_VFS_CORE_OK;
    }
    if (
        state->spawn.phase != MICROS_VFS_SPAWN_IDLE
        && !state->spawn.trusted_init
        && state->spawn.parent_endpoint == endpoint
    ) {
        *result = MICROS_VFS_TRUSTED_BUSY;
        return MICROS_VFS_CORE_OK;
    }
    if (
        state->spawn.phase
            == MICROS_VFS_SPAWN_HIDDEN_COMMITTED
        && state->spawn.child_endpoint == endpoint
    ) {
        *result = MICROS_VFS_TRUSTED_BUSY;
        return MICROS_VFS_CORE_OK;
    }
    if (
        state->pending.state
            == MICROS_VFS_PENDING_TTY_COMPLETION_NOTICE_DEBT
        || state->pending.state
            == MICROS_VFS_PENDING_TTY_WRITABLE_NOTICE_DEBT
#if defined(MICROS_BUILD_VFS_SERVICE_TEST)
        || state->pending.state
            == MICROS_VFS_PENDING_TTY_TEST_DRAIN_WAIT_WRITABLE
#endif
    ) {
        *result = MICROS_VFS_TRUSTED_BUSY;
        return MICROS_VFS_CORE_OK;
    }
    if (
        cleanup_pending_operation(
            state,
            endpoint,
            io,
            &debt
        ) != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    process = &state->processes[process_slot];
    root_vnode = process->root_vnode - 1;
    working_directory_vnode =
        process->working_directory_vnode - 1;
    for (
        descriptor = 0;
        descriptor < MICROS_VFS_DESCRIPTOR_CAPACITY;
        ++descriptor
    ) {
        uint16_t reference = process->descriptors[descriptor];

        if (reference == 0) {
            continue;
        }
        process->descriptors[descriptor] = 0;
        if (
            vfs_release_open_file_reference(
                state,
                reference - 1,
                io
            ) != MICROS_VFS_CORE_OK
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
    }
    if (
        vfs_release_vnode_reference(
            state,
            working_directory_vnode,
            io
        ) != MICROS_VFS_CORE_OK
        || vfs_release_vnode_reference(
            state,
            root_vnode,
            io
        ) != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    vfs_zero_bytes(process, sizeof(*process));
    if (micros_vfs_state_validate(state) != MICROS_VFS_CORE_OK) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    *result = debt
        ? MICROS_VFS_TRUSTED_PENDING
        : MICROS_VFS_TRUSTED_OK;
    return MICROS_VFS_CORE_OK;
}

static void prepare_control_result(
    const struct micros_vfs_control_request *request,
    enum micros_vfs_control_result result,
    struct micros_vfs_control_result_action *action
)
{
    vfs_zero_bytes(action, sizeof(*action));
    action->active = true;
    action->reply_token = request->reply_token;
    action->command = request->command;
    action->result = result;
}

static bool spawn_path_components_fit(
    const uint8_t *path,
    size_t path_size
)
{
    size_t offset = 0;

    while (offset + 1 < path_size) {
        size_t start;
        size_t length;

        while (
            offset + 1 < path_size
            && path[offset] == '/'
        ) {
            ++offset;
        }
        start = offset;
        while (
            offset + 1 < path_size
            && path[offset] != '/'
        ) {
            ++offset;
        }
        length = offset - start;
        if (
            length > MICROS_VFS_NAME_MAX
            && !(
                length == 1
                && path[start] == '.'
            )
            && !(
                length == 2
                && path[start] == '.'
                && path[start + 1] == '.'
            )
        ) {
            return false;
        }
    }
    return true;
}

static enum micros_vfs_core_error create_spawn_grant(
    const struct micros_vfs_io *io,
    micros_endpoint_t endpoint,
    uint8_t *address,
    size_t length,
    uint32_t permissions,
    micros_grant_t *grant,
    enum micros_vfs_control_result *result
)
{
    enum micros_vfs_backend_grant_result backend_result;

    if (
        io == NULL
        || io->backend_grant_create == NULL
        || !vfs_endpoint_is_canonical(endpoint)
        || address == NULL
        || length == 0
        || grant == NULL
        || result == NULL
        || (
            permissions != MICROS_GRANT_PERMISSION_READ
            && permissions != MICROS_GRANT_PERMISSION_WRITE
        )
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    backend_result = io->backend_grant_create(
        io->context,
        endpoint,
        address,
        length,
        permissions,
        grant
    );
    if (backend_result == MICROS_VFS_BACKEND_GRANT_CAPACITY) {
        *result = MICROS_VFS_CONTROL_RESULT_NO_SPACE;
        return MICROS_VFS_CORE_OK;
    }
    if (
        backend_result != MICROS_VFS_BACKEND_GRANT_OK
        || *grant == 0
        || *grant == MICROS_GRANT_NONE
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    *result = MICROS_VFS_CONTROL_RESULT_OK;
    return MICROS_VFS_CORE_OK;
}

static enum micros_vfs_core_error discard_spawn_preparation(
    struct micros_vfs_state *state,
    const struct micros_vfs_io *io,
    uint64_t node,
    micros_grant_t executable_grant,
    micros_grant_t request_grant
)
{
    if (
        request_grant != MICROS_GRANT_NONE
        && vfs_revoke_backend_grant(io, request_grant)
            != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (
        executable_grant != MICROS_GRANT_NONE
        && vfs_revoke_backend_grant(io, executable_grant)
            != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (
        node != 0
        && vfs_putnode(io, node, 1) != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    vfs_zero_bytes(
        state->spawn_request_bytes,
        sizeof(state->spawn_request_bytes)
    );
    vfs_zero_bytes(
        state->spawn_executable_bytes,
        sizeof(state->spawn_executable_bytes)
    );
    return MICROS_VFS_CORE_OK;
}

static enum micros_vfs_core_error call_spawn_lookup(
    struct micros_vfs_state *state,
    const struct micros_spawn_request *decoded,
    uint16_t start_vnode,
    uint16_t root_vnode,
    const struct micros_vfs_io *io,
    struct micros_vfs_ramfs_response *response,
    enum micros_vfs_control_result *result
)
{
    const uint8_t *path =
        &state->spawn_request_bytes[decoded->path.offset];
    struct micros_vfs_ramfs_request request = {
        .operation = MICROS_VFS_RAMFS_LOOKUP,
        .start = state->vnodes[start_vnode - 1].node,
        .root = state->vnodes[root_vnode - 1].node,
        .length = decoded->path.size,
    };
    micros_grant_t grant = MICROS_GRANT_NONE;
    enum micros_vfs_core_error error;

    error = create_spawn_grant(
        io,
        state->ramfs_endpoint,
        (uint8_t *)path,
        decoded->path.size,
        MICROS_GRANT_PERMISSION_READ,
        &grant,
        result
    );
    if (
        error != MICROS_VFS_CORE_OK
        || *result != MICROS_VFS_CONTROL_RESULT_OK
    ) {
        return error;
    }
    request.grant = grant;
    vfs_zero_bytes(response, sizeof(*response));
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
    if (response->result == MICROS_RAMFS_RESULT_OK) {
        if (
            !response_node_is_valid(response)
            || response->position != 0
            || response->count != 0
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        *result = MICROS_VFS_CONTROL_RESULT_OK;
        return MICROS_VFS_CORE_OK;
    }
    if (
        response->node != 0
        || response->file_size != 0
        || response->position != 0
        || response->count != 0
        || response->mode != 0
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (response->result == MICROS_RAMFS_RESULT_NOT_FOUND) {
        *result = MICROS_VFS_CONTROL_RESULT_NOT_FOUND;
        return MICROS_VFS_CORE_OK;
    }
    if (response->result == MICROS_RAMFS_RESULT_NO_SPACE) {
        *result = MICROS_VFS_CONTROL_RESULT_NO_SPACE;
        return MICROS_VFS_CORE_OK;
    }
    if (
        response->result == MICROS_RAMFS_RESULT_NOT_DIRECTORY
    ) {
        *result = MICROS_VFS_CONTROL_RESULT_ACCESS;
        return MICROS_VFS_CORE_OK;
    }
    return MICROS_VFS_CORE_ERROR_INVARIANT;
}

static enum micros_vfs_core_error read_spawn_executable(
    struct micros_vfs_state *state,
    const struct micros_vfs_ramfs_response *lookup,
    const struct micros_vfs_io *io,
    enum micros_vfs_control_result *result
)
{
    uint64_t offset = 0;

    while (offset < lookup->file_size) {
        struct micros_vfs_ramfs_request request = {
            .operation = MICROS_VFS_RAMFS_READ,
            .node = lookup->node,
            .file_offset = offset,
        };
        struct micros_vfs_ramfs_response response = {0};
        micros_grant_t grant = MICROS_GRANT_NONE;
        enum micros_vfs_core_error error;
        uint64_t remaining = lookup->file_size - offset;

        request.count = remaining > MICROS_VFS_TRANSFER_MAX
            ? MICROS_VFS_TRANSFER_MAX
            : (uint32_t)remaining;
        request.length = request.count;
        error = create_spawn_grant(
            io,
            state->ramfs_endpoint,
            &state->spawn_executable_bytes[offset],
            request.count,
            MICROS_GRANT_PERMISSION_WRITE,
            &grant,
            result
        );
        if (
            error != MICROS_VFS_CORE_OK
            || *result != MICROS_VFS_CONTROL_RESULT_OK
        ) {
            return error;
        }
        request.grant = grant;
        if (
            io->ramfs_call == NULL
            || io->ramfs_call(
                io->context,
                &request,
                &response
            ) != MICROS_VFS_BACKEND_OK
            || vfs_revoke_backend_grant(io, grant)
                != MICROS_VFS_CORE_OK
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        if (response.result != MICROS_RAMFS_RESULT_OK) {
            if (
                response.node != 0
                || response.file_size != 0
                || response.position != 0
                || response.count != 0
                || response.mode != 0
                || response.result
                    == MICROS_RAMFS_RESULT_GRANT
            ) {
                return MICROS_VFS_CORE_ERROR_INVARIANT;
            }
            *result = MICROS_VFS_CONTROL_RESULT_IO;
            return MICROS_VFS_CORE_OK;
        }
        if (
            response.node != 0
            || response.mode != 0
            || response.file_size != lookup->file_size
            || response.count != request.count
            || response.position != offset + request.count
        ) {
            *result = MICROS_VFS_CONTROL_RESULT_IO;
            return MICROS_VFS_CORE_OK;
        }
        offset += request.count;
    }
    *result = MICROS_VFS_CONTROL_RESULT_OK;
    return MICROS_VFS_CORE_OK;
}

static enum micros_vfs_core_error prepare_spawn_image(
    struct micros_vfs_state *state,
    const struct micros_vfs_control_request *request,
    const struct micros_vfs_io *io,
    struct micros_vfs_control_result_action *action
)
{
    struct micros_spawn_request decoded;
    struct micros_spawn_stack_layout stack_layout;
    struct micros_vfs_process_record *parent = NULL;
    struct micros_vfs_ramfs_response lookup = {0};
    micros_grant_t executable_grant = MICROS_GRANT_NONE;
    micros_grant_t request_grant = MICROS_GRANT_NONE;
    uint16_t root_vnode = 1;
    uint16_t start_vnode = 1;
    size_t parent_slot = 0;
    size_t reserved_vnode_slot = 0;
    size_t executable_vnode_slot = 0;
    bool existing_vnode;
    enum micros_vfs_control_result result =
        MICROS_VFS_CONTROL_RESULT_OK;
    enum micros_vfs_client_result client_result;
    enum micros_vfs_core_error error;

    if (
        request->version != MICROS_VFS_PROTOCOL_VERSION
        || request->source != state->pm_endpoint
        || request->reply_token == 0
        || request->lifecycle_token == 0
        || request->semantic_pid == 0
        || request->request_grant == 0
        || request->request_grant == MICROS_GRANT_NONE
        || request->request_size
            < MICROS_SPAWN_REQUEST_SIZE_MIN
        || request->request_size
            > MICROS_SPAWN_REQUEST_SIZE_MAX
        || request->image_token != 0
        || request->descriptor_token != 0
        || request->child_endpoint != 0
        || (
            request->trusted_init
            && request->parent_endpoint != 0
        )
        || (
            !request->trusted_init
            && !vfs_endpoint_is_canonical(
                request->parent_endpoint
            )
        )
    ) {
        prepare_control_result(
            request,
            MICROS_VFS_CONTROL_RESULT_STATE,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    if (
        state->phase != MICROS_VFS_PHASE_MOUNTED
        || state->spawn.phase != MICROS_VFS_SPAWN_IDLE
    ) {
        prepare_control_result(
            request,
            MICROS_VFS_CONTROL_RESULT_STATE,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    if (
        state->last_image_token == UINT64_MAX
        || io == NULL
        || io->client_validate == NULL
        || io->client_copy == NULL
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (!request->trusted_init) {
        if (
            !vfs_find_process(
                state,
                request->parent_endpoint,
                &parent_slot
            )
        ) {
            prepare_control_result(
                request,
                MICROS_VFS_CONTROL_RESULT_STATE,
                action
            );
            return MICROS_VFS_CORE_OK;
        }
        parent = &state->processes[parent_slot];
        root_vnode = parent->root_vnode;
        start_vnode = parent->working_directory_vnode;
    }
    client_result = io->client_validate(
        io->context,
        state->pm_endpoint,
        request->request_grant,
        0,
        request->request_size,
        MICROS_GRANT_PERMISSION_READ
    );
    if (client_result != MICROS_VFS_CLIENT_OK) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    client_result = io->client_copy(
        io->context,
        MICROS_VFS_CLIENT_FROM_APPLICATION,
        state->pm_endpoint,
        request->request_grant,
        0,
        state->spawn_request_bytes,
        request->request_size
    );
    if (
        client_result != MICROS_VFS_CLIENT_OK
        || micros_spawn_request_decode(
            state->spawn_request_bytes,
            request->request_size,
            &decoded
        ) != MICROS_SPAWN_REQUEST_OK
        || micros_spawn_stack_layout_plan(
            &decoded,
            &stack_layout
        ) != MICROS_SPAWN_STACK_LAYOUT_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (
        (
            request->trusted_init
            && (
                state->spawn_request_bytes[
                    decoded.path.offset
                ] != '/'
                || decoded.descriptor_action_count != 0
            )
        )
        || !spawn_path_components_fit(
            &state->spawn_request_bytes[decoded.path.offset],
            decoded.path.size
        )
    ) {
        result = request->trusted_init
                && (
                    state->spawn_request_bytes[
                        decoded.path.offset
                    ] != '/'
                    || decoded.descriptor_action_count != 0
                )
            ? MICROS_VFS_CONTROL_RESULT_STATE
            : MICROS_VFS_CONTROL_RESULT_TOO_BIG;
        vfs_zero_bytes(
            state->spawn_request_bytes,
            sizeof(state->spawn_request_bytes)
        );
        prepare_control_result(request, result, action);
        return MICROS_VFS_CORE_OK;
    }
    if (
        state->spawn_request_bytes[decoded.path.offset] == '/'
    ) {
        start_vnode = root_vnode;
    }
    error = call_spawn_lookup(
        state,
        &decoded,
        start_vnode,
        root_vnode,
        io,
        &lookup,
        &result
    );
    if (error != MICROS_VFS_CORE_OK) {
        return error;
    }
    if (result != MICROS_VFS_CONTROL_RESULT_OK) {
        vfs_zero_bytes(
            state->spawn_request_bytes,
            sizeof(state->spawn_request_bytes)
        );
        prepare_control_result(request, result, action);
        return MICROS_VFS_CORE_OK;
    }
    if (
        (
            lookup.mode & MICROS_RAMFS_MODE_TYPE_MASK
        ) != MICROS_RAMFS_MODE_REGULAR
        || (lookup.mode & UINT32_C(0111)) == 0
    ) {
        result = MICROS_VFS_CONTROL_RESULT_ACCESS;
    } else if (lookup.file_size == 0) {
        result = MICROS_VFS_CONTROL_RESULT_EXECUTABLE;
    } else if (
        lookup.file_size > MICROS_APPLICATION_EXECUTABLE_SIZE_MAX
    ) {
        result = MICROS_VFS_CONTROL_RESULT_TOO_BIG;
    }
    existing_vnode = vfs_find_vnode_by_node(
        state,
        lookup.node,
        &executable_vnode_slot
    );
    if (
        result == MICROS_VFS_CONTROL_RESULT_OK
        && !existing_vnode
        && !vfs_find_free_vnode(state, &reserved_vnode_slot)
    ) {
        result = MICROS_VFS_CONTROL_RESULT_NO_SPACE;
    }
    if (result == MICROS_VFS_CONTROL_RESULT_OK) {
        error = read_spawn_executable(
            state,
            &lookup,
            io,
            &result
        );
        if (error != MICROS_VFS_CORE_OK) {
            return error;
        }
    }
    if (result == MICROS_VFS_CONTROL_RESULT_OK) {
        error = create_spawn_grant(
            io,
            state->vm_endpoint,
            state->spawn_executable_bytes,
            lookup.file_size,
            MICROS_GRANT_PERMISSION_READ,
            &executable_grant,
            &result
        );
        if (error != MICROS_VFS_CORE_OK) {
            return error;
        }
    }
    if (result == MICROS_VFS_CONTROL_RESULT_OK) {
        error = create_spawn_grant(
            io,
            state->vm_endpoint,
            state->spawn_request_bytes,
            request->request_size,
            MICROS_GRANT_PERMISSION_READ,
            &request_grant,
            &result
        );
        if (error != MICROS_VFS_CORE_OK) {
            return error;
        }
    }
    if (result != MICROS_VFS_CONTROL_RESULT_OK) {
        error = discard_spawn_preparation(
            state,
            io,
            lookup.node,
            executable_grant,
            request_grant
        );
        if (error != MICROS_VFS_CORE_OK) {
            return error;
        }
        prepare_control_result(request, result, action);
        return MICROS_VFS_CORE_OK;
    }
    error = integrate_vnode(
        state,
        &lookup,
        reserved_vnode_slot,
        io,
        &executable_vnode_slot
    );
    if (error != MICROS_VFS_CORE_OK) {
        return error;
    }
    ++state->last_image_token;
    state->spawn = (struct micros_vfs_spawn_transaction){
        .phase = MICROS_VFS_SPAWN_IMAGE_PREPARED,
        .trusted_init = request->trusted_init,
        .parent_endpoint = request->parent_endpoint,
        .pm_spawn_token = request->lifecycle_token,
        .image_token = state->last_image_token,
        .semantic_pid = request->semantic_pid,
        .request_size = request->request_size,
        .executable_vnode =
            (uint16_t)(executable_vnode_slot + 1),
        .executable_size = lookup.file_size,
        .executable_grant = executable_grant,
        .request_grant = request_grant,
    };
    prepare_control_result(
        request,
        MICROS_VFS_CONTROL_RESULT_OK,
        action
    );
    action->image_token = state->spawn.image_token;
    action->executable_grant = executable_grant;
    action->request_grant = request_grant;
    action->request_size = request->request_size;
    action->executable_size = lookup.file_size;
    if (micros_vfs_state_validate(state) != MICROS_VFS_CORE_OK) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    return MICROS_VFS_CORE_OK;
}

static enum micros_vfs_core_error prepare_spawn_descriptors(
    struct micros_vfs_state *state,
    const struct micros_vfs_control_request *request,
    struct micros_vfs_control_result_action *action
)
{
    struct micros_spawn_request decoded;
    struct micros_vfs_process_record *parent = NULL;
    uint16_t child_descriptors[MICROS_VFS_DESCRIPTOR_CAPACITY] = {0};
    size_t open_file_slots[3] = {0};
    size_t child_process_slot;
    size_t parent_slot = 0;
    size_t descriptor;
    uint16_t root_vnode;
    uint16_t working_directory_vnode;

    if (
        request->version != MICROS_VFS_PROTOCOL_VERSION
        || request->source != state->pm_endpoint
        || request->reply_token == 0
        || request->lifecycle_token == 0
        || request->image_token == 0
        || request->semantic_pid == 0
        || request->trusted_init
        || request->parent_endpoint != 0
        || request->request_grant != 0
        || request->request_size != 0
        || request->descriptor_token != 0
        || request->child_endpoint != 0
        || state->phase != MICROS_VFS_PHASE_MOUNTED
        || state->spawn.phase
            != MICROS_VFS_SPAWN_IMAGE_PREPARED
        || request->lifecycle_token
            != state->spawn.pm_spawn_token
        || request->image_token != state->spawn.image_token
        || request->semantic_pid != state->spawn.semantic_pid
    ) {
        prepare_control_result(
            request,
            MICROS_VFS_CONTROL_RESULT_STATE,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    if (
        state->last_descriptor_token == UINT64_MAX
        || micros_spawn_request_decode(
            state->spawn_request_bytes,
            state->spawn.request_size,
            &decoded
        ) != MICROS_SPAWN_REQUEST_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (state->spawn.trusted_init) {
        root_vnode = 1;
        working_directory_vnode = 1;
        if (
            decoded.descriptor_action_count != 0
            || !vfs_find_free_open_files(
                state,
                3,
                open_file_slots
            )
            || state->vnodes[0].local_reference_count
                > UINT32_MAX - 2
        ) {
            enum micros_vfs_control_result result =
                decoded.descriptor_action_count != 0
                    ? MICROS_VFS_CONTROL_RESULT_STATE
                    : MICROS_VFS_CONTROL_RESULT_NO_SPACE;

            prepare_control_result(request, result, action);
            return MICROS_VFS_CORE_OK;
        }
        child_descriptors[0] =
            (uint16_t)(open_file_slots[0] + 1);
        child_descriptors[1] =
            (uint16_t)(open_file_slots[1] + 1);
        child_descriptors[2] =
            (uint16_t)(open_file_slots[2] + 1);
    } else {
        if (
            !vfs_find_process(
                state,
                state->spawn.parent_endpoint,
                &parent_slot
            )
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        parent = &state->processes[parent_slot];
        root_vnode = parent->root_vnode;
        working_directory_vnode =
            parent->working_directory_vnode;
        for (
            descriptor = 0;
            descriptor < decoded.descriptor_action_count;
            ++descriptor
        ) {
            const struct micros_spawn_descriptor_action *item =
                &decoded.descriptor_actions[descriptor];
            uint16_t reference =
                parent->descriptors[item->source];
            size_t same_reference_count = 1;
            size_t prior;

            if (reference == 0) {
                prepare_control_result(
                    request,
                    MICROS_VFS_CONTROL_RESULT_DESCRIPTOR,
                    action
                );
                return MICROS_VFS_CORE_OK;
            }
            for (prior = 0; prior < descriptor; ++prior) {
                if (
                    parent->descriptors[
                        decoded.descriptor_actions[prior].source
                    ] == reference
                ) {
                    ++same_reference_count;
                }
            }
            if (
                state->open_files[reference - 1].reference_count
                    > UINT32_MAX - same_reference_count
            ) {
                prepare_control_result(
                    request,
                    MICROS_VFS_CONTROL_RESULT_NO_SPACE,
                    action
                );
                return MICROS_VFS_CORE_OK;
            }
            child_descriptors[item->destination] = reference;
        }
        if (
            root_vnode == working_directory_vnode
            && state->vnodes[root_vnode - 1]
                .local_reference_count > UINT32_MAX - 2
        ) {
            prepare_control_result(
                request,
                MICROS_VFS_CONTROL_RESULT_NO_SPACE,
                action
            );
            return MICROS_VFS_CORE_OK;
        }
        if (
            root_vnode != working_directory_vnode
            && (
                state->vnodes[root_vnode - 1]
                    .local_reference_count == UINT32_MAX
                || state->vnodes[working_directory_vnode - 1]
                    .local_reference_count == UINT32_MAX
            )
        ) {
            prepare_control_result(
                request,
                MICROS_VFS_CONTROL_RESULT_NO_SPACE,
                action
            );
            return MICROS_VFS_CORE_OK;
        }
    }
    if (!vfs_find_free_process(state, &child_process_slot)) {
        prepare_control_result(
            request,
            MICROS_VFS_CONTROL_RESULT_NO_SPACE,
            action
        );
        return MICROS_VFS_CORE_OK;
    }

    if (state->spawn.trusted_init) {
        state->open_files[open_file_slots[0]] =
            (struct micros_vfs_open_file){
                .state = MICROS_VFS_OPEN_FILE_ACTIVE,
                .kind = MICROS_VFS_OBJECT_CONSOLE,
                .access = MICROS_VFS_ACCESS_READ,
                .open_flags = MICROS_VFS_OPEN_READ,
                .reference_count = 1,
            };
        state->open_files[open_file_slots[1]] =
            (struct micros_vfs_open_file){
                .state = MICROS_VFS_OPEN_FILE_ACTIVE,
                .kind = MICROS_VFS_OBJECT_CONSOLE,
                .access = MICROS_VFS_ACCESS_WRITE,
                .open_flags = MICROS_VFS_OPEN_WRITE,
                .reference_count = 1,
            };
        state->open_files[open_file_slots[2]] =
            state->open_files[open_file_slots[1]];
    } else {
        for (
            descriptor = 0;
            descriptor < MICROS_VFS_DESCRIPTOR_CAPACITY;
            ++descriptor
        ) {
            uint16_t reference = child_descriptors[descriptor];

            if (reference != 0) {
                ++state->open_files[reference - 1].reference_count;
            }
        }
    }
    ++state->vnodes[root_vnode - 1].local_reference_count;
    ++state->vnodes[
        working_directory_vnode - 1
    ].local_reference_count;
    ++state->last_descriptor_token;
    state->spawn.phase =
        MICROS_VFS_SPAWN_DESCRIPTORS_PREPARED;
    state->spawn.child_process_reference =
        (uint16_t)(child_process_slot + 1);
    state->spawn.descriptor_token =
        state->last_descriptor_token;
    state->spawn.child_root_vnode = root_vnode;
    state->spawn.child_working_directory_vnode =
        working_directory_vnode;
    for (
        descriptor = 0;
        descriptor < MICROS_VFS_DESCRIPTOR_CAPACITY;
        ++descriptor
    ) {
        state->spawn.child_descriptors[descriptor] =
            child_descriptors[descriptor];
    }
    prepare_control_result(
        request,
        MICROS_VFS_CONTROL_RESULT_OK,
        action
    );
    action->descriptor_token = state->spawn.descriptor_token;
    if (micros_vfs_state_validate(state) != MICROS_VFS_CORE_OK) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    return MICROS_VFS_CORE_OK;
}

static enum micros_vfs_core_error abort_spawn_descriptors(
    struct micros_vfs_state *state,
    const struct micros_vfs_control_request *request,
    const struct micros_vfs_io *io,
    struct micros_vfs_control_result_action *action
)
{
    struct micros_spawn_request decoded;
    size_t descriptor;

    if (
        request->version != MICROS_VFS_PROTOCOL_VERSION
        || request->source != state->pm_endpoint
        || request->reply_token == 0
        || request->lifecycle_token == 0
        || request->image_token == 0
        || request->descriptor_token == 0
        || request->semantic_pid != 0
        || request->trusted_init
        || request->parent_endpoint != 0
        || request->request_grant != 0
        || request->request_size != 0
        || request->child_endpoint != 0
        || state->phase != MICROS_VFS_PHASE_MOUNTED
        || state->spawn.phase
            != MICROS_VFS_SPAWN_DESCRIPTORS_PREPARED
        || request->lifecycle_token
            != state->spawn.pm_spawn_token
        || request->image_token != state->spawn.image_token
        || request->descriptor_token
            != state->spawn.descriptor_token
    ) {
        prepare_control_result(
            request,
            MICROS_VFS_CONTROL_RESULT_STATE,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    if (io == NULL || io->ramfs_call == NULL) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (
        micros_spawn_request_decode(
            state->spawn_request_bytes,
            state->spawn.request_size,
            &decoded
        ) != MICROS_SPAWN_REQUEST_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    descriptor = state->spawn.trusted_init
        ? 3
        : decoded.descriptor_action_count;
    while (descriptor != 0) {
        uint16_t reference;

        --descriptor;
        if (state->spawn.trusted_init) {
            reference =
                state->spawn.child_descriptors[descriptor];
        } else {
            reference = state->spawn.child_descriptors[
                decoded.descriptor_actions[descriptor].destination
            ];
        }
        if (
            !vfs_reference_is_valid(
                reference,
                MICROS_VFS_OPEN_FILE_CAPACITY
            )
            || vfs_release_open_file_reference(
                state,
                reference - 1,
                io
            ) != MICROS_VFS_CORE_OK
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
    }
    if (
        vfs_release_vnode_reference(
            state,
            state->spawn.child_working_directory_vnode - 1,
            io
        ) != MICROS_VFS_CORE_OK
        || vfs_release_vnode_reference(
            state,
            state->spawn.child_root_vnode - 1,
            io
        ) != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    state->spawn.phase = MICROS_VFS_SPAWN_IMAGE_PREPARED;
    state->spawn.child_process_reference = 0;
    state->spawn.descriptor_token = 0;
    state->spawn.child_root_vnode = 0;
    state->spawn.child_working_directory_vnode = 0;
    state->spawn.child_endpoint = 0;
    vfs_zero_bytes(
        state->spawn.child_descriptors,
        sizeof(state->spawn.child_descriptors)
    );
    prepare_control_result(
        request,
        MICROS_VFS_CONTROL_RESULT_OK,
        action
    );
    if (micros_vfs_state_validate(state) != MICROS_VFS_CORE_OK) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    return MICROS_VFS_CORE_OK;
}

static bool hidden_control_request_is_canonical(
    const struct micros_vfs_state *state,
    const struct micros_vfs_control_request *request
)
{
    return (
        request->version == MICROS_VFS_PROTOCOL_VERSION
        && request->source == state->pm_endpoint
        && request->reply_token != 0
        && request->lifecycle_token != 0
        && request->image_token != 0
        && request->descriptor_token != 0
        && request->semantic_pid == 0
        && !request->trusted_init
        && request->parent_endpoint == 0
        && request->request_grant == 0
        && request->request_size == 0
        && vfs_endpoint_is_canonical(request->child_endpoint)
        && request->child_endpoint != state->self_endpoint
        && request->child_endpoint != state->vm_endpoint
        && request->child_endpoint != state->pm_endpoint
        && request->child_endpoint != state->ramfs_endpoint
        && request->child_endpoint != state->tty_endpoint
    );
}

static enum micros_vfs_core_error commit_hidden_process(
    struct micros_vfs_state *state,
    const struct micros_vfs_control_request *request,
    struct micros_vfs_control_result_action *action
)
{
    struct micros_vfs_process_record *child;
    size_t existing_process_slot;
    size_t descriptor;

    if (
        !hidden_control_request_is_canonical(state, request)
        || state->phase != MICROS_VFS_PHASE_MOUNTED
        || state->spawn.phase
            != MICROS_VFS_SPAWN_DESCRIPTORS_PREPARED
        || request->lifecycle_token
            != state->spawn.pm_spawn_token
        || request->image_token != state->spawn.image_token
        || request->descriptor_token
            != state->spawn.descriptor_token
        || vfs_find_process(
            state,
            request->child_endpoint,
            &existing_process_slot
        )
    ) {
        prepare_control_result(
            request,
            MICROS_VFS_CONTROL_RESULT_STATE,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    child = &state->processes[
        state->spawn.child_process_reference - 1
    ];
    child->state = MICROS_VFS_PROCESS_ACTIVE;
    child->endpoint = request->child_endpoint;
    child->root_vnode = state->spawn.child_root_vnode;
    child->working_directory_vnode =
        state->spawn.child_working_directory_vnode;
    for (
        descriptor = 0;
        descriptor < MICROS_VFS_DESCRIPTOR_CAPACITY;
        ++descriptor
    ) {
        child->descriptors[descriptor] =
            state->spawn.child_descriptors[descriptor];
    }
    state->spawn.phase = MICROS_VFS_SPAWN_HIDDEN_COMMITTED;
    state->spawn.child_endpoint = request->child_endpoint;
    prepare_control_result(
        request,
        MICROS_VFS_CONTROL_RESULT_OK,
        action
    );
    if (micros_vfs_state_validate(state) != MICROS_VFS_CORE_OK) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    return MICROS_VFS_CORE_OK;
}

static enum micros_vfs_core_error revert_hidden_process(
    struct micros_vfs_state *state,
    const struct micros_vfs_control_request *request,
    struct micros_vfs_control_result_action *action
)
{
    struct micros_vfs_process_record *child;

    if (
        !hidden_control_request_is_canonical(state, request)
        || state->phase != MICROS_VFS_PHASE_MOUNTED
        || state->spawn.phase
            != MICROS_VFS_SPAWN_HIDDEN_COMMITTED
        || request->lifecycle_token
            != state->spawn.pm_spawn_token
        || request->image_token != state->spawn.image_token
        || request->descriptor_token
            != state->spawn.descriptor_token
        || request->child_endpoint
            != state->spawn.child_endpoint
    ) {
        prepare_control_result(
            request,
            MICROS_VFS_CONTROL_RESULT_STATE,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    child = &state->processes[
        state->spawn.child_process_reference - 1
    ];
    vfs_zero_bytes(child, sizeof(*child));
    state->spawn.phase =
        MICROS_VFS_SPAWN_DESCRIPTORS_PREPARED;
    state->spawn.child_endpoint = 0;
    prepare_control_result(
        request,
        MICROS_VFS_CONTROL_RESULT_OK,
        action
    );
    if (micros_vfs_state_validate(state) != MICROS_VFS_CORE_OK) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    return MICROS_VFS_CORE_OK;
}

static enum micros_vfs_core_error release_spawn_image(
    struct micros_vfs_state *state,
    const struct micros_vfs_control_request *request,
    const struct micros_vfs_io *io,
    struct micros_vfs_control_result_action *action
)
{
    size_t vnode_slot;

    if (
        request->version != MICROS_VFS_PROTOCOL_VERSION
        || request->source != state->pm_endpoint
        || request->reply_token == 0
        || request->lifecycle_token == 0
        || request->image_token == 0
        || request->trusted_init
        || request->parent_endpoint != 0
        || request->semantic_pid != 0
        || request->request_grant != 0
        || request->request_size != 0
        || request->descriptor_token != 0
        || request->child_endpoint != 0
        || state->phase != MICROS_VFS_PHASE_MOUNTED
        || (
            state->spawn.phase
                != MICROS_VFS_SPAWN_IMAGE_PREPARED
            && state->spawn.phase
                != MICROS_VFS_SPAWN_HIDDEN_COMMITTED
        )
        || request->lifecycle_token
            != state->spawn.pm_spawn_token
        || request->image_token != state->spawn.image_token
    ) {
        prepare_control_result(
            request,
            MICROS_VFS_CONTROL_RESULT_STATE,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    vnode_slot = state->spawn.executable_vnode - 1;
    if (
        vfs_revoke_backend_grant(
            io,
            state->spawn.request_grant
        ) != MICROS_VFS_CORE_OK
        || vfs_revoke_backend_grant(
            io,
            state->spawn.executable_grant
        ) != MICROS_VFS_CORE_OK
        || vfs_release_vnode_reference(
            state,
            vnode_slot,
            io
        ) != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    vfs_zero_bytes(
        state->spawn_request_bytes,
        sizeof(state->spawn_request_bytes)
    );
    vfs_zero_bytes(
        state->spawn_executable_bytes,
        sizeof(state->spawn_executable_bytes)
    );
    vfs_zero_bytes(&state->spawn, sizeof(state->spawn));
    prepare_control_result(
        request,
        MICROS_VFS_CONTROL_RESULT_OK,
        action
    );
    if (micros_vfs_state_validate(state) != MICROS_VFS_CORE_OK) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    return MICROS_VFS_CORE_OK;
}

enum micros_vfs_core_error micros_vfs_handle_control(
    struct micros_vfs_state *state,
    const struct micros_vfs_control_request *request,
    const struct micros_vfs_io *io,
    struct micros_vfs_control_result_action *action
)
{
    if (
        state == NULL
        || request == NULL
        || action == NULL
        || micros_vfs_state_validate(state) != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_ARGUMENT;
    }
    vfs_zero_bytes(action, sizeof(*action));
    if (
        request->source != state->pm_endpoint
        || request->reply_token == 0
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (request->command == MICROS_VFS_CONTROL_PREPARE_IMAGE) {
        return prepare_spawn_image(
            state,
            request,
            io,
            action
        );
    }
    if (request->command == MICROS_VFS_CONTROL_RELEASE_IMAGE) {
        return release_spawn_image(
            state,
            request,
            io,
            action
        );
    }
    if (
        request->command
            == MICROS_VFS_CONTROL_PREPARE_DESCRIPTORS
    ) {
        return prepare_spawn_descriptors(
            state,
            request,
            action
        );
    }
    if (
        request->command
            == MICROS_VFS_CONTROL_ABORT_DESCRIPTORS
    ) {
        return abort_spawn_descriptors(
            state,
            request,
            io,
            action
        );
    }
    if (
        request->command == MICROS_VFS_CONTROL_COMMIT_HIDDEN
    ) {
        return commit_hidden_process(
            state,
            request,
            action
        );
    }
    if (
        request->command == MICROS_VFS_CONTROL_REVERT_HIDDEN
    ) {
        return revert_hidden_process(
            state,
            request,
            action
        );
    }
    prepare_control_result(
        request,
        MICROS_VFS_CONTROL_RESULT_STATE,
        action
    );
    return MICROS_VFS_CORE_OK;
}

enum micros_vfs_core_error micros_vfs_handle_request(
    struct micros_vfs_state *state,
    const struct micros_vfs_request *request,
    const struct micros_vfs_io *io,
    struct micros_vfs_result_action *action
)
{
    size_t process_slot;
    enum micros_vfs_core_error error;

    if (
        state == NULL
        || request == NULL
        || action == NULL
        || micros_vfs_state_validate(state) != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_ARGUMENT;
    }
    vfs_zero_bytes(action, sizeof(*action));
    if (request->reply_token == 0) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (!request_type_is_known(request->type)) {
        prepare_result(
            request,
            MICROS_VFS_RESULT_BAD_TYPE,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    if (request->version != MICROS_VFS_PROTOCOL_VERSION) {
        prepare_result(
            request,
            MICROS_VFS_RESULT_BAD_VERSION,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    if (request->flags != 0) {
        prepare_result(
            request,
            MICROS_VFS_RESULT_MALFORMED,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    if (
        !vfs_endpoint_is_canonical(request->source)
        || !vfs_find_process(
            state,
            request->source,
            &process_slot
        )
    ) {
        prepare_result(
            request,
            MICROS_VFS_RESULT_CALLER,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    if (state->phase != MICROS_VFS_PHASE_MOUNTED) {
        prepare_result(
            request,
            MICROS_VFS_RESULT_STATE,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    switch (request->type) {
    case MICROS_VFS_MESSAGE_OPEN:
        error = handle_open(
            state,
            process_slot,
            request,
            io,
            action
        );
        break;
    case MICROS_VFS_MESSAGE_CLOSE:
        error = handle_close(
            state,
            process_slot,
            request,
            io,
            action
        );
        break;
    case MICROS_VFS_MESSAGE_READ:
    case MICROS_VFS_MESSAGE_WRITE:
    case MICROS_VFS_MESSAGE_GETDENTS:
        error = handle_transfer(
            state,
            process_slot,
            request,
            io,
            action
        );
        break;
    case MICROS_VFS_MESSAGE_MKDIR:
        error = handle_mkdir(
            state,
            process_slot,
            request,
            io,
            action
        );
        break;
    case MICROS_VFS_MESSAGE_CHDIR:
        error = handle_chdir(
            state,
            process_slot,
            request,
            io,
            action
        );
        break;
    default:
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (
        error == MICROS_VFS_CORE_OK
        && micros_vfs_state_validate(state)
            != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    return error;
}

static uint64_t expected_tty_event(
    enum micros_vfs_pending_state state
)
{
    switch (state) {
    case MICROS_VFS_PENDING_TTY_READ_WAIT_COMPLETION:
    case MICROS_VFS_PENDING_TTY_WRITE_WAIT_COMPLETION:
    case MICROS_VFS_PENDING_TTY_COMPLETION_NOTICE_DEBT:
        return MICROS_TTY_EVENT_COMPLETION;
    case MICROS_VFS_PENDING_TTY_WRITE_WAIT_WRITABLE:
    case MICROS_VFS_PENDING_TTY_WRITABLE_NOTICE_DEBT:
#if defined(MICROS_BUILD_VFS_SERVICE_TEST)
    case MICROS_VFS_PENDING_TTY_TEST_DRAIN_WAIT_WRITABLE:
#endif
        return MICROS_TTY_EVENT_WRITABLE;
    case MICROS_VFS_PENDING_NONE:
        return 0;
    }
    return 0;
}

enum micros_vfs_core_error micros_vfs_handle_tty_notification(
    struct micros_vfs_state *state,
    uint64_t events,
    const struct micros_vfs_io *io,
    struct micros_vfs_result_action *action
)
{
    if (
        state == NULL
        || io == NULL
        || action == NULL
        || events == 0
        || (
            events
                & ~(
                    MICROS_TTY_EVENT_COMPLETION
                    | MICROS_TTY_EVENT_WRITABLE
                )
        ) != 0
        || micros_vfs_state_validate(state)
            != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_ARGUMENT;
    }
    if (events != expected_tty_event(state->pending.state)) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    vfs_zero_bytes(action, sizeof(*action));
    if ((events & MICROS_TTY_EVENT_COMPLETION) != 0) {
        if (
            state->pending.state
                == MICROS_VFS_PENDING_TTY_COMPLETION_NOTICE_DEBT
        ) {
            clear_pending(state);
        } else if (
            state->pending.state
                == MICROS_VFS_PENDING_TTY_READ_WAIT_COMPLETION
            || state->pending.state
                == MICROS_VFS_PENDING_TTY_WRITE_WAIT_COMPLETION
        ) {
            if (
                collect_tty_completion(state, io, action)
                    != MICROS_VFS_CORE_OK
            ) {
                return MICROS_VFS_CORE_ERROR_INVARIANT;
            }
        } else {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
    }
    if ((events & MICROS_TTY_EVENT_WRITABLE) != 0) {
        if (
            state->pending.state
                == MICROS_VFS_PENDING_TTY_WRITABLE_NOTICE_DEBT
        ) {
            clear_pending(state);
        } else if (
            state->pending.state
                == MICROS_VFS_PENDING_TTY_WRITE_WAIT_WRITABLE
        ) {
            if (
                retry_tty_write(state, io)
                    != MICROS_VFS_CORE_OK
            ) {
                return MICROS_VFS_CORE_ERROR_INVARIANT;
            }
#if defined(MICROS_BUILD_VFS_SERVICE_TEST)
        } else if (
            state->pending.state
                == MICROS_VFS_PENDING_TTY_TEST_DRAIN_WAIT_WRITABLE
        ) {
            uint64_t reply_token =
                state->pending.reply_token;
            uint64_t request_id =
                state->pending.request_id;

            if (
                submit_test_drain(
                    state,
                    reply_token,
                    request_id,
                    io,
                    action
                ) != MICROS_VFS_CORE_OK
            ) {
                return MICROS_VFS_CORE_ERROR_INVARIANT;
            }
#endif
        } else {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
    }
    if (
        micros_vfs_state_validate(state)
            != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    return MICROS_VFS_CORE_OK;
}

#if defined(MICROS_BUILD_VFS_SERVICE_TEST)
enum micros_vfs_core_error micros_vfs_begin_test_drain(
    struct micros_vfs_state *state,
    micros_endpoint_t endpoint,
    uint64_t reply_token,
    const struct micros_vfs_io *io,
    struct micros_vfs_result_action *action
)
{
    size_t process_slot;

    if (
        state == NULL
        || io == NULL
        || action == NULL
        || reply_token == 0
        || !vfs_endpoint_is_canonical(endpoint)
        || micros_vfs_state_validate(state)
            != MICROS_VFS_CORE_OK
        || !vfs_find_process(state, endpoint, &process_slot)
    ) {
        return MICROS_VFS_CORE_ERROR_ARGUMENT;
    }
    (void)process_slot;
    vfs_zero_bytes(action, sizeof(*action));
    if (state->pending.state != MICROS_VFS_PENDING_NONE) {
        prepare_test_drain_action(
            reply_token,
            MICROS_VFS_RESULT_BUSY,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    if (state->next_tty_request_id == 0) {
        prepare_test_drain_action(
            reply_token,
            MICROS_VFS_RESULT_NO_SPACE,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    if (
        submit_test_drain(
            state,
            reply_token,
            state->next_tty_request_id,
            io,
            action
        ) != MICROS_VFS_CORE_OK
        || micros_vfs_state_validate(state)
            != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    return MICROS_VFS_CORE_OK;
}
#endif

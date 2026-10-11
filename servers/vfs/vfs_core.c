#include "servers/vfs/vfs_internal.h"

#include <stddef.h>
#include <stdint.h>

static bool request_type_is_known(uint32_t type)
{
    return (
        type >= MICROS_VFS_MESSAGE_OPEN
        && type <= MICROS_VFS_MESSAGE_CHDIR
    );
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
        vfs_prepare_result(
            request,
            MICROS_VFS_RESULT_MALFORMED,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    if (request->count > MICROS_VFS_TRANSFER_MAX) {
        vfs_prepare_result(
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
        vfs_prepare_result(
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
        vfs_prepare_result(
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
        vfs_prepare_result(request, result, action);
        return MICROS_VFS_CORE_OK;
    }
    (void)open_file_slot;
    if ((open_file->access & required_access) == 0) {
        vfs_prepare_result(
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
            vfs_prepare_result(
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
            vfs_prepare_result(
                request,
                MICROS_VFS_RESULT_IS_DIRECTORY,
                action
            );
            return MICROS_VFS_CORE_OK;
        }
    }
    if (state->pending.state != MICROS_VFS_PENDING_NONE) {
        vfs_prepare_result(request, MICROS_VFS_RESULT_BUSY, action);
        return MICROS_VFS_CORE_OK;
    }
    if (request->count == 0) {
        vfs_prepare_result(request, MICROS_VFS_RESULT_OK, action);
        action->position = open_file->position;
        return MICROS_VFS_CORE_OK;
    }
    if (
        request->type == MICROS_VFS_MESSAGE_READ
        && open_file->kind == MICROS_VFS_OBJECT_CONSOLE
    ) {
        if (
            vfs_console_handle_read(
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
            vfs_prepare_result(request, result, action);
        }
        return MICROS_VFS_CORE_OK;
    }
    if (
        request->type == MICROS_VFS_MESSAGE_WRITE
        && open_file->kind == MICROS_VFS_OBJECT_CONSOLE
    ) {
        if (
            vfs_console_handle_write(
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
            vfs_prepare_result(request, result, action);
        }
        return MICROS_VFS_CORE_OK;
    }
    if (
        request->type == MICROS_VFS_MESSAGE_READ
        && open_file->kind == MICROS_VFS_OBJECT_RAMFS
    ) {
        return vfs_io_handle_regular_read(
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
        return vfs_io_handle_regular_write(
            state,
            open_file,
            request,
            io,
            action
        );
    }
    if (request->type == MICROS_VFS_MESSAGE_GETDENTS) {
        return vfs_io_handle_getdents(
            state,
            open_file,
            request,
            io,
            action
        );
    }
    vfs_prepare_result(
        request,
        MICROS_VFS_RESULT_STATE,
        action
    );
    return MICROS_VFS_CORE_OK;
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
        return vfs_spawn_prepare_image(
            state,
            request,
            io,
            action
        );
    }
    if (request->command == MICROS_VFS_CONTROL_RELEASE_IMAGE) {
        return vfs_spawn_release_image(
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
        return vfs_spawn_prepare_descriptors(
            state,
            request,
            action
        );
    }
    if (
        request->command
            == MICROS_VFS_CONTROL_ABORT_DESCRIPTORS
    ) {
        return vfs_spawn_abort_descriptors(
            state,
            request,
            io,
            action
        );
    }
    if (
        request->command == MICROS_VFS_CONTROL_COMMIT_HIDDEN
    ) {
        return vfs_spawn_commit_hidden(
            state,
            request,
            action
        );
    }
    if (
        request->command == MICROS_VFS_CONTROL_REVERT_HIDDEN
    ) {
        return vfs_spawn_revert_hidden(
            state,
            request,
            action
        );
    }
    vfs_prepare_control_result(
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
        vfs_prepare_result(
            request,
            MICROS_VFS_RESULT_BAD_TYPE,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    if (request->version != MICROS_VFS_PROTOCOL_VERSION) {
        vfs_prepare_result(
            request,
            MICROS_VFS_RESULT_BAD_VERSION,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    if (request->flags != 0) {
        vfs_prepare_result(
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
        vfs_prepare_result(
            request,
            MICROS_VFS_RESULT_CALLER,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    if (state->phase != MICROS_VFS_PHASE_MOUNTED) {
        vfs_prepare_result(
            request,
            MICROS_VFS_RESULT_STATE,
            action
        );
        return MICROS_VFS_CORE_OK;
    }
    switch (request->type) {
    case MICROS_VFS_MESSAGE_OPEN:
        error = vfs_namespace_handle_open(
            state,
            process_slot,
            request,
            io,
            action
        );
        break;
    case MICROS_VFS_MESSAGE_CLOSE:
        error = vfs_io_handle_close(
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
        error = vfs_namespace_handle_mkdir(
            state,
            process_slot,
            request,
            io,
            action
        );
        break;
    case MICROS_VFS_MESSAGE_CHDIR:
        error = vfs_namespace_handle_chdir(
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

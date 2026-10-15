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

enum micros_vfs_core_error micros_vfs_mount(
    struct micros_vfs_state *state,
    const struct micros_vfs_io *io
)
{
    enum micros_vfs_core_error error;

    if (
        state == NULL
        || io == NULL
        || io->ramfs_call == NULL
        || micros_vfs_state_validate(state) != MICROS_VFS_CORE_OK
        || state->phase != MICROS_VFS_PHASE_READY_UNMOUNTED
    ) {
        return MICROS_VFS_CORE_ERROR_ARGUMENT;
    }
    error = vfs_namespace_mount(state, io);
    if (error != MICROS_VFS_CORE_OK) {
        return error;
    }
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
    enum micros_vfs_core_error error;
    enum micros_vfs_trusted_result transition_result;

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
    error = vfs_process_attach_console(
        state,
        endpoint,
        root_vnode,
        &transition_result
    );
    if (error != MICROS_VFS_CORE_OK) {
        return error;
    }
    if (micros_vfs_state_validate(state) != MICROS_VFS_CORE_OK) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    *result = transition_result;
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
    enum micros_vfs_core_error error;
    enum micros_vfs_trusted_result transition_result;

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
    error = vfs_process_share_descriptor(
        state,
        source_endpoint,
        source_descriptor,
        destination_endpoint,
        destination_descriptor,
        &transition_result
    );
    if (error != MICROS_VFS_CORE_OK) {
        return error;
    }
    if (micros_vfs_state_validate(state) != MICROS_VFS_CORE_OK) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    *result = transition_result;
    return MICROS_VFS_CORE_OK;
}

enum micros_vfs_core_error micros_vfs_detach(
    struct micros_vfs_state *state,
    micros_endpoint_t endpoint,
    const struct micros_vfs_io *io,
    enum micros_vfs_trusted_result *result
)
{
    enum micros_vfs_core_error error;
    enum micros_vfs_trusted_result transition_result;

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
    error = vfs_process_detach(
        state,
        endpoint,
        io,
        &transition_result
    );
    if (error != MICROS_VFS_CORE_OK) {
        return error;
    }
    if (micros_vfs_state_validate(state) != MICROS_VFS_CORE_OK) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    *result = transition_result;
    return MICROS_VFS_CORE_OK;
}

enum micros_vfs_core_error micros_vfs_handle_tty_notification(
    struct micros_vfs_state *state,
    uint64_t events,
    const struct micros_vfs_io *io,
    struct micros_vfs_result_action *action
)
{
    enum micros_vfs_core_error error;

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
    error = vfs_console_handle_notification(
        state,
        events,
        io,
        action
    );
    if (
        error == MICROS_VFS_CORE_OK
        && micros_vfs_state_validate(state)
            != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    return error;
}

#if defined(MICROS_BUILD_VFS_SERVICE_TEST) || defined(MICROS_BUILD_ELF_SPAWN_TEST)
enum micros_vfs_core_error micros_vfs_begin_test_drain(
    struct micros_vfs_state *state,
    micros_endpoint_t endpoint,
    uint64_t reply_token,
    const struct micros_vfs_io *io,
    struct micros_vfs_result_action *action
)
{
    enum micros_vfs_core_error error;
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
    error = vfs_console_begin_test_drain(
        state,
        reply_token,
        io,
        action
    );
    if (
        error == MICROS_VFS_CORE_OK
        && micros_vfs_state_validate(state)
            != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    return error;
}
#endif

enum micros_vfs_core_error micros_vfs_handle_control(
    struct micros_vfs_state *state,
    const struct micros_vfs_control_request *request,
    const struct micros_vfs_io *io,
    struct micros_vfs_control_result_action *action
)
{
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
    if (
        request->source != state->pm_endpoint
        || request->reply_token == 0
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (request->command == MICROS_VFS_CONTROL_PREPARE_IMAGE) {
        error = vfs_spawn_prepare_image(
            state,
            request,
            io,
            action
        );
    } else if (
        request->command == MICROS_VFS_CONTROL_RELEASE_IMAGE
    ) {
        error = vfs_spawn_release_image(
            state,
            request,
            io,
            action
        );
    } else if (
        request->command
            == MICROS_VFS_CONTROL_PREPARE_DESCRIPTORS
    ) {
        error = vfs_spawn_prepare_descriptors(
            state,
            request,
            action
        );
    } else if (
        request->command
            == MICROS_VFS_CONTROL_ABORT_DESCRIPTORS
    ) {
        error = vfs_spawn_abort_descriptors(
            state,
            request,
            io,
            action
        );
    } else if (
        request->command == MICROS_VFS_CONTROL_COMMIT_HIDDEN
    ) {
        error = vfs_spawn_commit_hidden(
            state,
            request,
            action
        );
    } else if (
        request->command == MICROS_VFS_CONTROL_REVERT_HIDDEN
    ) {
        error = vfs_spawn_revert_hidden(
            state,
            request,
            action
        );
    } else if (
        request->command == MICROS_VFS_CONTROL_EXIT_PROCESS
    ) {
        error = vfs_process_exit(
            state,
            request,
            io,
            action
        );
    } else {
        vfs_prepare_control_result(
            request,
            MICROS_VFS_CONTROL_RESULT_STATE,
            action
        );
        error = MICROS_VFS_CORE_OK;
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

static enum micros_vfs_core_error prepare_application_request(
    struct micros_vfs_state *state,
    const struct micros_vfs_request *request,
    size_t *process_slot,
    struct micros_vfs_result_action *action
)
{
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
            process_slot
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
    return MICROS_VFS_CORE_OK;
}

static enum micros_vfs_core_error dispatch_application_request(
    struct micros_vfs_state *state,
    size_t process_slot,
    const struct micros_vfs_request *request,
    const struct micros_vfs_io *io,
    struct micros_vfs_result_action *action
)
{
    switch (request->type) {
    case MICROS_VFS_MESSAGE_OPEN:
        return vfs_namespace_handle_open(
            state,
            process_slot,
            request,
            io,
            action
        );
    case MICROS_VFS_MESSAGE_CLOSE:
        return vfs_io_handle_close(
            state,
            process_slot,
            request,
            io,
            action
        );
    case MICROS_VFS_MESSAGE_READ:
    case MICROS_VFS_MESSAGE_WRITE:
    case MICROS_VFS_MESSAGE_GETDENTS:
        return vfs_io_handle_transfer(
            state,
            process_slot,
            request,
            io,
            action
        );
    case MICROS_VFS_MESSAGE_MKDIR:
        return vfs_namespace_handle_mkdir(
            state,
            process_slot,
            request,
            io,
            action
        );
    case MICROS_VFS_MESSAGE_CHDIR:
        return vfs_namespace_handle_chdir(
            state,
            process_slot,
            request,
            io,
            action
        );
    default:
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
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
    error = prepare_application_request(
        state,
        request,
        &process_slot,
        action
    );
    if (
        error != MICROS_VFS_CORE_OK
        || action->active
    ) {
        return error;
    }
    error = dispatch_application_request(
        state,
        process_slot,
        request,
        io,
        action
    );
    if (
        error == MICROS_VFS_CORE_OK
        && micros_vfs_state_validate(state)
            != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    return error;
}

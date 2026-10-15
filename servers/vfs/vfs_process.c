#include "servers/vfs/vfs_internal.h"

static bool pending_cleanup_debt_exists(
    const struct micros_vfs_state *state
)
{
    return (
        state->pending.state
            == MICROS_VFS_PENDING_TTY_COMPLETION_NOTICE_DEBT
        || state->pending.state
            == MICROS_VFS_PENDING_TTY_WRITABLE_NOTICE_DEBT
#if defined(MICROS_BUILD_VFS_SERVICE_TEST) || defined(MICROS_BUILD_ELF_SPAWN_TEST)
        || state->pending.state
            == MICROS_VFS_PENDING_TTY_TEST_DRAIN_WAIT_WRITABLE
#endif
    );
}

static bool spawn_names_process(
    const struct micros_vfs_state *state,
    micros_endpoint_t endpoint
)
{
    return (
        (
            state->spawn.phase != MICROS_VFS_SPAWN_IDLE
            && !state->spawn.trusted_init
            && state->spawn.parent_endpoint == endpoint
        )
        || (
            state->spawn.phase
                == MICROS_VFS_SPAWN_HIDDEN_COMMITTED
            && state->spawn.child_endpoint == endpoint
        )
    );
}

static enum micros_vfs_core_error release_process_descriptors(
    struct micros_vfs_state *state,
    struct micros_vfs_process_record *process,
    const struct micros_vfs_io *io
)
{
    size_t descriptor;

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
    return MICROS_VFS_CORE_OK;
}

static enum micros_vfs_core_error
release_process_directories(
    struct micros_vfs_state *state,
    const struct micros_vfs_process_record *process,
    const struct micros_vfs_io *io
)
{
    if (
        vfs_release_vnode_reference(
            state,
            process->working_directory_vnode - 1,
            io
        ) != MICROS_VFS_CORE_OK
        || vfs_release_vnode_reference(
            state,
            process->root_vnode - 1,
            io
        ) != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    return MICROS_VFS_CORE_OK;
}

static enum micros_vfs_core_error release_process_owned_state(
    struct micros_vfs_state *state,
    size_t process_slot,
    micros_endpoint_t endpoint,
    const struct micros_vfs_io *io,
    bool *debt
)
{
    struct micros_vfs_process_record *process =
        &state->processes[process_slot];

    if (
        vfs_console_cleanup_pending_operation(
            state,
            endpoint,
            io,
            debt
        ) != MICROS_VFS_CORE_OK
        || release_process_descriptors(
            state,
            process,
            io
        ) != MICROS_VFS_CORE_OK
        || release_process_directories(
            state,
            process,
            io
        ) != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    vfs_zero_bytes(process, sizeof(*process));
    return MICROS_VFS_CORE_OK;
}

enum micros_vfs_core_error vfs_process_attach_console(
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
    *result = MICROS_VFS_TRUSTED_OK;
    return MICROS_VFS_CORE_OK;
}

enum micros_vfs_core_error vfs_process_share_descriptor(
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
    *result = MICROS_VFS_TRUSTED_OK;
    return MICROS_VFS_CORE_OK;
}

enum micros_vfs_core_error vfs_process_detach(
    struct micros_vfs_state *state,
    micros_endpoint_t endpoint,
    const struct micros_vfs_io *io,
    enum micros_vfs_trusted_result *result
)
{
    size_t process_slot;
    bool debt;

    if (!vfs_find_process(state, endpoint, &process_slot)) {
        *result = MICROS_VFS_TRUSTED_NOT_FOUND;
        return MICROS_VFS_CORE_OK;
    }
    if (spawn_names_process(state, endpoint)) {
        *result = MICROS_VFS_TRUSTED_BUSY;
        return MICROS_VFS_CORE_OK;
    }
    if (pending_cleanup_debt_exists(state)) {
        *result = MICROS_VFS_TRUSTED_BUSY;
        return MICROS_VFS_CORE_OK;
    }
    if (
        release_process_owned_state(
            state,
            process_slot,
            endpoint,
            io,
            &debt
        ) != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    *result = debt
        ? MICROS_VFS_TRUSTED_PENDING
        : MICROS_VFS_TRUSTED_OK;
    return MICROS_VFS_CORE_OK;
}

static bool exit_request_is_canonical(
    const struct micros_vfs_state *state,
    const struct micros_vfs_control_request *request
)
{
    /* VFS validates token/PID shape; its cleanup identity is the endpoint. */
    return (
        request->version == MICROS_VFS_PROTOCOL_VERSION
        && request->command == MICROS_VFS_CONTROL_EXIT_PROCESS
        && request->source == state->pm_endpoint
        && request->reply_token != 0
        && request->lifecycle_token != 0
        && request->image_token == 0
        && request->descriptor_token == 0
        && request->semantic_pid != 0
        && request->parent_endpoint == 0
        && request->request_grant == 0
        && request->request_size == 0
        && !request->trusted_init
        && vfs_endpoint_is_canonical(request->child_endpoint)
        && request->child_endpoint != state->self_endpoint
        && request->child_endpoint != state->vm_endpoint
        && request->child_endpoint != state->pm_endpoint
        && request->child_endpoint != state->ramfs_endpoint
        && request->child_endpoint != state->tty_endpoint
        && state->phase == MICROS_VFS_PHASE_MOUNTED
    );
}

static bool vfs_state_names_process(
    const struct micros_vfs_state *state,
    micros_endpoint_t endpoint
)
{
    size_t process_slot;

    return (
        vfs_find_process(state, endpoint, &process_slot)
        || state->pending.endpoint == endpoint
        || state->spawn.parent_endpoint == endpoint
        || state->spawn.child_endpoint == endpoint
    );
}

enum micros_vfs_core_error vfs_process_exit(
    struct micros_vfs_state *state,
    const struct micros_vfs_control_request *request,
    const struct micros_vfs_io *io,
    struct micros_vfs_control_result_action *action
)
{
    size_t process_slot;
    bool debt;

    if (
        !exit_request_is_canonical(state, request)
        || state->spawn.phase != MICROS_VFS_SPAWN_IDLE
        || !vfs_find_process(
            state,
            request->child_endpoint,
            &process_slot
        )
    ) {
        vfs_prepare_control_result(
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
        release_process_owned_state(
            state,
            process_slot,
            request->child_endpoint,
            io,
            &debt
        ) != MICROS_VFS_CORE_OK
        || vfs_state_names_process(
            state,
            request->child_endpoint
        )
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    (void)debt;
    vfs_prepare_control_result(
        request,
        MICROS_VFS_CONTROL_RESULT_OK,
        action
    );
    return MICROS_VFS_CORE_OK;
}

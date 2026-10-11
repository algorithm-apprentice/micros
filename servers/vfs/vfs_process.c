#include "servers/vfs/vfs_internal.h"

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
        vfs_console_cleanup_pending_operation(
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

#include "servers/vfs/vfs_internal.h"

size_t vfs_descriptor_user_count(
    const struct micros_vfs_state *state,
    size_t open_file_slot
)
{
    size_t count = 0;
    size_t process_slot;

    for (
        process_slot = 0;
        process_slot < MICROS_VFS_PROCESS_CAPACITY;
        ++process_slot
    ) {
        const struct micros_vfs_process_record *process =
            &state->processes[process_slot];
        size_t descriptor;

        if (process->state != MICROS_VFS_PROCESS_ACTIVE) {
            continue;
        }
        for (
            descriptor = 0;
            descriptor < MICROS_VFS_DESCRIPTOR_CAPACITY;
            ++descriptor
        ) {
            if (
                process->descriptors[descriptor]
                    == open_file_slot + 1
            ) {
                ++count;
            }
        }
    }
    if (
        state->spawn.phase
            == MICROS_VFS_SPAWN_DESCRIPTORS_PREPARED
    ) {
        size_t descriptor;

        for (
            descriptor = 0;
            descriptor < MICROS_VFS_DESCRIPTOR_CAPACITY;
            ++descriptor
        ) {
            if (
                state->spawn.child_descriptors[descriptor]
                    == open_file_slot + 1
            ) {
                ++count;
            }
        }
    }
    return count;
}

size_t vfs_vnode_user_count(
    const struct micros_vfs_state *state,
    size_t vnode_slot
)
{
    size_t count = vnode_slot == 0 ? 1 : 0;
    size_t process_slot;
    size_t open_file_slot;

    if (
        state->spawn.phase != MICROS_VFS_SPAWN_IDLE
        && state->spawn.executable_vnode == vnode_slot + 1
    ) {
        ++count;
    }
    if (
        state->spawn.phase
            == MICROS_VFS_SPAWN_DESCRIPTORS_PREPARED
    ) {
        if (state->spawn.child_root_vnode == vnode_slot + 1) {
            ++count;
        }
        if (
            state->spawn.child_working_directory_vnode
                == vnode_slot + 1
        ) {
            ++count;
        }
    }

    for (
        process_slot = 0;
        process_slot < MICROS_VFS_PROCESS_CAPACITY;
        ++process_slot
    ) {
        const struct micros_vfs_process_record *process =
            &state->processes[process_slot];

        if (process->state != MICROS_VFS_PROCESS_ACTIVE) {
            continue;
        }
        if (process->root_vnode == vnode_slot + 1) {
            ++count;
        }
        if (
            process->working_directory_vnode
                == vnode_slot + 1
        ) {
            ++count;
        }
    }
    for (
        open_file_slot = 0;
        open_file_slot < MICROS_VFS_OPEN_FILE_CAPACITY;
        ++open_file_slot
    ) {
        const struct micros_vfs_open_file *open_file =
            &state->open_files[open_file_slot];

        if (
            open_file->state == MICROS_VFS_OPEN_FILE_ACTIVE
            && open_file->kind == MICROS_VFS_OBJECT_RAMFS
            && open_file->vnode == vnode_slot + 1
        ) {
            ++count;
        }
    }
    return count;
}

bool vfs_find_process(
    const struct micros_vfs_state *state,
    micros_endpoint_t endpoint,
    size_t *process_slot
)
{
    size_t index;

    for (
        index = 0;
        index < MICROS_VFS_PROCESS_CAPACITY;
        ++index
    ) {
        if (
            state->processes[index].state
                == MICROS_VFS_PROCESS_ACTIVE
            && state->processes[index].endpoint == endpoint
        ) {
            *process_slot = index;
            return true;
        }
    }
    return false;
}

bool vfs_find_free_process(
    const struct micros_vfs_state *state,
    size_t *process_slot
)
{
    size_t index;

    for (
        index = 0;
        index < MICROS_VFS_PROCESS_CAPACITY;
        ++index
    ) {
        if (
            state->processes[index].state
                == MICROS_VFS_PROCESS_FREE
            && (
                state->spawn.child_process_reference == 0
                || state->spawn.child_process_reference
                    != index + 1
            )
        ) {
            *process_slot = index;
            return true;
        }
    }
    return false;
}

bool vfs_find_free_open_files(
    const struct micros_vfs_state *state,
    size_t required,
    size_t *slots
)
{
    size_t count = 0;
    size_t index;

    for (
        index = 0;
        index < MICROS_VFS_OPEN_FILE_CAPACITY
            && count < required;
        ++index
    ) {
        if (
            state->open_files[index].state
                == MICROS_VFS_OPEN_FILE_FREE
        ) {
            slots[count] = index;
            ++count;
        }
    }
    return count == required;
}

enum micros_vfs_core_error vfs_putnode(
    const struct micros_vfs_io *io,
    uint64_t node,
    uint32_t count
)
{
    struct micros_vfs_ramfs_request request = {
        .operation = MICROS_VFS_RAMFS_PUTNODE,
        .node = node,
        .count = count,
    };
    struct micros_vfs_ramfs_response response = {0};

    if (
        io == NULL
        || io->ramfs_call == NULL
        || count == 0
        || io->ramfs_call(
            io->context,
            &request,
            &response
        ) != MICROS_VFS_BACKEND_OK
        || response.result != MICROS_RAMFS_RESULT_OK
        || response.node != 0
        || response.file_size != 0
        || response.position != 0
        || response.count != 0
        || response.mode != 0
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    return MICROS_VFS_CORE_OK;
}

enum micros_vfs_core_error vfs_release_vnode_reference(
    struct micros_vfs_state *state,
    size_t vnode_slot,
    const struct micros_vfs_io *io
)
{
    struct micros_vfs_vnode *vnode;

    if (
        vnode_slot >= MICROS_VFS_VNODE_CAPACITY
        || state->vnodes[vnode_slot].state
            != MICROS_VFS_VNODE_ACTIVE
        || state->vnodes[vnode_slot].local_reference_count == 0
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    vnode = &state->vnodes[vnode_slot];
    --vnode->local_reference_count;
    if (vnode->local_reference_count != 0) {
        return MICROS_VFS_CORE_OK;
    }
    if (
        vnode_slot == 0
        || vnode->mount_root
        || vnode->backend_reference_count == 0
        || vfs_putnode(
            io,
            vnode->node,
            vnode->backend_reference_count
        ) != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    vfs_zero_bytes(vnode, sizeof(*vnode));
    return MICROS_VFS_CORE_OK;
}

enum micros_vfs_core_error vfs_release_open_file_reference(
    struct micros_vfs_state *state,
    size_t open_file_slot,
    const struct micros_vfs_io *io
)
{
    struct micros_vfs_open_file *open_file;
    size_t vnode_slot = 0;
    bool release_vnode = false;

    if (
        open_file_slot >= MICROS_VFS_OPEN_FILE_CAPACITY
        || state->open_files[open_file_slot].state
            != MICROS_VFS_OPEN_FILE_ACTIVE
        || state->open_files[open_file_slot].reference_count == 0
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    open_file = &state->open_files[open_file_slot];
    --open_file->reference_count;
    if (open_file->reference_count != 0) {
        return MICROS_VFS_CORE_OK;
    }
    if (open_file->kind == MICROS_VFS_OBJECT_RAMFS) {
        if (
            !vfs_reference_is_valid(
                open_file->vnode,
                MICROS_VFS_VNODE_CAPACITY
            )
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        vnode_slot = open_file->vnode - 1;
        release_vnode = true;
    }
    vfs_zero_bytes(open_file, sizeof(*open_file));
    if (release_vnode) {
        return vfs_release_vnode_reference(state, vnode_slot, io);
    }
    return MICROS_VFS_CORE_OK;
}

bool vfs_find_free_descriptor(
    const struct micros_vfs_process_record *process,
    size_t *descriptor
)
{
    size_t index;

    for (
        index = 0;
        index < MICROS_VFS_DESCRIPTOR_CAPACITY;
        ++index
    ) {
        if (process->descriptors[index] == 0) {
            *descriptor = index;
            return true;
        }
    }
    return false;
}

bool vfs_find_free_vnode(
    const struct micros_vfs_state *state,
    size_t *vnode_slot
)
{
    size_t index;

    for (
        index = 1;
        index < MICROS_VFS_VNODE_CAPACITY;
        ++index
    ) {
        if (
            state->vnodes[index].state
                == MICROS_VFS_VNODE_FREE
        ) {
            *vnode_slot = index;
            return true;
        }
    }
    return false;
}

size_t vfs_free_vnode_count(
    const struct micros_vfs_state *state
)
{
    size_t count = 0;
    size_t index;

    for (
        index = 1;
        index < MICROS_VFS_VNODE_CAPACITY;
        ++index
    ) {
        if (
            state->vnodes[index].state
                == MICROS_VFS_VNODE_FREE
        ) {
            ++count;
        }
    }
    return count;
}

bool vfs_find_vnode_by_node(
    const struct micros_vfs_state *state,
    uint64_t node,
    size_t *vnode_slot
)
{
    size_t index;

    for (
        index = 0;
        index < MICROS_VFS_VNODE_CAPACITY;
        ++index
    ) {
        if (
            state->vnodes[index].state
                == MICROS_VFS_VNODE_ACTIVE
            && state->vnodes[index].node == node
        ) {
            *vnode_slot = index;
            return true;
        }
    }
    return false;
}

enum micros_vfs_result vfs_resolve_descriptor(
    struct micros_vfs_state *state,
    size_t process_slot,
    uint32_t descriptor,
    size_t *open_file_slot,
    struct micros_vfs_open_file **open_file
)
{
    uint16_t reference;

    if (descriptor >= MICROS_VFS_DESCRIPTOR_CAPACITY) {
        return MICROS_VFS_RESULT_DESCRIPTOR;
    }
    reference = state->processes[process_slot]
        .descriptors[descriptor];
    if (reference == 0) {
        return MICROS_VFS_RESULT_DESCRIPTOR;
    }
    *open_file_slot = reference - 1;
    *open_file = &state->open_files[*open_file_slot];
    return MICROS_VFS_RESULT_OK;
}

#include "servers/vfs/vfs_core.h"

#include <stddef.h>
#include <stdint.h>

#define MICROS_VFS_INITIALIZATION_MAGIC \
    UINT64_C(0x4d4943524f535646)
#define MICROS_VFS_RAMFS_ROOT_NODE \
    UINT64_C(0x0000000100000000)

static void zero_bytes(void *storage, size_t size)
{
    uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static bool bytes_are_zero(const void *storage, size_t size)
{
    const uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static uint32_t read_u32_le(const uint8_t *bytes)
{
    return (
        (uint32_t)bytes[0]
        | (uint32_t)bytes[1] << 8
        | (uint32_t)bytes[2] << 16
        | (uint32_t)bytes[3] << 24
    );
}

static uint64_t read_u64_le(const uint8_t *bytes)
{
    uint64_t value = 0;
    size_t index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8);
    }
    return value;
}

static void write_u32_le(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static bool endpoint_is_canonical(micros_endpoint_t endpoint)
{
    uint32_t slot_mask =
        (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS) - 1;
    uint32_t slot = endpoint & slot_mask;
    uint32_t generation = endpoint >> MICROS_ENDPOINT_SLOT_BITS;

    return (
        endpoint != MICROS_ENDPOINT_NONE
        && endpoint != MICROS_ENDPOINT_ANY
        && slot < MICROS_PROCESS_CAPACITY
        && generation != 0
        && generation <= MICROS_ENDPOINT_GENERATION_MAX
    );
}

static bool ramfs_handle_is_canonical(uint64_t node)
{
    uint16_t slot = (uint16_t)node;
    uint16_t reserved = (uint16_t)(node >> 16);
    uint32_t generation = (uint32_t)(node >> 32);

    return (
        node != 0
        && slot < MICROS_RAMFS_NODE_CAPACITY
        && reserved == 0
        && generation != 0
    );
}

static bool mode_is_canonical(uint32_t mode)
{
    uint32_t type = mode & MICROS_RAMFS_MODE_TYPE_MASK;

    return (
        (
            type == MICROS_RAMFS_MODE_DIRECTORY
            || type == MICROS_RAMFS_MODE_REGULAR
        )
        && (
            mode
            & ~(
                MICROS_RAMFS_MODE_TYPE_MASK
                | MICROS_RAMFS_MODE_PERMISSIONS
            )
        ) == 0
    );
}

static bool mode_is_directory(uint32_t mode)
{
    return (
        mode & MICROS_RAMFS_MODE_TYPE_MASK
    ) == MICROS_RAMFS_MODE_DIRECTORY;
}

static bool reference_is_valid(uint16_t reference, size_t capacity)
{
    return reference != 0 && (size_t)(reference - 1) < capacity;
}

static uint64_t tty_request_id_successor(uint64_t request_id)
{
    return request_id == UINT64_MAX ? 0 : request_id + 1;
}

static bool free_process_is_canonical(
    const struct micros_vfs_process_record *process
)
{
    return bytes_are_zero(process, sizeof(*process));
}

static bool free_open_file_is_canonical(
    const struct micros_vfs_open_file *open_file
)
{
    return bytes_are_zero(open_file, sizeof(*open_file));
}

static bool free_vnode_is_canonical(
    const struct micros_vfs_vnode *vnode
)
{
    return bytes_are_zero(vnode, sizeof(*vnode));
}

static bool pending_debt_is_canonical(
    const struct micros_vfs_pending_operation *pending
)
{
    struct micros_vfs_pending_operation expected = {
        .state = pending->state,
    };

    return bytes_are_zero(
        (const uint8_t *)pending + sizeof(pending->state),
        sizeof(*pending) - sizeof(pending->state)
    ) && expected.state == pending->state;
}

static size_t descriptor_user_count(
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
    return count;
}

static size_t vnode_user_count(
    const struct micros_vfs_state *state,
    size_t vnode_slot
)
{
    size_t count = vnode_slot == 0 ? 1 : 0;
    size_t process_slot;
    size_t open_file_slot;

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

static enum micros_vfs_core_error validate_processes(
    const struct micros_vfs_state *state
)
{
    size_t process_slot;

    for (
        process_slot = 0;
        process_slot < MICROS_VFS_PROCESS_CAPACITY;
        ++process_slot
    ) {
        const struct micros_vfs_process_record *process =
            &state->processes[process_slot];
        size_t other_slot;
        size_t descriptor;

        if (process->state == MICROS_VFS_PROCESS_FREE) {
            if (!free_process_is_canonical(process)) {
                return MICROS_VFS_CORE_ERROR_INVARIANT;
            }
            continue;
        }
        if (
            process->state != MICROS_VFS_PROCESS_ACTIVE
            || !endpoint_is_canonical(process->endpoint)
            || process->endpoint == state->self_endpoint
            || process->endpoint == state->ramfs_endpoint
            || process->endpoint == state->tty_endpoint
            || !reference_is_valid(
                process->root_vnode,
                MICROS_VFS_VNODE_CAPACITY
            )
            || !reference_is_valid(
                process->working_directory_vnode,
                MICROS_VFS_VNODE_CAPACITY
            )
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        if (
            state->vnodes[process->root_vnode - 1].state
                != MICROS_VFS_VNODE_ACTIVE
            || !mode_is_directory(
                state->vnodes[process->root_vnode - 1].mode
            )
            || state->vnodes[
                process->working_directory_vnode - 1
            ].state != MICROS_VFS_VNODE_ACTIVE
            || !mode_is_directory(
                state->vnodes[
                    process->working_directory_vnode - 1
                ].mode
            )
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        for (
            other_slot = process_slot + 1;
            other_slot < MICROS_VFS_PROCESS_CAPACITY;
            ++other_slot
        ) {
            if (
                state->processes[other_slot].state
                    == MICROS_VFS_PROCESS_ACTIVE
                && state->processes[other_slot].endpoint
                    == process->endpoint
            ) {
                return MICROS_VFS_CORE_ERROR_INVARIANT;
            }
        }
        for (
            descriptor = 0;
            descriptor < MICROS_VFS_DESCRIPTOR_CAPACITY;
            ++descriptor
        ) {
            uint16_t reference =
                process->descriptors[descriptor];

            if (
                reference != 0
                && (
                    !reference_is_valid(
                        reference,
                        MICROS_VFS_OPEN_FILE_CAPACITY
                    )
                    || state->open_files[reference - 1].state
                        != MICROS_VFS_OPEN_FILE_ACTIVE
                )
            ) {
                return MICROS_VFS_CORE_ERROR_INVARIANT;
            }
        }
    }
    return MICROS_VFS_CORE_OK;
}

static enum micros_vfs_core_error validate_open_files(
    const struct micros_vfs_state *state
)
{
    size_t open_file_slot;

    for (
        open_file_slot = 0;
        open_file_slot < MICROS_VFS_OPEN_FILE_CAPACITY;
        ++open_file_slot
    ) {
        const struct micros_vfs_open_file *open_file =
            &state->open_files[open_file_slot];
        const struct micros_vfs_vnode *vnode;

        if (open_file->state == MICROS_VFS_OPEN_FILE_FREE) {
            if (!free_open_file_is_canonical(open_file)) {
                return MICROS_VFS_CORE_ERROR_INVARIANT;
            }
            continue;
        }
        if (
            open_file->state != MICROS_VFS_OPEN_FILE_ACTIVE
            || (
                open_file->kind != MICROS_VFS_OBJECT_RAMFS
                && open_file->kind != MICROS_VFS_OBJECT_CONSOLE
            )
            || open_file->access == 0
            || (
                open_file->access
                    & ~MICROS_VFS_ACCESS_DEFINED_MASK
            ) != 0
            || (
                open_file->open_flags
                    & ~MICROS_VFS_OPEN_DEFINED_MASK
            ) != 0
            || open_file->access
                != (
                    open_file->open_flags
                    & MICROS_VFS_ACCESS_DEFINED_MASK
                )
            || (
                (
                    open_file->open_flags
                    & MICROS_VFS_OPEN_EXCLUSIVE
                ) != 0
                && (
                    open_file->open_flags
                    & MICROS_VFS_OPEN_CREATE
                ) == 0
            )
            || (
                open_file->open_flags
                    & (
                        MICROS_VFS_OPEN_CREATE
                        | MICROS_VFS_OPEN_DIRECTORY
                    )
            ) == (
                MICROS_VFS_OPEN_CREATE
                | MICROS_VFS_OPEN_DIRECTORY
            )
            || open_file->reference_count == 0
            || descriptor_user_count(state, open_file_slot)
                != open_file->reference_count
            || open_file->reserved != 0
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        if (open_file->kind == MICROS_VFS_OBJECT_CONSOLE) {
            if (
                open_file->vnode != 0
                || open_file->open_flags != open_file->access
                || open_file->position != 0
            ) {
                return MICROS_VFS_CORE_ERROR_INVARIANT;
            }
            continue;
        }
        if (
            !reference_is_valid(
                open_file->vnode,
                MICROS_VFS_VNODE_CAPACITY
            )
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        vnode = &state->vnodes[open_file->vnode - 1];
        if (
            vnode->state != MICROS_VFS_VNODE_ACTIVE
            || (
                mode_is_directory(vnode->mode)
                && (
                    (open_file->access & MICROS_VFS_ACCESS_WRITE)
                        != 0
                    || (
                        open_file->open_flags
                        & MICROS_VFS_OPEN_CREATE
                    ) != 0
                )
            )
            || (
                !mode_is_directory(vnode->mode)
                && (
                    open_file->open_flags
                    & MICROS_VFS_OPEN_DIRECTORY
                ) != 0
            )
            || (
                mode_is_directory(vnode->mode)
                && open_file->position
                    > MICROS_RAMFS_DIRECTORY_CURSOR_END
            )
            || (
                !mode_is_directory(vnode->mode)
                && open_file->position
                    > MICROS_RAMFS_FILE_SIZE_MAX
            )
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
    }
    return MICROS_VFS_CORE_OK;
}

static enum micros_vfs_core_error validate_vnodes(
    const struct micros_vfs_state *state
)
{
    size_t vnode_slot;

    for (
        vnode_slot = 0;
        vnode_slot < MICROS_VFS_VNODE_CAPACITY;
        ++vnode_slot
    ) {
        const struct micros_vfs_vnode *vnode =
            &state->vnodes[vnode_slot];
        size_t other_slot;

        if (vnode->state == MICROS_VFS_VNODE_FREE) {
            if (!free_vnode_is_canonical(vnode)) {
                return MICROS_VFS_CORE_ERROR_INVARIANT;
            }
            continue;
        }
        if (
            vnode->state != MICROS_VFS_VNODE_ACTIVE
            || vnode->mount_root != (vnode_slot == 0)
            || !bytes_are_zero(
                vnode->reserved0,
                sizeof(vnode->reserved0)
            )
            || !ramfs_handle_is_canonical(vnode->node)
            || (
                vnode_slot == 0
                && vnode->node != MICROS_VFS_RAMFS_ROOT_NODE
            )
            || !mode_is_canonical(vnode->mode)
            || vnode->local_reference_count
                != vnode_user_count(state, vnode_slot)
            || vnode->local_reference_count == 0
            || vnode->backend_reference_count == 0
            || vnode->backend_reference_count
                >= MICROS_VFS_BACKEND_REFERENCE_THRESHOLD
            || vnode->reserved1 != 0
            || (
                mode_is_directory(vnode->mode)
                && vnode->size != 0
            )
            || vnode->size > MICROS_RAMFS_FILE_SIZE_MAX
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        for (
            other_slot = vnode_slot + 1;
            other_slot < MICROS_VFS_VNODE_CAPACITY;
            ++other_slot
        ) {
            if (
                state->vnodes[other_slot].state
                    == MICROS_VFS_VNODE_ACTIVE
                && state->vnodes[other_slot].node == vnode->node
            ) {
                return MICROS_VFS_CORE_ERROR_INVARIANT;
            }
        }
    }
    if (
        state->vnodes[0].state != MICROS_VFS_VNODE_ACTIVE
        || !state->vnodes[0].mount_root
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    return MICROS_VFS_CORE_OK;
}

static enum micros_vfs_core_error validate_pending(
    const struct micros_vfs_state *state
)
{
    const struct micros_vfs_pending_operation *pending =
        &state->pending;
    const struct micros_vfs_process_record *process;
    const struct micros_vfs_open_file *open_file;

    if (state->client_page_owner != MICROS_VFS_PAGE_FREE) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (pending->state == MICROS_VFS_PENDING_NONE) {
        if (
            !bytes_are_zero(pending, sizeof(*pending))
            || state->backend_page_owner != MICROS_VFS_PAGE_FREE
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        return MICROS_VFS_CORE_OK;
    }
    if (
        pending->state
            == MICROS_VFS_PENDING_TTY_COMPLETION_NOTICE_DEBT
        || pending->state
            == MICROS_VFS_PENDING_TTY_WRITABLE_NOTICE_DEBT
    ) {
        if (
            !pending_debt_is_canonical(pending)
            || state->backend_page_owner != MICROS_VFS_PAGE_FREE
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        return MICROS_VFS_CORE_OK;
    }
    if (
        pending->state
            == MICROS_VFS_PENDING_TTY_TEST_DRAIN_WAIT_WRITABLE
    ) {
        if (
            pending->reply_token == 0
            || pending->request_id == 0
            || pending->application_grant != MICROS_GRANT_NONE
            || pending->tty_grant != MICROS_GRANT_NONE
            || pending->process_slot != 0
            || pending->descriptor != 0
            || pending->open_file != 0
            || pending->reserved != 0
            || pending->endpoint != 0
            || pending->application_offset != 0
            || pending->count != 0
            || state->next_tty_request_id != pending->request_id
            || state->backend_page_owner != MICROS_VFS_PAGE_FREE
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        return MICROS_VFS_CORE_OK;
    }
    if (
        pending->state
            != MICROS_VFS_PENDING_TTY_READ_WAIT_COMPLETION
        && pending->state
            != MICROS_VFS_PENDING_TTY_WRITE_WAIT_WRITABLE
        && pending->state
            != MICROS_VFS_PENDING_TTY_WRITE_WAIT_COMPLETION
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (
        pending->process_slot >= MICROS_VFS_PROCESS_CAPACITY
        || pending->descriptor >= MICROS_VFS_DESCRIPTOR_CAPACITY
        || pending->open_file >= MICROS_VFS_OPEN_FILE_CAPACITY
        || pending->reserved != 0
        || !endpoint_is_canonical(pending->endpoint)
        || pending->reply_token == 0
        || pending->count == 0
        || pending->count > MICROS_VFS_TRANSFER_MAX
        || pending->request_id == 0
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    process = &state->processes[pending->process_slot];
    open_file = &state->open_files[pending->open_file];
    if (
        process->state != MICROS_VFS_PROCESS_ACTIVE
        || process->endpoint != pending->endpoint
        || process->descriptors[pending->descriptor]
            != pending->open_file + 1
        || open_file->state != MICROS_VFS_OPEN_FILE_ACTIVE
        || open_file->kind != MICROS_VFS_OBJECT_CONSOLE
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    switch (pending->state) {
    case MICROS_VFS_PENDING_TTY_READ_WAIT_COMPLETION:
        if (
            pending->application_grant == MICROS_GRANT_NONE
            || pending->tty_grant == MICROS_GRANT_NONE
            || (
                open_file->access & MICROS_VFS_ACCESS_READ
            ) == 0
            || UINT64_MAX - pending->application_offset
                < pending->count
            || state->next_tty_request_id
                != tty_request_id_successor(
                    pending->request_id
                )
            || state->backend_page_owner
                != MICROS_VFS_PAGE_TTY_READ
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        break;
    case MICROS_VFS_PENDING_TTY_WRITE_WAIT_WRITABLE:
        if (
            pending->application_grant != MICROS_GRANT_NONE
            || pending->tty_grant == MICROS_GRANT_NONE
            || (
                open_file->access & MICROS_VFS_ACCESS_WRITE
            ) == 0
            || pending->application_offset != 0
            || state->next_tty_request_id
                != pending->request_id
            || state->backend_page_owner
                != MICROS_VFS_PAGE_TTY_WRITE_RETRY
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        break;
    case MICROS_VFS_PENDING_TTY_WRITE_WAIT_COMPLETION:
        if (
            pending->application_grant != MICROS_GRANT_NONE
            || pending->tty_grant != MICROS_GRANT_NONE
            || (
                open_file->access & MICROS_VFS_ACCESS_WRITE
            ) == 0
            || pending->application_offset != 0
            || state->next_tty_request_id
                != tty_request_id_successor(
                    pending->request_id
                )
            || state->backend_page_owner != MICROS_VFS_PAGE_FREE
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        break;
    default:
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    return MICROS_VFS_CORE_OK;
}

static bool find_process(
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

static bool find_free_process(
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
        ) {
            *process_slot = index;
            return true;
        }
    }
    return false;
}

static bool find_free_open_files(
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

static enum micros_vfs_core_error putnode(
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

static enum micros_vfs_core_error release_vnode_reference(
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
        || putnode(
            io,
            vnode->node,
            vnode->backend_reference_count
        ) != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    zero_bytes(vnode, sizeof(*vnode));
    return MICROS_VFS_CORE_OK;
}

static enum micros_vfs_core_error release_open_file_reference(
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
            !reference_is_valid(
                open_file->vnode,
                MICROS_VFS_VNODE_CAPACITY
            )
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        vnode_slot = open_file->vnode - 1;
        release_vnode = true;
    }
    zero_bytes(open_file, sizeof(*open_file));
    if (release_vnode) {
        return release_vnode_reference(state, vnode_slot, io);
    }
    return MICROS_VFS_CORE_OK;
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
    zero_bytes(action, sizeof(*action));
    action->active = true;
    action->reply_token = request->reply_token;
    action->request_type = request->type;
    action->result = result;
}

static bool find_free_descriptor(
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

static bool find_free_vnode(
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

static size_t free_vnode_count(
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

static bool find_vnode_by_node(
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

static bool response_node_is_valid(
    const struct micros_vfs_ramfs_response *response
)
{
    return (
        ramfs_handle_is_canonical(response->node)
        && mode_is_canonical(response->mode)
        && response->file_size <= MICROS_RAMFS_FILE_SIZE_MAX
        && (
            !mode_is_directory(response->mode)
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

static enum micros_vfs_core_error create_backend_grant(
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

static enum micros_vfs_core_error revoke_backend_grant(
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

static enum micros_vfs_core_error copy_application_path(
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
    error = create_backend_grant(
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
        || revoke_backend_grant(io, grant)
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

    if (find_vnode_by_node(state, response->node, vnode_slot)) {
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
                putnode(
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
        state->client_page[0] = 0;
        *parent_length = 1;
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
    zero_bytes(state->client_page, name_length + 1);
    while (name_length != 0) {
        --name_length;
        state->client_page[name_length] = name[name_length];
    }
    state->backend_page_owner = MICROS_VFS_PAGE_SYNCHRONOUS;
    error = create_backend_grant(
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
        || revoke_backend_grant(io, grant)
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
    if (!mode_is_directory(parent_response.mode)) {
        if (
            putnode(io, parent_response.node, 1)
                != MICROS_VFS_CORE_OK
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        *result = MICROS_VFS_RESULT_NOT_DIRECTORY;
        return MICROS_VFS_CORE_OK;
    }
    if (!create_name_is_valid(name, name_length)) {
        if (
            putnode(io, parent_response.node, 1)
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
        putnode(io, parent_response.node, 1)
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
        copy_application_path(
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
        !find_free_descriptor(process, &descriptor)
        || !find_free_open_files(state, 1, &open_file_slot)
        || !find_free_vnode(state, &vnode_scratch)
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
            if (free_vnode_count(state) < 2) {
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
            putnode(io, response.node, 1)
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
    if (mode_is_directory(response.mode)) {
        if (
            (request->open_flags & MICROS_VFS_OPEN_WRITE) != 0
            || (
                request->open_flags & MICROS_VFS_OPEN_CREATE
            ) != 0
        ) {
            if (
                putnode(io, response.node, 1)
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
            putnode(io, response.node, 1)
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

static enum micros_vfs_result resolve_descriptor(
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
    result = resolve_descriptor(
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
        release_open_file_reference(
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

static enum micros_vfs_core_error validate_application_range(
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
        validate_application_range(
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
        create_backend_grant(
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
        || revoke_backend_grant(io, grant)
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
        create_backend_grant(
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
        || revoke_backend_grant(io, grant)
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
        uint64_t node = read_u64_le(&source[0]);
        uint32_t mode = read_u32_le(&source[8]);
        uint32_t name_length = read_u32_le(&source[12]);
        size_t index;

        if (
            !ramfs_handle_is_canonical(node)
            || !mode_is_canonical(mode)
            || name_length == 0
            || name_length > MICROS_VFS_NAME_MAX
            || !bytes_are_zero(
                &source[16 + name_length],
                MICROS_VFS_NAME_MAX - name_length
            )
            || !bytes_are_zero(
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
        zero_bytes(
            destination,
            MICROS_VFS_DIRECTORY_RECORD_SIZE
        );
        write_u32_le(
            &destination[0],
            MICROS_VFS_DIRECTORY_RECORD_SIZE
        );
        write_u32_le(&destination[4], mode);
        write_u32_le(&destination[8], name_length);
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
        validate_application_range(
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
        create_backend_grant(
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
        || revoke_backend_grant(io, grant)
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
        tty_request_id_successor(request_id);
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
        validate_application_range(
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
        create_backend_grant(
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
        create_backend_grant(
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
        revoke_backend_grant(io, grant)
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
    result = resolve_descriptor(
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
            || !mode_is_directory(
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
        if (mode_is_directory(vnode->mode)) {
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
    error = copy_application_path(state, request, io, &result);
    if (
        error != MICROS_VFS_CORE_OK
        || result != MICROS_VFS_RESULT_OK
    ) {
        if (error == MICROS_VFS_CORE_OK) {
            prepare_result(request, result, action);
        }
        return error;
    }
    if (!find_free_vnode(state, &vnode_scratch)) {
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
            putnode(io, response.node, 1)
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
    if (!mode_is_directory(parent_response.mode)) {
        state->client_page_owner = MICROS_VFS_PAGE_FREE;
        if (
            putnode(io, parent_response.node, 1)
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
            putnode(io, parent_response.node, 1)
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
        putnode(io, parent_response.node, 1)
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
    error = copy_application_path(state, request, io, &result);
    if (
        error != MICROS_VFS_CORE_OK
        || result != MICROS_VFS_RESULT_OK
    ) {
        if (error == MICROS_VFS_CORE_OK) {
            prepare_result(request, result, action);
        }
        return error;
    }
    if (!find_free_vnode(state, &vnode_scratch)) {
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
    if (!mode_is_directory(response.mode)) {
        if (
            putnode(io, response.node, 1)
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
        release_vnode_reference(
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
    zero_bytes(&state->pending, sizeof(state->pending));
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
    zero_bytes(action, sizeof(*action));
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
            || revoke_backend_grant(
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
        || revoke_backend_grant(
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

static void prepare_test_drain_action(
    uint64_t reply_token,
    enum micros_vfs_result result,
    struct micros_vfs_result_action *action
)
{
    zero_bytes(action, sizeof(*action));
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
            revoke_backend_grant(io, pending.tty_grant)
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
            revoke_backend_grant(io, pending.tty_grant)
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
    case MICROS_VFS_PENDING_TTY_TEST_DRAIN_WAIT_WRITABLE:
    case MICROS_VFS_PENDING_NONE:
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    return MICROS_VFS_CORE_ERROR_INVARIANT;
}

enum micros_vfs_core_error micros_vfs_state_initialize(
    struct micros_vfs_state *state,
    micros_endpoint_t self_endpoint,
    micros_endpoint_t ramfs_endpoint,
    micros_endpoint_t tty_endpoint
)
{
    if (
        state == NULL
        || !endpoint_is_canonical(self_endpoint)
        || !endpoint_is_canonical(ramfs_endpoint)
        || !endpoint_is_canonical(tty_endpoint)
        || self_endpoint == ramfs_endpoint
        || self_endpoint == tty_endpoint
        || ramfs_endpoint == tty_endpoint
    ) {
        return MICROS_VFS_CORE_ERROR_ARGUMENT;
    }
    zero_bytes(state, sizeof(*state));
    state->initialization_magic = MICROS_VFS_INITIALIZATION_MAGIC;
    state->phase = MICROS_VFS_PHASE_READY_UNMOUNTED;
    state->self_endpoint = self_endpoint;
    state->ramfs_endpoint = ramfs_endpoint;
    state->tty_endpoint = tty_endpoint;
    state->next_tty_request_id = 1;
    return MICROS_VFS_CORE_OK;
}

enum micros_vfs_core_error micros_vfs_state_validate(
    const struct micros_vfs_state *state
)
{
    if (state == NULL) {
        return MICROS_VFS_CORE_ERROR_ARGUMENT;
    }
    if (
        state->initialization_magic
            != MICROS_VFS_INITIALIZATION_MAGIC
        || (
            state->phase != MICROS_VFS_PHASE_READY_UNMOUNTED
            && state->phase != MICROS_VFS_PHASE_MOUNTED
        )
        || !endpoint_is_canonical(state->self_endpoint)
        || !endpoint_is_canonical(state->ramfs_endpoint)
        || !endpoint_is_canonical(state->tty_endpoint)
        || state->self_endpoint == state->ramfs_endpoint
        || state->self_endpoint == state->tty_endpoint
        || state->ramfs_endpoint == state->tty_endpoint
        || state->reserved != 0
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (state->phase == MICROS_VFS_PHASE_READY_UNMOUNTED) {
        if (
            !bytes_are_zero(
                state->processes,
                sizeof(state->processes)
            )
            || !bytes_are_zero(
                state->open_files,
                sizeof(state->open_files)
            )
            || !bytes_are_zero(
                state->vnodes,
                sizeof(state->vnodes)
            )
            || !bytes_are_zero(
                &state->pending,
                sizeof(state->pending)
            )
            || state->client_page_owner != MICROS_VFS_PAGE_FREE
            || state->backend_page_owner != MICROS_VFS_PAGE_FREE
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        return MICROS_VFS_CORE_OK;
    }
    if (
        validate_processes(state) != MICROS_VFS_CORE_OK
        || validate_open_files(state) != MICROS_VFS_CORE_OK
        || validate_vnodes(state) != MICROS_VFS_CORE_OK
        || validate_pending(state) != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
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
        || !mode_is_directory(response.mode)
        || !mode_is_canonical(response.mode)
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
        || !endpoint_is_canonical(endpoint)
        || endpoint == state->self_endpoint
        || endpoint == state->ramfs_endpoint
        || endpoint == state->tty_endpoint
        || root_vnode >= MICROS_VFS_VNODE_CAPACITY
        || micros_vfs_state_validate(state) != MICROS_VFS_CORE_OK
        || state->phase != MICROS_VFS_PHASE_MOUNTED
    ) {
        return MICROS_VFS_CORE_ERROR_ARGUMENT;
    }
    if (find_process(state, endpoint, &process_slot)) {
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
        !find_free_process(state, &process_slot)
        || !find_free_open_files(state, 3, open_file_slots)
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
        || !endpoint_is_canonical(source_endpoint)
        || !endpoint_is_canonical(destination_endpoint)
        || source_descriptor >= MICROS_VFS_DESCRIPTOR_CAPACITY
        || destination_descriptor >= MICROS_VFS_DESCRIPTOR_CAPACITY
        || micros_vfs_state_validate(state) != MICROS_VFS_CORE_OK
        || state->phase != MICROS_VFS_PHASE_MOUNTED
    ) {
        return MICROS_VFS_CORE_ERROR_ARGUMENT;
    }
    if (
        !find_process(
            state,
            source_endpoint,
            &source_process_slot
        )
        || !find_process(
            state,
            destination_endpoint,
            &destination_process_slot
        )
    ) {
        *result = MICROS_VFS_TRUSTED_NOT_FOUND;
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
        || !endpoint_is_canonical(endpoint)
        || micros_vfs_state_validate(state) != MICROS_VFS_CORE_OK
        || state->phase != MICROS_VFS_PHASE_MOUNTED
    ) {
        return MICROS_VFS_CORE_ERROR_ARGUMENT;
    }
    if (!find_process(state, endpoint, &process_slot)) {
        *result = MICROS_VFS_TRUSTED_NOT_FOUND;
        return MICROS_VFS_CORE_OK;
    }
    if (
        state->pending.state
            == MICROS_VFS_PENDING_TTY_COMPLETION_NOTICE_DEBT
        || state->pending.state
            == MICROS_VFS_PENDING_TTY_WRITABLE_NOTICE_DEBT
        || state->pending.state
            == MICROS_VFS_PENDING_TTY_TEST_DRAIN_WAIT_WRITABLE
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
            release_open_file_reference(
                state,
                reference - 1,
                io
            ) != MICROS_VFS_CORE_OK
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
    }
    if (
        release_vnode_reference(
            state,
            working_directory_vnode,
            io
        ) != MICROS_VFS_CORE_OK
        || release_vnode_reference(
            state,
            root_vnode,
            io
        ) != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    zero_bytes(process, sizeof(*process));
    if (micros_vfs_state_validate(state) != MICROS_VFS_CORE_OK) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    *result = debt
        ? MICROS_VFS_TRUSTED_PENDING
        : MICROS_VFS_TRUSTED_OK;
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
    zero_bytes(action, sizeof(*action));
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
        !endpoint_is_canonical(request->source)
        || !find_process(
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
    zero_bytes(action, sizeof(*action));
    if (
        events
            == (
                MICROS_TTY_EVENT_COMPLETION
                | MICROS_TTY_EVENT_WRITABLE
            )
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
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
        || !endpoint_is_canonical(endpoint)
        || micros_vfs_state_validate(state)
            != MICROS_VFS_CORE_OK
        || !find_process(state, endpoint, &process_slot)
    ) {
        return MICROS_VFS_CORE_ERROR_ARGUMENT;
    }
    (void)process_slot;
    zero_bytes(action, sizeof(*action));
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

#include "servers/vfs/vfs_internal.h"

#include "lib/spawn/spawn_request.h"

#define MICROS_VFS_INITIALIZATION_MAGIC \
    UINT64_C(0x4d4943524f535646)

void vfs_zero_bytes(void *storage, size_t size)
{
    uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

bool vfs_bytes_are_zero(const void *storage, size_t size)
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

bool vfs_endpoint_is_canonical(micros_endpoint_t endpoint)
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

bool vfs_ramfs_handle_is_canonical(uint64_t node)
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

bool vfs_mode_is_canonical(uint32_t mode)
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

bool vfs_mode_is_directory(uint32_t mode)
{
    return (
        mode & MICROS_RAMFS_MODE_TYPE_MASK
    ) == MICROS_RAMFS_MODE_DIRECTORY;
}

bool vfs_reference_is_valid(
    uint16_t reference,
    size_t capacity
)
{
    return reference != 0 && (size_t)(reference - 1) < capacity;
}

uint64_t vfs_tty_request_id_successor(uint64_t request_id)
{
    return request_id == UINT64_MAX ? 0 : request_id + 1;
}

bool vfs_ramfs_response_fields_are_zero(
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

void vfs_prepare_result(
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

void vfs_prepare_control_result(
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

static bool free_process_is_canonical(
    const struct micros_vfs_process_record *process
)
{
    return vfs_bytes_are_zero(process, sizeof(*process));
}

static bool free_open_file_is_canonical(
    const struct micros_vfs_open_file *open_file
)
{
    return vfs_bytes_are_zero(open_file, sizeof(*open_file));
}

static bool free_vnode_is_canonical(
    const struct micros_vfs_vnode *vnode
)
{
    return vfs_bytes_are_zero(vnode, sizeof(*vnode));
}

static bool pending_debt_is_canonical(
    const struct micros_vfs_pending_operation *pending
)
{
    struct micros_vfs_pending_operation expected = {
        .state = pending->state,
    };

    return vfs_bytes_are_zero(
        (const uint8_t *)pending + sizeof(pending->state),
        sizeof(*pending) - sizeof(pending->state)
    ) && expected.state == pending->state;
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

        if (process->state == MICROS_VFS_PROCESS_FREE) {
            if (!free_process_is_canonical(process)) {
                return MICROS_VFS_CORE_ERROR_INVARIANT;
            }
            continue;
        }
        if (
            process->state != MICROS_VFS_PROCESS_ACTIVE
            || !vfs_endpoint_is_canonical(process->endpoint)
            || process->endpoint == state->self_endpoint
            || process->endpoint == state->vm_endpoint
            || process->endpoint == state->pm_endpoint
            || process->endpoint == state->ramfs_endpoint
            || process->endpoint == state->tty_endpoint
            || !vfs_reference_is_valid(
                process->root_vnode,
                MICROS_VFS_VNODE_CAPACITY
            )
            || !vfs_reference_is_valid(
                process->working_directory_vnode,
                MICROS_VFS_VNODE_CAPACITY
            )
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        if (
            state->vnodes[process->root_vnode - 1].state
                != MICROS_VFS_VNODE_ACTIVE
            || !vfs_mode_is_directory(
                state->vnodes[process->root_vnode - 1].mode
            )
            || state->vnodes[
                process->working_directory_vnode - 1
            ].state != MICROS_VFS_VNODE_ACTIVE
            || !vfs_mode_is_directory(
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
    }
    return MICROS_VFS_CORE_OK;
}

static enum micros_vfs_core_error validate_open_file_shape(
    const struct micros_vfs_state *state,
    size_t open_file_slot
)
{
    const struct micros_vfs_open_file *open_file =
        &state->open_files[open_file_slot];
    const struct micros_vfs_vnode *vnode;

    if (open_file->state == MICROS_VFS_OPEN_FILE_FREE) {
        return free_open_file_is_canonical(open_file)
            ? MICROS_VFS_CORE_OK
            : MICROS_VFS_CORE_ERROR_INVARIANT;
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
        || open_file->reserved != 0
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (open_file->kind == MICROS_VFS_OBJECT_CONSOLE) {
        return (
            open_file->vnode == 0
            && open_file->open_flags == open_file->access
            && open_file->position == 0
        )
            ? MICROS_VFS_CORE_OK
            : MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (
        !vfs_reference_is_valid(
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
            vfs_mode_is_directory(vnode->mode)
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
            !vfs_mode_is_directory(vnode->mode)
            && (
                open_file->open_flags
                & MICROS_VFS_OPEN_DIRECTORY
            ) != 0
        )
        || (
            vfs_mode_is_directory(vnode->mode)
            && open_file->position
                > MICROS_RAMFS_DIRECTORY_CURSOR_END
        )
        || (
            !vfs_mode_is_directory(vnode->mode)
            && open_file->position
                > MICROS_RAMFS_FILE_SIZE_MAX
        )
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    return MICROS_VFS_CORE_OK;
}

static enum micros_vfs_core_error validate_descriptor_references(
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
        size_t descriptor;

        if (process->state != MICROS_VFS_PROCESS_ACTIVE) {
            continue;
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
                    !vfs_reference_is_valid(
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

static enum micros_vfs_core_error validate_reference_counts(
    const struct micros_vfs_state *state
)
{
    size_t slot;

    for (
        slot = 0;
        slot < MICROS_VFS_OPEN_FILE_CAPACITY;
        ++slot
    ) {
        const struct micros_vfs_open_file *open_file =
            &state->open_files[slot];

        if (
            open_file->state == MICROS_VFS_OPEN_FILE_ACTIVE
            && (
                open_file->reference_count == 0
                || vfs_descriptor_user_count(state, slot)
                    != open_file->reference_count
            )
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
    }
    for (
        slot = 0;
        slot < MICROS_VFS_VNODE_CAPACITY;
        ++slot
    ) {
        const struct micros_vfs_vnode *vnode =
            &state->vnodes[slot];

        if (
            vnode->state == MICROS_VFS_VNODE_ACTIVE
            && (
                vnode->local_reference_count == 0
                || vnode->local_reference_count
                    != vfs_vnode_user_count(state, slot)
            )
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
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
        if (
            validate_open_file_shape(state, open_file_slot)
                != MICROS_VFS_CORE_OK
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
    }
    if (
        validate_descriptor_references(state)
            != MICROS_VFS_CORE_OK
        || validate_reference_counts(state)
            != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
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
            || !vfs_bytes_are_zero(
                vnode->reserved0,
                sizeof(vnode->reserved0)
            )
            || !vfs_ramfs_handle_is_canonical(vnode->node)
            || (
                vnode_slot == 0
                && vnode->node != MICROS_VFS_RAMFS_ROOT_NODE
            )
            || !vfs_mode_is_canonical(vnode->mode)
            || vnode->backend_reference_count == 0
            || vnode->backend_reference_count
                >= MICROS_VFS_BACKEND_REFERENCE_THRESHOLD
            || vnode->reserved1 != 0
            || (
                vfs_mode_is_directory(vnode->mode)
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

static bool pending_has_process_owner(uint32_t pending_state)
{
    return (
        pending_state == MICROS_VFS_PENDING_TTY_READ_WAIT_COMPLETION
        || pending_state
            == MICROS_VFS_PENDING_TTY_WRITE_WAIT_WRITABLE
        || pending_state
            == MICROS_VFS_PENDING_TTY_WRITE_WAIT_COMPLETION
    );
}

static enum micros_vfs_core_error validate_pending_shape(
    const struct micros_vfs_pending_operation *pending
)
{
    if (pending->state == MICROS_VFS_PENDING_NONE) {
        return vfs_bytes_are_zero(pending, sizeof(*pending))
            ? MICROS_VFS_CORE_OK
            : MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (
        pending->state
            == MICROS_VFS_PENDING_TTY_COMPLETION_NOTICE_DEBT
        || pending->state
            == MICROS_VFS_PENDING_TTY_WRITABLE_NOTICE_DEBT
    ) {
        return MICROS_VFS_CORE_OK;
    }
#if defined(MICROS_BUILD_VFS_SERVICE_TEST) || defined(MICROS_BUILD_ELF_SPAWN_TEST)
    if (
        pending->state
            == MICROS_VFS_PENDING_TTY_TEST_DRAIN_WAIT_WRITABLE
    ) {
        return (
            pending->reply_token != 0
            && pending->request_id != 0
            && pending->application_grant == MICROS_GRANT_NONE
            && pending->tty_grant == MICROS_GRANT_NONE
            && pending->process_slot == 0
            && pending->descriptor == 0
            && pending->open_file == 0
            && pending->reserved == 0
            && pending->endpoint == 0
            && pending->application_offset == 0
            && pending->count == 0
        )
            ? MICROS_VFS_CORE_OK
            : MICROS_VFS_CORE_ERROR_INVARIANT;
    }
#endif
    if (
        !pending_has_process_owner(pending->state)
        || pending->process_slot >= MICROS_VFS_PROCESS_CAPACITY
        || pending->descriptor >= MICROS_VFS_DESCRIPTOR_CAPACITY
        || pending->open_file >= MICROS_VFS_OPEN_FILE_CAPACITY
        || pending->reserved != 0
        || !vfs_endpoint_is_canonical(pending->endpoint)
        || pending->reply_token == 0
        || pending->count == 0
        || pending->count > MICROS_VFS_TRANSFER_MAX
        || pending->request_id == 0
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    return MICROS_VFS_CORE_OK;
}

static enum micros_vfs_core_error validate_pending_owner(
    const struct micros_vfs_state *state
)
{
    const struct micros_vfs_pending_operation *pending =
        &state->pending;
    const struct micros_vfs_process_record *process;
    const struct micros_vfs_open_file *open_file;

    if (!pending_has_process_owner(pending->state)) {
        return MICROS_VFS_CORE_OK;
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
    return MICROS_VFS_CORE_OK;
}

static enum micros_vfs_core_error validate_pending_resources(
    const struct micros_vfs_state *state
)
{
    const struct micros_vfs_pending_operation *pending =
        &state->pending;
    const struct micros_vfs_open_file *open_file;

    if (state->client_page_owner != MICROS_VFS_PAGE_FREE) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (pending->state == MICROS_VFS_PENDING_NONE) {
        return state->backend_page_owner == MICROS_VFS_PAGE_FREE
            ? MICROS_VFS_CORE_OK
            : MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (
        pending->state
            == MICROS_VFS_PENDING_TTY_COMPLETION_NOTICE_DEBT
        || pending->state
            == MICROS_VFS_PENDING_TTY_WRITABLE_NOTICE_DEBT
    ) {
        return (
            pending_debt_is_canonical(pending)
            && state->backend_page_owner == MICROS_VFS_PAGE_FREE
        )
            ? MICROS_VFS_CORE_OK
            : MICROS_VFS_CORE_ERROR_INVARIANT;
    }
#if defined(MICROS_BUILD_VFS_SERVICE_TEST) || defined(MICROS_BUILD_ELF_SPAWN_TEST)
    if (
        pending->state
            == MICROS_VFS_PENDING_TTY_TEST_DRAIN_WAIT_WRITABLE
    ) {
        return (
            state->next_tty_request_id == pending->request_id
            && state->backend_page_owner == MICROS_VFS_PAGE_FREE
        )
            ? MICROS_VFS_CORE_OK
            : MICROS_VFS_CORE_ERROR_INVARIANT;
    }
#endif
    open_file = &state->open_files[pending->open_file];
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
                != vfs_tty_request_id_successor(
                    pending->request_id
                )
            || state->backend_page_owner
                != MICROS_VFS_PAGE_TTY_READ
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        return MICROS_VFS_CORE_OK;
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
        return MICROS_VFS_CORE_OK;
    case MICROS_VFS_PENDING_TTY_WRITE_WAIT_COMPLETION:
        if (
            pending->application_grant != MICROS_GRANT_NONE
            || pending->tty_grant != MICROS_GRANT_NONE
            || (
                open_file->access & MICROS_VFS_ACCESS_WRITE
            ) == 0
            || pending->application_offset != 0
            || state->next_tty_request_id
                != vfs_tty_request_id_successor(
                    pending->request_id
                )
            || state->backend_page_owner != MICROS_VFS_PAGE_FREE
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        return MICROS_VFS_CORE_OK;
    default:
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
}

static enum micros_vfs_core_error validate_pending(
    const struct micros_vfs_state *state
)
{
    if (
        validate_pending_shape(&state->pending)
            != MICROS_VFS_CORE_OK
        || validate_pending_owner(state) != MICROS_VFS_CORE_OK
        || validate_pending_resources(state) != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    return MICROS_VFS_CORE_OK;
}

static bool spawn_descriptor_metadata_is_zero(
    const struct micros_vfs_spawn_transaction *spawn
)
{
    return (
        spawn->child_process_reference == 0
        && spawn->descriptor_token == 0
        && spawn->child_root_vnode == 0
        && spawn->child_working_directory_vnode == 0
        && vfs_bytes_are_zero(
            spawn->child_descriptors,
            sizeof(spawn->child_descriptors)
        )
        && spawn->child_endpoint == 0
    );
}

static enum micros_vfs_core_error
validate_trusted_spawn_descriptors(
    const struct micros_vfs_state *state,
    const struct micros_vfs_spawn_transaction *spawn
)
{
    size_t descriptor;

    if (
        spawn->child_root_vnode != 1
        || spawn->child_working_directory_vnode != 1
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    for (
        descriptor = 0;
        descriptor < MICROS_VFS_DESCRIPTOR_CAPACITY;
        ++descriptor
    ) {
        uint16_t reference =
            spawn->child_descriptors[descriptor];
        const struct micros_vfs_open_file *open_file;
        size_t prior;

        if (descriptor >= 3) {
            if (reference != 0) {
                return MICROS_VFS_CORE_ERROR_INVARIANT;
            }
            continue;
        }
        if (
            !vfs_reference_is_valid(
                reference,
                MICROS_VFS_OPEN_FILE_CAPACITY
            )
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        open_file = &state->open_files[reference - 1];
        if (
            open_file->state != MICROS_VFS_OPEN_FILE_ACTIVE
            || open_file->kind != MICROS_VFS_OBJECT_CONSOLE
            || open_file->access
                != (
                    descriptor == 0
                        ? MICROS_VFS_ACCESS_READ
                        : MICROS_VFS_ACCESS_WRITE
                )
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        for (prior = 0; prior < descriptor; ++prior) {
            if (
                spawn->child_descriptors[prior] == reference
            ) {
                return MICROS_VFS_CORE_ERROR_INVARIANT;
            }
        }
    }
    return MICROS_VFS_CORE_OK;
}

static enum micros_vfs_core_error
validate_public_spawn_descriptors(
    const struct micros_vfs_spawn_transaction *spawn,
    const struct micros_spawn_request *decoded,
    const struct micros_vfs_process_record *parent
)
{
    uint16_t expected_descriptors[
        MICROS_VFS_DESCRIPTOR_CAPACITY
    ] = {0};
    size_t descriptor;

    if (
        spawn->child_root_vnode != parent->root_vnode
        || spawn->child_working_directory_vnode
            != parent->working_directory_vnode
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    for (
        descriptor = 0;
        descriptor < decoded->descriptor_action_count;
        ++descriptor
    ) {
        const struct micros_spawn_descriptor_action *item =
            &decoded->descriptor_actions[descriptor];
        uint16_t reference = parent->descriptors[item->source];

        if (reference == 0) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        expected_descriptors[item->destination] = reference;
    }
    for (
        descriptor = 0;
        descriptor < MICROS_VFS_DESCRIPTOR_CAPACITY;
        ++descriptor
    ) {
        if (
            spawn->child_descriptors[descriptor]
                != expected_descriptors[descriptor]
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
    }
    return MICROS_VFS_CORE_OK;
}

static enum micros_vfs_core_error
validate_hidden_spawn_process(
    const struct micros_vfs_spawn_transaction *spawn,
    const struct micros_vfs_process_record *child
)
{
    size_t descriptor;

    if (
        child->root_vnode != spawn->child_root_vnode
        || child->working_directory_vnode
            != spawn->child_working_directory_vnode
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    for (
        descriptor = 0;
        descriptor < MICROS_VFS_DESCRIPTOR_CAPACITY;
        ++descriptor
    ) {
        if (
            child->descriptors[descriptor]
                != spawn->child_descriptors[descriptor]
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
    }
    return MICROS_VFS_CORE_OK;
}

static enum micros_vfs_core_error
validate_spawn_descriptor_state(
    const struct micros_vfs_state *state,
    const struct micros_spawn_request *decoded,
    const struct micros_vfs_process_record *parent,
    bool hidden_committed
)
{
    const struct micros_vfs_spawn_transaction *spawn =
        &state->spawn;
    const struct micros_vfs_process_record *child;
    enum micros_vfs_core_error error;

    if (
        spawn->descriptor_token == 0
        || spawn->descriptor_token != state->last_descriptor_token
        || !vfs_reference_is_valid(
            spawn->child_process_reference,
            MICROS_VFS_PROCESS_CAPACITY
        )
        || !vfs_reference_is_valid(
            spawn->child_root_vnode,
            MICROS_VFS_VNODE_CAPACITY
        )
        || !vfs_reference_is_valid(
            spawn->child_working_directory_vnode,
            MICROS_VFS_VNODE_CAPACITY
        )
        || !vfs_mode_is_directory(
            state->vnodes[spawn->child_root_vnode - 1].mode
        )
        || !vfs_mode_is_directory(
            state->vnodes[
                spawn->child_working_directory_vnode - 1
            ].mode
        )
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    child = &state->processes[
        spawn->child_process_reference - 1
    ];
    if (hidden_committed) {
        if (
            !vfs_endpoint_is_canonical(spawn->child_endpoint)
            || spawn->child_endpoint == state->self_endpoint
            || spawn->child_endpoint == state->vm_endpoint
            || spawn->child_endpoint == state->pm_endpoint
            || spawn->child_endpoint == state->ramfs_endpoint
            || spawn->child_endpoint == state->tty_endpoint
            || child->state != MICROS_VFS_PROCESS_ACTIVE
            || child->endpoint != spawn->child_endpoint
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
    } else if (
        spawn->child_endpoint != 0
        || !free_process_is_canonical(child)
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    error = spawn->trusted_init
        ? validate_trusted_spawn_descriptors(state, spawn)
        : validate_public_spawn_descriptors(
            spawn,
            decoded,
            parent
        );
    if (error != MICROS_VFS_CORE_OK) {
        return error;
    }
    if (hidden_committed) {
        return validate_hidden_spawn_process(spawn, child);
    }
    return MICROS_VFS_CORE_OK;
}

static enum micros_vfs_core_error validate_spawn_stage_metadata(
    const struct micros_vfs_state *state,
    struct micros_spawn_request *decoded,
    bool *descriptors_reserved,
    bool *hidden_committed
)
{
    const struct micros_vfs_spawn_transaction *spawn =
        &state->spawn;
    struct micros_spawn_stack_layout stack_layout;

    *hidden_committed =
        spawn->phase == MICROS_VFS_SPAWN_HIDDEN_COMMITTED;
    *descriptors_reserved =
        spawn->phase == MICROS_VFS_SPAWN_DESCRIPTORS_PREPARED;
    *descriptors_reserved =
        *descriptors_reserved || *hidden_committed;
    if (
        (
            spawn->phase != MICROS_VFS_SPAWN_IMAGE_PREPARED
            && !*descriptors_reserved
        )
        || !vfs_bytes_are_zero(
            spawn->reserved0,
            sizeof(spawn->reserved0)
        )
        || spawn->pm_spawn_token == 0
        || spawn->image_token == 0
        || spawn->image_token != state->last_image_token
        || spawn->semantic_pid == 0
        || spawn->request_size < MICROS_SPAWN_REQUEST_SIZE_MIN
        || spawn->request_size > MICROS_SPAWN_REQUEST_SIZE_MAX
        || spawn->executable_size == 0
        || spawn->executable_size
            > MICROS_APPLICATION_EXECUTABLE_SIZE_MAX
        || micros_spawn_request_decode(
            state->spawn_request_bytes,
            spawn->request_size,
            decoded
        ) != MICROS_SPAWN_REQUEST_OK
        || micros_spawn_stack_layout_plan(
            decoded,
            &stack_layout
        ) != MICROS_SPAWN_STACK_LAYOUT_OK
        || (
            !*descriptors_reserved
            && !spawn_descriptor_metadata_is_zero(spawn)
        )
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    return MICROS_VFS_CORE_OK;
}

static enum micros_vfs_core_error validate_spawn_image_ownership(
    const struct micros_vfs_state *state,
    const struct micros_spawn_request *decoded,
    const struct micros_vfs_process_record **parent
)
{
    const struct micros_vfs_spawn_transaction *spawn =
        &state->spawn;
    const struct micros_vfs_vnode *vnode;
    size_t process_slot;

    if (
        !vfs_reference_is_valid(
            spawn->executable_vnode,
            MICROS_VFS_VNODE_CAPACITY
        )
        || spawn->executable_grant == 0
        || spawn->executable_grant == MICROS_GRANT_NONE
        || spawn->request_grant == 0
        || spawn->request_grant == MICROS_GRANT_NONE
        || spawn->request_grant == spawn->executable_grant
        || (
            state->pending.tty_grant != 0
            && state->pending.tty_grant != MICROS_GRANT_NONE
            && (
                spawn->executable_grant
                    == state->pending.tty_grant
                || spawn->request_grant
                    == state->pending.tty_grant
            )
        )
        || !vfs_bytes_are_zero(
            &state->spawn_request_bytes[spawn->request_size],
            sizeof(state->spawn_request_bytes)
                - spawn->request_size
        )
        || !vfs_bytes_are_zero(
            &state->spawn_executable_bytes[
                spawn->executable_size
            ],
            sizeof(state->spawn_executable_bytes)
                - spawn->executable_size
        )
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    vnode = &state->vnodes[spawn->executable_vnode - 1];
    if (
        vnode->state != MICROS_VFS_VNODE_ACTIVE
        || vfs_mode_is_directory(vnode->mode)
        || (
            vnode->mode & MICROS_RAMFS_MODE_TYPE_MASK
        ) != MICROS_RAMFS_MODE_REGULAR
        || (vnode->mode & UINT32_C(0111)) == 0
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (spawn->trusted_init) {
        if (
            spawn->parent_endpoint != 0
            || state->spawn_request_bytes[decoded->path.offset]
                != '/'
            || decoded->descriptor_action_count != 0
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
    } else {
        if (!vfs_endpoint_is_canonical(spawn->parent_endpoint)) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        if (
            !vfs_find_process(
                state,
                spawn->parent_endpoint,
                &process_slot
            )
        ) {
            return MICROS_VFS_CORE_ERROR_INVARIANT;
        }
        *parent = &state->processes[process_slot];
    }
    return MICROS_VFS_CORE_OK;
}

static enum micros_vfs_core_error validate_spawn(
    const struct micros_vfs_state *state
)
{
    const struct micros_vfs_spawn_transaction *spawn =
        &state->spawn;
    const struct micros_vfs_process_record *parent = NULL;
    struct micros_spawn_request decoded;
    bool descriptors_reserved;
    bool hidden_committed;

    if (spawn->phase == MICROS_VFS_SPAWN_IDLE) {
        return (
            vfs_bytes_are_zero(spawn, sizeof(*spawn))
            && vfs_bytes_are_zero(
                state->spawn_request_bytes,
                sizeof(state->spawn_request_bytes)
            )
            && vfs_bytes_are_zero(
                state->spawn_executable_bytes,
                sizeof(state->spawn_executable_bytes)
            )
        )
            ? MICROS_VFS_CORE_OK
            : MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (
        validate_spawn_stage_metadata(
            state,
            &decoded,
            &descriptors_reserved,
            &hidden_committed
        ) != MICROS_VFS_CORE_OK
        || validate_spawn_image_ownership(
            state,
            &decoded,
            &parent
        ) != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (!descriptors_reserved) {
        return MICROS_VFS_CORE_OK;
    }
    return validate_spawn_descriptor_state(
        state,
        &decoded,
        parent,
        hidden_committed
    );
}

enum micros_vfs_core_error micros_vfs_state_initialize(
    struct micros_vfs_state *state,
    micros_endpoint_t self_endpoint,
    micros_endpoint_t vm_endpoint,
    micros_endpoint_t pm_endpoint,
    micros_endpoint_t ramfs_endpoint,
    micros_endpoint_t tty_endpoint
)
{
    if (
        state == NULL
        || !vfs_endpoint_is_canonical(self_endpoint)
        || !vfs_endpoint_is_canonical(vm_endpoint)
        || !vfs_endpoint_is_canonical(pm_endpoint)
        || !vfs_endpoint_is_canonical(ramfs_endpoint)
        || !vfs_endpoint_is_canonical(tty_endpoint)
        || self_endpoint == vm_endpoint
        || self_endpoint == pm_endpoint
        || self_endpoint == ramfs_endpoint
        || self_endpoint == tty_endpoint
        || vm_endpoint == pm_endpoint
        || vm_endpoint == ramfs_endpoint
        || vm_endpoint == tty_endpoint
        || pm_endpoint == ramfs_endpoint
        || pm_endpoint == tty_endpoint
        || ramfs_endpoint == tty_endpoint
    ) {
        return MICROS_VFS_CORE_ERROR_ARGUMENT;
    }
    vfs_zero_bytes(state, sizeof(*state));
    state->initialization_magic = MICROS_VFS_INITIALIZATION_MAGIC;
    state->phase = MICROS_VFS_PHASE_READY_UNMOUNTED;
    state->self_endpoint = self_endpoint;
    state->vm_endpoint = vm_endpoint;
    state->pm_endpoint = pm_endpoint;
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
        || !vfs_endpoint_is_canonical(state->self_endpoint)
        || !vfs_endpoint_is_canonical(state->vm_endpoint)
        || !vfs_endpoint_is_canonical(state->pm_endpoint)
        || !vfs_endpoint_is_canonical(state->ramfs_endpoint)
        || !vfs_endpoint_is_canonical(state->tty_endpoint)
        || state->self_endpoint == state->vm_endpoint
        || state->self_endpoint == state->pm_endpoint
        || state->self_endpoint == state->ramfs_endpoint
        || state->self_endpoint == state->tty_endpoint
        || state->vm_endpoint == state->pm_endpoint
        || state->vm_endpoint == state->ramfs_endpoint
        || state->vm_endpoint == state->tty_endpoint
        || state->pm_endpoint == state->ramfs_endpoint
        || state->pm_endpoint == state->tty_endpoint
        || state->ramfs_endpoint == state->tty_endpoint
        || state->reserved != 0
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    if (state->phase == MICROS_VFS_PHASE_READY_UNMOUNTED) {
        if (
            !vfs_bytes_are_zero(
                state->processes,
                sizeof(state->processes)
            )
            || !vfs_bytes_are_zero(
                state->open_files,
                sizeof(state->open_files)
            )
            || !vfs_bytes_are_zero(
                state->vnodes,
                sizeof(state->vnodes)
            )
            || !vfs_bytes_are_zero(
                &state->pending,
                sizeof(state->pending)
            )
            || state->last_image_token != 0
            || state->last_descriptor_token != 0
            || !vfs_bytes_are_zero(
                &state->spawn,
                sizeof(state->spawn)
            )
            || state->client_page_owner != MICROS_VFS_PAGE_FREE
            || state->backend_page_owner != MICROS_VFS_PAGE_FREE
            || !vfs_bytes_are_zero(
                state->spawn_request_bytes,
                sizeof(state->spawn_request_bytes)
            )
            || !vfs_bytes_are_zero(
                state->spawn_executable_bytes,
                sizeof(state->spawn_executable_bytes)
            )
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
        || validate_spawn(state) != MICROS_VFS_CORE_OK
    ) {
        return MICROS_VFS_CORE_ERROR_INVARIANT;
    }
    return MICROS_VFS_CORE_OK;
}

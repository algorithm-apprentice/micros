#ifndef MICROS_SERVERS_VFS_INTERNAL_H
#define MICROS_SERVERS_VFS_INTERNAL_H

#include "servers/vfs/vfs_core.h"

#define MICROS_VFS_RAMFS_ROOT_NODE \
    UINT64_C(0x0000000100000000)

static inline void vfs_zero_bytes(void *storage, size_t size)
{
    uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static inline bool vfs_bytes_are_zero(
    const void *storage,
    size_t size
)
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

static inline bool vfs_endpoint_is_canonical(
    micros_endpoint_t endpoint
)
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

static inline bool vfs_ramfs_handle_is_canonical(uint64_t node)
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

static inline bool vfs_mode_is_canonical(uint32_t mode)
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

static inline bool vfs_mode_is_directory(uint32_t mode)
{
    return (
        mode & MICROS_RAMFS_MODE_TYPE_MASK
    ) == MICROS_RAMFS_MODE_DIRECTORY;
}

static inline bool vfs_reference_is_valid(
    uint16_t reference,
    size_t capacity
)
{
    return reference != 0 && (size_t)(reference - 1) < capacity;
}

static inline uint64_t vfs_tty_request_id_successor(
    uint64_t request_id
)
{
    return request_id == UINT64_MAX ? 0 : request_id + 1;
}

static inline bool vfs_ramfs_response_fields_are_zero(
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

static inline void vfs_prepare_result(
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

static inline void vfs_prepare_control_result(
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

size_t vfs_descriptor_user_count(
    const struct micros_vfs_state *state,
    size_t open_file_slot
);

size_t vfs_vnode_user_count(
    const struct micros_vfs_state *state,
    size_t vnode_slot
);

bool vfs_find_process(
    const struct micros_vfs_state *state,
    micros_endpoint_t endpoint,
    size_t *process_slot
);

bool vfs_find_free_process(
    const struct micros_vfs_state *state,
    size_t *process_slot
);

bool vfs_find_free_open_files(
    const struct micros_vfs_state *state,
    size_t required,
    size_t *slots
);

enum micros_vfs_core_error vfs_putnode(
    const struct micros_vfs_io *io,
    uint64_t node,
    uint32_t count
);

enum micros_vfs_core_error vfs_release_vnode_reference(
    struct micros_vfs_state *state,
    size_t vnode_slot,
    const struct micros_vfs_io *io
);

enum micros_vfs_core_error vfs_release_open_file_reference(
    struct micros_vfs_state *state,
    size_t open_file_slot,
    const struct micros_vfs_io *io
);

bool vfs_find_free_descriptor(
    const struct micros_vfs_process_record *process,
    size_t *descriptor
);

bool vfs_find_free_vnode(
    const struct micros_vfs_state *state,
    size_t *vnode_slot
);

size_t vfs_free_vnode_count(
    const struct micros_vfs_state *state
);

bool vfs_find_vnode_by_node(
    const struct micros_vfs_state *state,
    uint64_t node,
    size_t *vnode_slot
);

enum micros_vfs_result vfs_resolve_descriptor(
    struct micros_vfs_state *state,
    size_t process_slot,
    uint32_t descriptor,
    size_t *open_file_slot,
    struct micros_vfs_open_file **open_file
);

enum micros_vfs_core_error vfs_create_backend_grant(
    struct micros_vfs_state *state,
    const struct micros_vfs_io *io,
    micros_endpoint_t endpoint,
    uint8_t *local,
    size_t length,
    uint32_t permission,
    micros_grant_t *grant,
    enum micros_vfs_result *result
);

enum micros_vfs_core_error vfs_revoke_backend_grant(
    const struct micros_vfs_io *io,
    micros_grant_t grant
);

enum micros_vfs_core_error vfs_copy_application_path(
    struct micros_vfs_state *state,
    const struct micros_vfs_request *request,
    const struct micros_vfs_io *io,
    enum micros_vfs_result *result
);

enum micros_vfs_core_error vfs_validate_application_range(
    const struct micros_vfs_request *request,
    const struct micros_vfs_io *io,
    uint32_t permission,
    enum micros_vfs_result *result
);

bool vfs_namespace_response_node_is_valid(
    const struct micros_vfs_ramfs_response *response
);

enum micros_vfs_core_error vfs_namespace_integrate_vnode(
    struct micros_vfs_state *state,
    const struct micros_vfs_ramfs_response *response,
    size_t reserved_vnode_slot,
    const struct micros_vfs_io *io,
    size_t *vnode_slot
);

enum micros_vfs_core_error vfs_namespace_handle_open(
    struct micros_vfs_state *state,
    size_t process_slot,
    const struct micros_vfs_request *request,
    const struct micros_vfs_io *io,
    struct micros_vfs_result_action *action
);

enum micros_vfs_core_error vfs_namespace_handle_mkdir(
    struct micros_vfs_state *state,
    size_t process_slot,
    const struct micros_vfs_request *request,
    const struct micros_vfs_io *io,
    struct micros_vfs_result_action *action
);

enum micros_vfs_core_error vfs_namespace_handle_chdir(
    struct micros_vfs_state *state,
    size_t process_slot,
    const struct micros_vfs_request *request,
    const struct micros_vfs_io *io,
    struct micros_vfs_result_action *action
);

enum micros_vfs_core_error vfs_namespace_mount(
    struct micros_vfs_state *state,
    const struct micros_vfs_io *io
);

enum micros_vfs_core_error vfs_io_handle_close(
    struct micros_vfs_state *state,
    size_t process_slot,
    const struct micros_vfs_request *request,
    const struct micros_vfs_io *io,
    struct micros_vfs_result_action *action
);

enum micros_vfs_core_error vfs_io_handle_regular_read(
    struct micros_vfs_state *state,
    struct micros_vfs_open_file *open_file,
    const struct micros_vfs_request *request,
    const struct micros_vfs_io *io,
    struct micros_vfs_result_action *action
);

enum micros_vfs_core_error vfs_io_handle_regular_write(
    struct micros_vfs_state *state,
    struct micros_vfs_open_file *open_file,
    const struct micros_vfs_request *request,
    const struct micros_vfs_io *io,
    struct micros_vfs_result_action *action
);

enum micros_vfs_core_error vfs_io_handle_getdents(
    struct micros_vfs_state *state,
    struct micros_vfs_open_file *open_file,
    const struct micros_vfs_request *request,
    const struct micros_vfs_io *io,
    struct micros_vfs_result_action *action
);

enum micros_vfs_core_error vfs_io_handle_transfer(
    struct micros_vfs_state *state,
    size_t process_slot,
    const struct micros_vfs_request *request,
    const struct micros_vfs_io *io,
    struct micros_vfs_result_action *action
);

enum micros_vfs_core_error vfs_console_handle_read(
    struct micros_vfs_state *state,
    size_t process_slot,
    size_t open_file_slot,
    const struct micros_vfs_request *request,
    const struct micros_vfs_io *io,
    enum micros_vfs_result *immediate_result
);

enum micros_vfs_core_error vfs_console_handle_write(
    struct micros_vfs_state *state,
    size_t process_slot,
    size_t open_file_slot,
    const struct micros_vfs_request *request,
    const struct micros_vfs_io *io,
    enum micros_vfs_result *immediate_result
);

enum micros_vfs_core_error vfs_console_cleanup_pending_operation(
    struct micros_vfs_state *state,
    micros_endpoint_t endpoint,
    const struct micros_vfs_io *io,
    bool *debt
);

enum micros_vfs_core_error vfs_console_handle_notification(
    struct micros_vfs_state *state,
    uint64_t events,
    const struct micros_vfs_io *io,
    struct micros_vfs_result_action *action
);

#if defined(MICROS_BUILD_VFS_SERVICE_TEST)
enum micros_vfs_core_error vfs_console_begin_test_drain(
    struct micros_vfs_state *state,
    uint64_t reply_token,
    const struct micros_vfs_io *io,
    struct micros_vfs_result_action *action
);
#endif

enum micros_vfs_core_error vfs_process_attach_console(
    struct micros_vfs_state *state,
    micros_endpoint_t endpoint,
    uint16_t root_vnode,
    enum micros_vfs_trusted_result *result
);

enum micros_vfs_core_error vfs_process_share_descriptor(
    struct micros_vfs_state *state,
    micros_endpoint_t source_endpoint,
    uint32_t source_descriptor,
    micros_endpoint_t destination_endpoint,
    uint32_t destination_descriptor,
    enum micros_vfs_trusted_result *result
);

enum micros_vfs_core_error vfs_process_detach(
    struct micros_vfs_state *state,
    micros_endpoint_t endpoint,
    const struct micros_vfs_io *io,
    enum micros_vfs_trusted_result *result
);

enum micros_vfs_core_error vfs_spawn_prepare_image(
    struct micros_vfs_state *state,
    const struct micros_vfs_control_request *request,
    const struct micros_vfs_io *io,
    struct micros_vfs_control_result_action *action
);

enum micros_vfs_core_error vfs_spawn_release_image(
    struct micros_vfs_state *state,
    const struct micros_vfs_control_request *request,
    const struct micros_vfs_io *io,
    struct micros_vfs_control_result_action *action
);

enum micros_vfs_core_error vfs_spawn_prepare_descriptors(
    struct micros_vfs_state *state,
    const struct micros_vfs_control_request *request,
    struct micros_vfs_control_result_action *action
);

enum micros_vfs_core_error vfs_spawn_abort_descriptors(
    struct micros_vfs_state *state,
    const struct micros_vfs_control_request *request,
    const struct micros_vfs_io *io,
    struct micros_vfs_control_result_action *action
);

enum micros_vfs_core_error vfs_spawn_commit_hidden(
    struct micros_vfs_state *state,
    const struct micros_vfs_control_request *request,
    struct micros_vfs_control_result_action *action
);

enum micros_vfs_core_error vfs_spawn_revert_hidden(
    struct micros_vfs_state *state,
    const struct micros_vfs_control_request *request,
    struct micros_vfs_control_result_action *action
);

#endif

#ifndef MICROS_SERVERS_VFS_CORE_H
#define MICROS_SERVERS_VFS_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/endpoint.h"
#include "micros/grant.h"
#include "micros/kernel_objects.h"
#include "micros/ramfs.h"
#include "micros/tty.h"
#include "micros/vfs.h"

enum {
    MICROS_VFS_PROCESS_CAPACITY = MICROS_PROCESS_CAPACITY,
    MICROS_VFS_DESCRIPTOR_CAPACITY = 16,
    MICROS_VFS_OPEN_FILE_CAPACITY =
        MICROS_VFS_PROCESS_CAPACITY * MICROS_VFS_DESCRIPTOR_CAPACITY,
    MICROS_VFS_VNODE_CAPACITY = MICROS_RAMFS_NODE_CAPACITY,
    MICROS_VFS_BACKEND_REFERENCE_THRESHOLD = 256,
    MICROS_VFS_PENDING_OPERATION_CAPACITY = 1,
};
#if defined(MICROS_BUILD_VFS_SERVICE_TEST)
#define MICROS_VFS_TEST_MESSAGE_DRAIN UINT32_C(0x0004ff01)
#endif

enum micros_vfs_core_error {
    MICROS_VFS_CORE_OK = 0,
    MICROS_VFS_CORE_ERROR_ARGUMENT,
    MICROS_VFS_CORE_ERROR_INVARIANT,
};

enum micros_vfs_phase {
    MICROS_VFS_PHASE_UNINITIALIZED = 0,
    MICROS_VFS_PHASE_READY_UNMOUNTED,
    MICROS_VFS_PHASE_MOUNTED,
};

enum micros_vfs_process_state {
    MICROS_VFS_PROCESS_FREE = 0,
    MICROS_VFS_PROCESS_ACTIVE,
};

enum micros_vfs_open_file_state {
    MICROS_VFS_OPEN_FILE_FREE = 0,
    MICROS_VFS_OPEN_FILE_ACTIVE,
};

enum micros_vfs_vnode_state {
    MICROS_VFS_VNODE_FREE = 0,
    MICROS_VFS_VNODE_ACTIVE,
};

enum micros_vfs_object_kind {
    MICROS_VFS_OBJECT_NONE = 0,
    MICROS_VFS_OBJECT_RAMFS,
    MICROS_VFS_OBJECT_CONSOLE,
};

enum micros_vfs_pending_state {
    MICROS_VFS_PENDING_NONE = 0,
    MICROS_VFS_PENDING_TTY_READ_WAIT_COMPLETION,
    MICROS_VFS_PENDING_TTY_WRITE_WAIT_WRITABLE,
    MICROS_VFS_PENDING_TTY_WRITE_WAIT_COMPLETION,
    MICROS_VFS_PENDING_TTY_COMPLETION_NOTICE_DEBT,
    MICROS_VFS_PENDING_TTY_WRITABLE_NOTICE_DEBT,
#if defined(MICROS_BUILD_VFS_SERVICE_TEST)
    MICROS_VFS_PENDING_TTY_TEST_DRAIN_WAIT_WRITABLE,
#endif
};

enum micros_vfs_page_owner {
    MICROS_VFS_PAGE_FREE = 0,
    MICROS_VFS_PAGE_SYNCHRONOUS,
    MICROS_VFS_PAGE_TTY_READ,
    MICROS_VFS_PAGE_TTY_WRITE_RETRY,
};

enum micros_vfs_client_direction {
    MICROS_VFS_CLIENT_FROM_APPLICATION = 0,
    MICROS_VFS_CLIENT_TO_APPLICATION,
};

enum micros_vfs_client_result {
    MICROS_VFS_CLIENT_OK = 0,
    MICROS_VFS_CLIENT_REJECTED,
    MICROS_VFS_CLIENT_INVARIANT,
};

enum micros_vfs_backend_grant_result {
    MICROS_VFS_BACKEND_GRANT_OK = 0,
    MICROS_VFS_BACKEND_GRANT_CAPACITY,
    MICROS_VFS_BACKEND_GRANT_INVARIANT,
};

enum micros_vfs_backend_status {
    MICROS_VFS_BACKEND_OK = 0,
    MICROS_VFS_BACKEND_INVARIANT,
};

enum micros_vfs_ramfs_operation {
    MICROS_VFS_RAMFS_MOUNT = 1,
    MICROS_VFS_RAMFS_LOOKUP,
    MICROS_VFS_RAMFS_CREATE,
    MICROS_VFS_RAMFS_MKDIR,
    MICROS_VFS_RAMFS_READ,
    MICROS_VFS_RAMFS_WRITE,
    MICROS_VFS_RAMFS_GETDENTS,
    MICROS_VFS_RAMFS_PUTNODE,
};

enum micros_vfs_tty_operation {
    MICROS_VFS_TTY_SUBMIT_READ = 1,
    MICROS_VFS_TTY_SUBMIT_WRITE,
    MICROS_VFS_TTY_CANCEL,
    MICROS_VFS_TTY_COLLECT,
};

enum micros_vfs_trusted_result {
    MICROS_VFS_TRUSTED_OK = 0,
    MICROS_VFS_TRUSTED_NO_SPACE,
    MICROS_VFS_TRUSTED_EXISTS,
    MICROS_VFS_TRUSTED_NOT_FOUND,
    MICROS_VFS_TRUSTED_BUSY,
    MICROS_VFS_TRUSTED_PENDING,
};

struct micros_vfs_process_record {
    enum micros_vfs_process_state state;
    micros_endpoint_t endpoint;
    uint16_t root_vnode;
    uint16_t working_directory_vnode;
    uint16_t descriptors[MICROS_VFS_DESCRIPTOR_CAPACITY];
};

struct micros_vfs_open_file {
    enum micros_vfs_open_file_state state;
    enum micros_vfs_object_kind kind;
    uint32_t access;
    uint32_t open_flags;
    uint32_t reference_count;
    uint16_t vnode;
    uint16_t reserved;
    uint64_t position;
};

struct micros_vfs_vnode {
    enum micros_vfs_vnode_state state;
    bool mount_root;
    uint8_t reserved0[3];
    uint64_t node;
    uint32_t mode;
    uint32_t local_reference_count;
    uint32_t backend_reference_count;
    uint32_t reserved1;
    uint64_t size;
};

struct micros_vfs_pending_operation {
    enum micros_vfs_pending_state state;
    uint16_t process_slot;
    uint16_t descriptor;
    uint16_t open_file;
    uint16_t reserved;
    micros_endpoint_t endpoint;
    uint64_t reply_token;
    micros_grant_t application_grant;
    micros_grant_t tty_grant;
    uint64_t application_offset;
    uint64_t count;
    uint64_t request_id;
};

struct micros_vfs_state {
    uint64_t initialization_magic;
    enum micros_vfs_phase phase;
    micros_endpoint_t self_endpoint;
    micros_endpoint_t ramfs_endpoint;
    micros_endpoint_t tty_endpoint;
    uint32_t reserved;
    uint64_t next_tty_request_id;
    enum micros_vfs_page_owner client_page_owner;
    enum micros_vfs_page_owner backend_page_owner;
    struct micros_vfs_pending_operation pending;
    struct micros_vfs_process_record
        processes[MICROS_VFS_PROCESS_CAPACITY];
    struct micros_vfs_open_file
        open_files[MICROS_VFS_OPEN_FILE_CAPACITY];
    struct micros_vfs_vnode vnodes[MICROS_VFS_VNODE_CAPACITY];
    _Alignas(4096) uint8_t client_page[MICROS_VFS_TRANSFER_MAX];
    _Alignas(4096) uint8_t backend_page[MICROS_VFS_TRANSFER_MAX];
};

struct micros_vfs_request {
    uint32_t type;
    uint32_t version;
    uint32_t flags;
    micros_endpoint_t source;
    uint64_t reply_token;
    uint32_t descriptor;
    micros_grant_t grant;
    uint64_t grant_offset;
    uint32_t count;
    uint32_t path_length;
    uint32_t open_flags;
    uint32_t mode;
};

struct micros_vfs_result_action {
    bool active;
    uint64_t reply_token;
    uint32_t request_type;
    enum micros_vfs_result result;
    uint32_t descriptor;
    uint32_t mode;
    uint64_t transferred_count;
    uint64_t position;
};

struct micros_vfs_ramfs_request {
    enum micros_vfs_ramfs_operation operation;
    uint64_t node;
    uint64_t start;
    uint64_t root;
    uint64_t file_offset;
    uint64_t cursor;
    uint64_t grant_offset;
    uint32_t mode;
    micros_grant_t grant;
    uint32_t length;
    uint32_t count;
};

struct micros_vfs_ramfs_response {
    enum micros_ramfs_result result;
    uint64_t node;
    uint64_t file_size;
    uint64_t position;
    uint32_t count;
    uint32_t mode;
};

struct micros_vfs_tty_request {
    enum micros_vfs_tty_operation operation;
    micros_grant_t grant;
    uint64_t request_id;
    uint64_t count;
};

struct micros_vfs_tty_response {
    enum micros_tty_result result;
    uint64_t request_id;
    uint64_t transferred_count;
};

typedef enum micros_vfs_client_result
(*micros_vfs_client_validate_fn)(
    void *context,
    micros_endpoint_t endpoint,
    micros_grant_t grant,
    uint64_t offset,
    size_t length,
    uint32_t permission
);

typedef enum micros_vfs_client_result
(*micros_vfs_client_copy_fn)(
    void *context,
    enum micros_vfs_client_direction direction,
    micros_endpoint_t endpoint,
    micros_grant_t grant,
    uint64_t offset,
    uint8_t *local,
    size_t length
);

typedef enum micros_vfs_backend_grant_result
(*micros_vfs_backend_grant_create_fn)(
    void *context,
    micros_endpoint_t endpoint,
    uint8_t *local,
    size_t length,
    uint32_t permission,
    micros_grant_t *grant
);

typedef enum micros_vfs_backend_status
(*micros_vfs_backend_grant_revoke_fn)(
    void *context,
    micros_grant_t grant
);

typedef enum micros_vfs_backend_status
(*micros_vfs_ramfs_call_fn)(
    void *context,
    const struct micros_vfs_ramfs_request *request,
    struct micros_vfs_ramfs_response *response
);

typedef enum micros_vfs_backend_status
(*micros_vfs_tty_call_fn)(
    void *context,
    const struct micros_vfs_tty_request *request,
    struct micros_vfs_tty_response *response
);

struct micros_vfs_io {
    micros_vfs_client_validate_fn client_validate;
    micros_vfs_client_copy_fn client_copy;
    micros_vfs_backend_grant_create_fn backend_grant_create;
    micros_vfs_backend_grant_revoke_fn backend_grant_revoke;
    micros_vfs_ramfs_call_fn ramfs_call;
    micros_vfs_tty_call_fn tty_call;
    void *context;
};

enum micros_vfs_core_error micros_vfs_state_initialize(
    struct micros_vfs_state *state,
    micros_endpoint_t self_endpoint,
    micros_endpoint_t ramfs_endpoint,
    micros_endpoint_t tty_endpoint
);

enum micros_vfs_core_error micros_vfs_state_validate(
    const struct micros_vfs_state *state
);

enum micros_vfs_core_error micros_vfs_mount(
    struct micros_vfs_state *state,
    const struct micros_vfs_io *io
);

enum micros_vfs_core_error micros_vfs_attach_console(
    struct micros_vfs_state *state,
    micros_endpoint_t endpoint,
    uint16_t root_vnode,
    enum micros_vfs_trusted_result *result
);

enum micros_vfs_core_error micros_vfs_share_descriptor(
    struct micros_vfs_state *state,
    micros_endpoint_t source_endpoint,
    uint32_t source_descriptor,
    micros_endpoint_t destination_endpoint,
    uint32_t destination_descriptor,
    enum micros_vfs_trusted_result *result
);

enum micros_vfs_core_error micros_vfs_detach(
    struct micros_vfs_state *state,
    micros_endpoint_t endpoint,
    const struct micros_vfs_io *io,
    enum micros_vfs_trusted_result *result
);

enum micros_vfs_core_error micros_vfs_handle_request(
    struct micros_vfs_state *state,
    const struct micros_vfs_request *request,
    const struct micros_vfs_io *io,
    struct micros_vfs_result_action *action
);

enum micros_vfs_core_error micros_vfs_handle_tty_notification(
    struct micros_vfs_state *state,
    uint64_t events,
    const struct micros_vfs_io *io,
    struct micros_vfs_result_action *action
);

#if defined(MICROS_BUILD_VFS_SERVICE_TEST)
enum micros_vfs_core_error micros_vfs_begin_test_drain(
    struct micros_vfs_state *state,
    micros_endpoint_t endpoint,
    uint64_t reply_token,
    const struct micros_vfs_io *io,
    struct micros_vfs_result_action *action
);
#endif

#endif

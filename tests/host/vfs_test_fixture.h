#ifndef MICROS_TESTS_HOST_VFS_TEST_FIXTURE_H
#define MICROS_TESTS_HOST_VFS_TEST_FIXTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "servers/vfs/vfs_core.h"

enum {
    VFS_TEST_APPLICATION_GRANT_CAPACITY = 32,
    VFS_TEST_NODE_DATA_CAPACITY = 8192,
};

struct vfs_test_application_grant {
    bool active;
    micros_endpoint_t endpoint;
    uint32_t permission;
    size_t length;
    uint8_t bytes[MICROS_VFS_TRANSFER_MAX];
};

struct vfs_test_backend_grant {
    bool active;
    micros_endpoint_t endpoint;
    uint8_t *local;
    size_t length;
    uint32_t permission;
};

struct vfs_test_node {
    bool live;
    uint32_t generation;
    uint32_t mode;
    uint32_t backend_references;
    uint16_t parent;
    uint16_t name_length;
    uint64_t size;
    uint8_t name[MICROS_RAMFS_NAME_MAX];
    uint8_t data[VFS_TEST_NODE_DATA_CAPACITY];
};

struct vfs_test_tty_request {
    bool active;
    uint64_t request_id;
    micros_grant_t grant;
    uint64_t count;
};

struct vfs_test_tty_completion {
    bool active;
    uint64_t request_id;
    uint64_t count;
};

struct vfs_test_fixture {
    struct micros_vfs_io io;
    micros_endpoint_t vfs_endpoint;
    micros_endpoint_t ramfs_endpoint;
    micros_endpoint_t tty_endpoint;
    struct vfs_test_application_grant
        application_grants[VFS_TEST_APPLICATION_GRANT_CAPACITY];
    struct vfs_test_backend_grant
        backend_grants[MICROS_GRANT_CAPACITY];
    size_t backend_grant_count;
    struct vfs_test_node nodes[MICROS_RAMFS_NODE_CAPACITY];
    bool mounted;
    struct vfs_test_tty_request pending_read;
    struct vfs_test_tty_completion read_completion;
    struct vfs_test_tty_completion write_completion;
    uint8_t tty_queued_input[MICROS_TTY_TRANSFER_MAX];
    size_t tty_queued_input_length;
    uint64_t tty_last_accepted_request_id;
    bool tty_output_busy;
    bool tty_writable_armed;
    uint8_t tty_output[MICROS_TTY_TRANSFER_MAX];
    size_t tty_output_length;
    size_t client_validate_calls;
    size_t client_copy_calls;
    size_t backend_create_calls;
    size_t backend_revoke_calls;
    size_t ramfs_calls;
    size_t tty_calls;
    size_t putnode_calls;
    uint64_t putnode_count;
    uint64_t last_putnode;
    uint32_t last_putnode_count;
    bool reject_client_validate;
    bool reject_client_copy;
    bool force_backend_grant_capacity;
    bool force_backend_grant_invariant;
    bool force_backend_revoke_invariant;
    bool force_ramfs_invariant;
    bool force_tty_invariant;
    bool corrupt_directory_record;
    bool corrupt_read_count;
    bool inject_ramfs_result;
    enum micros_vfs_ramfs_operation injected_ramfs_operation;
    enum micros_ramfs_result injected_ramfs_result;
    bool inject_tty_result;
    enum micros_vfs_tty_operation injected_tty_operation;
    enum micros_tty_result injected_tty_result;
};

void vfs_test_fixture_initialize(
    struct vfs_test_fixture *fixture,
    micros_endpoint_t vfs_endpoint,
    micros_endpoint_t ramfs_endpoint,
    micros_endpoint_t tty_endpoint
);

micros_grant_t vfs_test_fixture_add_application_grant(
    struct vfs_test_fixture *fixture,
    micros_endpoint_t endpoint,
    uint32_t permission,
    const uint8_t *bytes,
    size_t length
);

bool vfs_test_fixture_revoke_application_grant(
    struct vfs_test_fixture *fixture,
    micros_grant_t grant
);

uint8_t *vfs_test_fixture_application_bytes(
    struct vfs_test_fixture *fixture,
    micros_grant_t grant
);

uint64_t vfs_test_fixture_find_path(
    const struct vfs_test_fixture *fixture,
    const char *path
);

bool vfs_test_fixture_add_root_file(
    struct vfs_test_fixture *fixture,
    const char *name,
    const uint8_t *bytes,
    size_t length
);

bool vfs_test_fixture_complete_read(
    struct vfs_test_fixture *fixture,
    const uint8_t *bytes,
    size_t length
);

bool vfs_test_fixture_queue_input(
    struct vfs_test_fixture *fixture,
    const uint8_t *bytes,
    size_t length
);

uint64_t vfs_test_fixture_drain_output(
    struct vfs_test_fixture *fixture
);

#endif

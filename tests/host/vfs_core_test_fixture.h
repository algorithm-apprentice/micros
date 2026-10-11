#ifndef MICROS_TESTS_HOST_VFS_CORE_TEST_FIXTURE_H
#define MICROS_TESTS_HOST_VFS_CORE_TEST_FIXTURE_H

#include <stdbool.h>
#include <stdint.h>

#include "servers/vfs/vfs_core.h"
#include "tests/host/vfs_test_fixture.h"

struct vfs_core_test_backend {
    size_t mount_calls;
    size_t putnode_calls;
    uint64_t mount_node;
    uint64_t last_putnode;
    uint32_t last_putnode_count;
};

struct vfs_core_test_corrupting_ramfs {
    const struct micros_vfs_io *delegate;
    struct micros_vfs_state *state;
    bool corrupted;
};

struct vfs_core_test_context {
    struct micros_vfs_state state;
    struct micros_vfs_state snapshot;
    struct vfs_test_fixture fixture;
    struct vfs_core_test_backend backend;
    struct micros_vfs_io backend_io;
    struct vfs_core_test_corrupting_ramfs corrupting_ramfs;
    struct micros_vfs_io corrupting_io;
    micros_endpoint_t application_endpoint;
    uint64_t note;
};

void vfs_core_test_context_initialize(
    struct vfs_core_test_context *context
);

bool vfs_core_test_initialize_full(
    struct vfs_core_test_context *context
);

struct micros_vfs_request vfs_core_test_request(
    const struct vfs_core_test_context *context,
    uint32_t type
);

struct micros_vfs_request vfs_core_test_path_request(
    struct vfs_core_test_context *context,
    uint32_t type,
    const char *path,
    uint32_t open_flags,
    uint32_t mode
);

struct micros_vfs_control_request vfs_core_test_exit_request(
    const struct vfs_core_test_context *context,
    micros_endpoint_t endpoint
);

bool vfs_core_test_control_action_is_exact(
    const struct micros_vfs_control_result_action *action,
    const struct micros_vfs_control_request *request,
    enum micros_vfs_control_result result
);

bool vfs_core_test_state_names_endpoint(
    const struct micros_vfs_state *state,
    micros_endpoint_t endpoint
);

uint32_t vfs_core_test_read_u32_le(const uint8_t *bytes);

#endif

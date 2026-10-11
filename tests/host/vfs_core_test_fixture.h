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

struct vfs_core_test_context {
    struct micros_vfs_state state;
    struct micros_vfs_state snapshot;
    struct vfs_test_fixture fixture;
    struct vfs_core_test_backend backend;
    uint64_t note;
};

bool vfs_core_test_initialize_full(
    struct vfs_core_test_context *context
);

struct micros_vfs_request vfs_core_test_request(uint32_t type);

struct micros_vfs_request vfs_core_test_path_request(
    struct vfs_core_test_context *context,
    uint32_t type,
    const char *path,
    uint32_t open_flags,
    uint32_t mode
);

uint32_t vfs_core_test_read_u32_le(const uint8_t *bytes);

#endif

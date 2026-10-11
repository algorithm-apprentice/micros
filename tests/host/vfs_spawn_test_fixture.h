#ifndef MICROS_TESTS_HOST_VFS_SPAWN_TEST_FIXTURE_H
#define MICROS_TESTS_HOST_VFS_SPAWN_TEST_FIXTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/spawn.h"
#include "servers/vfs/vfs_core.h"
#include "tests/host/vfs_test_fixture.h"

struct vfs_spawn_test_context {
    struct micros_vfs_state state;
    struct micros_vfs_state snapshot;
    struct vfs_test_fixture fixture;
    uint8_t request_bytes[MICROS_SPAWN_REQUEST_SIZE_MAX];
    uint8_t executable_bytes[
        MICROS_APPLICATION_EXECUTABLE_SIZE_MAX + 1
    ];
};

#endif

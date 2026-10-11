#ifndef MICROS_TESTS_HOST_VFS_SERVICE_TEST_FIXTURE_H
#define MICROS_TESTS_HOST_VFS_SERVICE_TEST_FIXTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/bootstrap.h"
#include "micros/ipc.h"
#include "servers/vfs/vfs_core.h"
#include "tests/host/vfs_test_fixture.h"

struct vfs_service_test_context {
    struct micros_vfs_state state;
    struct vfs_test_fixture fixture;
};

#endif

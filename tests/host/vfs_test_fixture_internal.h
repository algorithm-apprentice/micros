#ifndef MICROS_TESTS_HOST_VFS_TEST_FIXTURE_INTERNAL_H
#define MICROS_TESTS_HOST_VFS_TEST_FIXTURE_INTERNAL_H

#include "tests/host/vfs_test_fixture.h"

struct vfs_test_backend_grant *vfs_test_fixture_lookup_backend_grant(
    struct vfs_test_fixture *fixture,
    micros_grant_t grant
);

#endif

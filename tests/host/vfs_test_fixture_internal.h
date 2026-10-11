#ifndef MICROS_TESTS_HOST_VFS_TEST_FIXTURE_INTERNAL_H
#define MICROS_TESTS_HOST_VFS_TEST_FIXTURE_INTERNAL_H

#include "tests/host/vfs_test_fixture.h"

struct vfs_test_backend_grant *vfs_test_fixture_lookup_backend_grant(
    struct vfs_test_fixture *fixture,
    micros_grant_t grant,
    micros_endpoint_t endpoint,
    uint32_t permission,
    size_t length
);

enum micros_vfs_client_result vfs_test_fixture_client_validate(
    void *context,
    micros_endpoint_t endpoint,
    micros_grant_t grant,
    uint64_t offset,
    size_t length,
    uint32_t permission
);

enum micros_vfs_client_result vfs_test_fixture_client_copy(
    void *context,
    enum micros_vfs_client_direction direction,
    micros_endpoint_t endpoint,
    micros_grant_t grant,
    uint64_t offset,
    uint8_t *local,
    size_t length
);

enum micros_vfs_backend_grant_result
vfs_test_fixture_backend_grant_create(
    void *context,
    micros_endpoint_t endpoint,
    uint8_t *local,
    size_t length,
    uint32_t permission,
    micros_grant_t *grant
);

enum micros_vfs_backend_status vfs_test_fixture_backend_grant_revoke(
    void *context,
    micros_grant_t grant
);

enum micros_vfs_backend_status vfs_test_fixture_ramfs_call(
    void *context,
    const struct micros_vfs_ramfs_request *request,
    struct micros_vfs_ramfs_response *response
);

enum micros_vfs_backend_status vfs_test_fixture_tty_call(
    void *context,
    const struct micros_vfs_tty_request *request,
    struct micros_vfs_tty_response *response
);

void vfs_test_fixture_seed_ramfs(
    struct vfs_test_fixture *fixture
);

#endif

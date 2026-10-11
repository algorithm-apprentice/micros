#include "tests/host/vfs_test_fixture.h"
#include "tests/host/vfs_test_fixture_internal.h"

#include <string.h>

void vfs_test_fixture_initialize(
    struct vfs_test_fixture *fixture,
    micros_endpoint_t vfs_endpoint,
    micros_endpoint_t vm_endpoint,
    micros_endpoint_t pm_endpoint,
    micros_endpoint_t ramfs_endpoint,
    micros_endpoint_t tty_endpoint
)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->vfs_endpoint = vfs_endpoint;
    fixture->vm_endpoint = vm_endpoint;
    fixture->pm_endpoint = pm_endpoint;
    fixture->ramfs_endpoint = ramfs_endpoint;
    fixture->tty_endpoint = tty_endpoint;
    fixture->io.client_validate =
        vfs_test_fixture_client_validate;
    fixture->io.client_copy = vfs_test_fixture_client_copy;
    fixture->io.backend_grant_create =
        vfs_test_fixture_backend_grant_create;
    fixture->io.backend_grant_revoke =
        vfs_test_fixture_backend_grant_revoke;
    fixture->io.ramfs_call = vfs_test_fixture_ramfs_call;
    fixture->io.tty_call = vfs_test_fixture_tty_call;
    fixture->io.context = fixture;
    vfs_test_fixture_seed_ramfs(fixture);
}

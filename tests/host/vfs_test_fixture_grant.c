#include "tests/host/vfs_test_fixture_internal.h"

#include <string.h>

static const struct vfs_test_application_grant *
resolve_application_grant(
    const struct vfs_test_fixture *fixture,
    micros_endpoint_t endpoint,
    micros_grant_t grant,
    uint64_t offset,
    size_t length,
    uint32_t permission
)
{
    const struct vfs_test_application_grant *record;

    if (
        grant == 0
        || grant > VFS_TEST_APPLICATION_GRANT_CAPACITY
    ) {
        return NULL;
    }
    record = &fixture->application_grants[grant - 1];
    if (
        !record->active
        || record->endpoint != endpoint
        || (record->permission & permission) == 0
        || offset > record->length
        || length > record->length - (size_t)offset
    ) {
        return NULL;
    }
    return record;
}

struct vfs_test_backend_grant *vfs_test_fixture_lookup_backend_grant(
    struct vfs_test_fixture *fixture,
    micros_grant_t grant,
    micros_endpoint_t endpoint,
    uint32_t permission,
    size_t length
)
{
    size_t slot;

    if (grant < UINT32_C(0x100)) {
        return NULL;
    }
    slot = grant - UINT32_C(0x100);
    if (
        slot >= MICROS_GRANT_CAPACITY
        || !fixture->backend_grants[slot].active
        || fixture->backend_grants[slot].endpoint != endpoint
        || (
            fixture->backend_grants[slot].permission
                & permission
        ) == 0
        || length > fixture->backend_grants[slot].length
    ) {
        return NULL;
    }
    return &fixture->backend_grants[slot];
}

enum micros_vfs_client_result vfs_test_fixture_client_validate(
    void *context,
    micros_endpoint_t endpoint,
    micros_grant_t grant,
    uint64_t offset,
    size_t length,
    uint32_t permission
)
{
    struct vfs_test_fixture *fixture = context;

    ++fixture->client_validate_calls;
    if (fixture->reject_client_validate) {
        return MICROS_VFS_CLIENT_REJECTED;
    }
    return resolve_application_grant(
        fixture,
        endpoint,
        grant,
        offset,
        length,
        permission
    ) == NULL
        ? MICROS_VFS_CLIENT_REJECTED
        : MICROS_VFS_CLIENT_OK;
}

enum micros_vfs_client_result vfs_test_fixture_client_copy(
    void *context,
    enum micros_vfs_client_direction direction,
    micros_endpoint_t endpoint,
    micros_grant_t grant,
    uint64_t offset,
    uint8_t *local,
    size_t length
)
{
    struct vfs_test_fixture *fixture = context;
    const struct vfs_test_application_grant *record;

    ++fixture->client_copy_calls;
    if (fixture->reject_client_copy || local == NULL) {
        return MICROS_VFS_CLIENT_REJECTED;
    }
    record = resolve_application_grant(
        fixture,
        endpoint,
        grant,
        offset,
        length,
        direction == MICROS_VFS_CLIENT_FROM_APPLICATION
            ? MICROS_GRANT_PERMISSION_READ
            : MICROS_GRANT_PERMISSION_WRITE
    );
    if (record == NULL) {
        return MICROS_VFS_CLIENT_REJECTED;
    }
    if (direction == MICROS_VFS_CLIENT_FROM_APPLICATION) {
        memcpy(local, &record->bytes[offset], length);
    } else {
        memcpy(
            &fixture->application_grants[grant - 1].bytes[offset],
            local,
            length
        );
    }
    return MICROS_VFS_CLIENT_OK;
}

enum micros_vfs_backend_grant_result
vfs_test_fixture_backend_grant_create(
    void *context,
    micros_endpoint_t endpoint,
    uint8_t *local,
    size_t length,
    uint32_t permission,
    micros_grant_t *grant
)
{
    struct vfs_test_fixture *fixture = context;
    size_t slot;

    ++fixture->backend_create_calls;
    if (
        fixture->force_backend_grant_invariant
        || local == NULL
        || length == 0
        || (
            (
                endpoint == fixture->vm_endpoint
                && (
                    permission != MICROS_GRANT_PERMISSION_READ
                    || length
                        > MICROS_APPLICATION_EXECUTABLE_SIZE_MAX
                )
            )
            || (
                endpoint != fixture->vm_endpoint
                && (
                    length > MICROS_VFS_TRANSFER_MAX
                    || (
                        permission != MICROS_GRANT_PERMISSION_READ
                        && permission
                            != MICROS_GRANT_PERMISSION_WRITE
                    )
                )
            )
        )
        || (
            endpoint != fixture->vm_endpoint
            && endpoint != fixture->ramfs_endpoint
            && endpoint != fixture->tty_endpoint
        )
        || grant == NULL
    ) {
        return MICROS_VFS_BACKEND_GRANT_INVARIANT;
    }
    if (
        fixture->force_backend_grant_capacity
        || (
            fixture->backend_capacity_on_create_call != 0
            && fixture->backend_create_calls
                == fixture->backend_capacity_on_create_call
        )
        || fixture->backend_grant_count >= MICROS_GRANT_CAPACITY
    ) {
        return MICROS_VFS_BACKEND_GRANT_CAPACITY;
    }
    for (slot = 0; slot < MICROS_GRANT_CAPACITY; ++slot) {
        if (!fixture->backend_grants[slot].active) {
            fixture->backend_grants[slot] =
                (struct vfs_test_backend_grant){
                    .active = true,
                    .endpoint = endpoint,
                    .local = local,
                    .length = length,
                    .permission = permission,
                };
            ++fixture->backend_grant_count;
            *grant = (micros_grant_t)(
                UINT32_C(0x100) + slot
            );
            return MICROS_VFS_BACKEND_GRANT_OK;
        }
    }
    return MICROS_VFS_BACKEND_GRANT_INVARIANT;
}

enum micros_vfs_backend_status vfs_test_fixture_backend_grant_revoke(
    void *context,
    micros_grant_t grant
)
{
    struct vfs_test_fixture *fixture = context;
    size_t slot;

    ++fixture->backend_revoke_calls;
    if (
        fixture->force_backend_revoke_invariant
        || grant < UINT32_C(0x100)
    ) {
        return MICROS_VFS_BACKEND_INVARIANT;
    }
    slot = grant - UINT32_C(0x100);
    if (
        slot >= MICROS_GRANT_CAPACITY
        || !fixture->backend_grants[slot].active
    ) {
        return MICROS_VFS_BACKEND_INVARIANT;
    }
    memset(
        &fixture->backend_grants[slot],
        0,
        sizeof(fixture->backend_grants[slot])
    );
    --fixture->backend_grant_count;
    return MICROS_VFS_BACKEND_OK;
}

micros_grant_t vfs_test_fixture_add_application_grant(
    struct vfs_test_fixture *fixture,
    micros_endpoint_t endpoint,
    uint32_t permission,
    const uint8_t *bytes,
    size_t length
)
{
    size_t slot;

    if (length > MICROS_VFS_TRANSFER_MAX) {
        return MICROS_GRANT_NONE;
    }
    for (
        slot = 0;
        slot < VFS_TEST_APPLICATION_GRANT_CAPACITY;
        ++slot
    ) {
        if (!fixture->application_grants[slot].active) {
            fixture->application_grants[slot].active = true;
            fixture->application_grants[slot].endpoint = endpoint;
            fixture->application_grants[slot].permission =
                permission;
            fixture->application_grants[slot].length = length;
            if (bytes != NULL && length != 0) {
                memcpy(
                    fixture->application_grants[slot].bytes,
                    bytes,
                    length
                );
            }
            return (micros_grant_t)(slot + 1);
        }
    }
    return MICROS_GRANT_NONE;
}

bool vfs_test_fixture_revoke_application_grant(
    struct vfs_test_fixture *fixture,
    micros_grant_t grant
)
{
    if (
        grant == 0
        || grant > VFS_TEST_APPLICATION_GRANT_CAPACITY
        || !fixture->application_grants[grant - 1].active
    ) {
        return false;
    }
    memset(
        &fixture->application_grants[grant - 1],
        0,
        sizeof(fixture->application_grants[grant - 1])
    );
    return true;
}

uint8_t *vfs_test_fixture_application_bytes(
    struct vfs_test_fixture *fixture,
    micros_grant_t grant
)
{
    if (
        grant == 0
        || grant > VFS_TEST_APPLICATION_GRANT_CAPACITY
        || !fixture->application_grants[grant - 1].active
    ) {
        return NULL;
    }
    return fixture->application_grants[grant - 1].bytes;
}

const struct vfs_test_backend_grant *
vfs_test_fixture_backend_grant(
    const struct vfs_test_fixture *fixture,
    micros_grant_t grant
)
{
    size_t slot;

    if (grant < UINT32_C(0x100)) {
        return NULL;
    }
    slot = grant - UINT32_C(0x100);
    if (
        slot >= MICROS_GRANT_CAPACITY
        || !fixture->backend_grants[slot].active
    ) {
        return NULL;
    }
    return &fixture->backend_grants[slot];
}

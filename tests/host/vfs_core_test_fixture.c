#include "tests/host/vfs_core_test_fixture.h"

#include <string.h>

static const micros_endpoint_t vfs_endpoint =
    UINT32_C(0x00001006);
static const micros_endpoint_t vm_endpoint =
    UINT32_C(0x00001002);
static const micros_endpoint_t pm_endpoint =
    UINT32_C(0x00001003);
static const micros_endpoint_t ramfs_endpoint =
    UINT32_C(0x00001005);
static const micros_endpoint_t tty_endpoint =
    UINT32_C(0x00001004);
static const micros_endpoint_t application_endpoint =
    UINT32_C(0x00002007);

static enum micros_vfs_backend_status test_ramfs_call(
    void *context,
    const struct micros_vfs_ramfs_request *request,
    struct micros_vfs_ramfs_response *response
)
{
    struct vfs_core_test_backend *selected = context;

    memset(response, 0, sizeof(*response));
    if (request->operation == MICROS_VFS_RAMFS_MOUNT) {
        ++selected->mount_calls;
        response->result = MICROS_RAMFS_RESULT_OK;
        response->node = selected->mount_node == 0
            ? UINT64_C(0x0000000100000000)
            : selected->mount_node;
        response->mode =
            MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0755);
        return MICROS_VFS_BACKEND_OK;
    }
    if (request->operation == MICROS_VFS_RAMFS_PUTNODE) {
        ++selected->putnode_calls;
        selected->last_putnode = request->node;
        selected->last_putnode_count = request->count;
        response->result = MICROS_RAMFS_RESULT_OK;
        return MICROS_VFS_BACKEND_OK;
    }
    return MICROS_VFS_BACKEND_INVARIANT;
}

static enum micros_vfs_backend_status corrupting_ramfs_call(
    void *context,
    const struct micros_vfs_ramfs_request *request,
    struct micros_vfs_ramfs_response *response
)
{
    struct vfs_core_test_corrupting_ramfs *selected = context;
    enum micros_vfs_backend_status status =
        selected->delegate->ramfs_call(
            selected->delegate->context,
            request,
            response
        );

    if (
        status == MICROS_VFS_BACKEND_OK
        && request->operation == MICROS_VFS_RAMFS_PUTNODE
    ) {
        selected->state->reserved = 1;
        selected->corrupted = true;
    }
    return status;
}

void vfs_core_test_context_initialize(
    struct vfs_core_test_context *context
)
{
    memset(context, 0, sizeof(*context));
    context->application_endpoint = application_endpoint;
    vfs_test_fixture_initialize(
        &context->fixture,
        vfs_endpoint,
        vm_endpoint,
        pm_endpoint,
        ramfs_endpoint,
        tty_endpoint
    );
    context->backend_io.ramfs_call = test_ramfs_call;
    context->backend_io.context = &context->backend;
    context->corrupting_ramfs.delegate = &context->fixture.io;
    context->corrupting_ramfs.state = &context->state;
    context->corrupting_io.ramfs_call = corrupting_ramfs_call;
    context->corrupting_io.context = &context->corrupting_ramfs;
}

bool vfs_core_test_initialize_full(
    struct vfs_core_test_context *context
)
{
    enum micros_vfs_trusted_result result;

    vfs_core_test_context_initialize(context);
    if (
        micros_vfs_state_initialize(
            &context->state,
            vfs_endpoint,
            vm_endpoint,
            pm_endpoint,
            ramfs_endpoint,
            tty_endpoint
        ) != MICROS_VFS_CORE_OK
        || micros_vfs_mount(
            &context->state,
            &context->fixture.io
        ) != MICROS_VFS_CORE_OK
        || micros_vfs_attach_console(
            &context->state,
            application_endpoint,
            0,
            &result
        ) != MICROS_VFS_CORE_OK
        || result != MICROS_VFS_TRUSTED_OK
    ) {
        return false;
    }
    return true;
}

struct micros_vfs_request vfs_core_test_request(
    const struct vfs_core_test_context *context,
    uint32_t type
)
{
    struct micros_vfs_request selected = {
        .type = type,
        .version = MICROS_VFS_PROTOCOL_VERSION,
        .source = context->application_endpoint,
        .reply_token = 1,
    };

    return selected;
}

struct micros_vfs_request vfs_core_test_path_request(
    struct vfs_core_test_context *context,
    uint32_t type,
    const char *path,
    uint32_t open_flags,
    uint32_t mode
)
{
    struct micros_vfs_request selected =
        vfs_core_test_request(context, type);
    size_t length = strlen(path) + 1;

    selected.grant = vfs_test_fixture_add_application_grant(
        &context->fixture,
        context->application_endpoint,
        MICROS_GRANT_PERMISSION_READ,
        (const uint8_t *)path,
        length
    );
    selected.path_length = (uint32_t)length;
    selected.open_flags = open_flags;
    selected.mode = mode;
    return selected;
}

uint32_t vfs_core_test_read_u32_le(const uint8_t *bytes)
{
    return (
        (uint32_t)bytes[0]
        | (uint32_t)bytes[1] << 8
        | (uint32_t)bytes[2] << 16
        | (uint32_t)bytes[3] << 24
    );
}

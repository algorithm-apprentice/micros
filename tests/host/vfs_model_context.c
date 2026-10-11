#include "tests/host/vfs_model_internal.h"

#include <stdio.h>
#include <string.h>

bool model_failure(const struct vfs_model *selected, const char *expression, int line) {
    size_t index;

    fprintf(stderr,
            "%s:%d: VFS model failure: %s "
            "seed=0x%016llx step=%zu\n",
            __FILE__, line, expression, (unsigned long long)selected->seed, selected->step);
    fprintf(stderr, "operation trace:");
    for (index = 0; index <= selected->step; ++index) {
        if ((index % 8) == 0) {
            fputc('\n', stderr);
        }
        fprintf(stderr, " %08x", selected->trace[index]);
    }
    fputc('\n', stderr);
    return false;
}

uint64_t model_random(struct vfs_model *selected) {
    uint64_t value = selected->random_state;

    value ^= value << 13;
    value ^= value >> 7;
    value ^= value << 17;
    selected->random_state = value;
    return value;
}

void record_operation(struct vfs_model *selected, enum model_operation operation, uint32_t detail) {
    selected->trace[selected->step] = (uint32_t)operation << 24 | (detail & UINT32_C(0x00ffffff));
}

struct micros_vfs_request model_request(uint32_t type, micros_endpoint_t source,
                                        uint64_t reply_token) {
    struct micros_vfs_request request = {
        .type = type,
        .version = MICROS_VFS_PROTOCOL_VERSION,
        .source = source,
        .reply_token = reply_token,
    };

    return request;
}

struct micros_vfs_request model_path_request(struct vfs_model *selected, uint32_t type,
                                             micros_endpoint_t endpoint, uint64_t reply_token,
                                             const char *path, uint32_t open_flags, uint32_t mode) {
    struct micros_vfs_request request = model_request(type, endpoint, reply_token);
    size_t length = strlen(path) + 1;

    request.grant = vfs_test_fixture_add_application_grant(
        &selected->fixture, endpoint, MICROS_GRANT_PERMISSION_READ, (const uint8_t *)path, length);
    request.path_length = (uint32_t)length;
    request.open_flags = open_flags;
    request.mode = mode;
    return request;
}

bool revoke_request_grant(struct vfs_model *selected, const struct micros_vfs_request *request) {
    return vfs_test_fixture_revoke_application_grant(&selected->fixture, request->grant);
}

bool vfs_model_initialize(struct vfs_model *selected) {
    enum micros_vfs_trusted_result result;

    memset(selected, 0, sizeof(*selected));
    selected->seed = UINT64_C(0x6d6963726f735646);
    selected->random_state = selected->seed;
    vfs_test_fixture_initialize(&selected->fixture, VFS_MODEL_VFS_ENDPOINT, VFS_MODEL_VM_ENDPOINT,
                                VFS_MODEL_PM_ENDPOINT, VFS_MODEL_RAMFS_ENDPOINT,
                                VFS_MODEL_TTY_ENDPOINT);
    if (micros_vfs_state_initialize(&selected->production, VFS_MODEL_VFS_ENDPOINT,
                                    VFS_MODEL_VM_ENDPOINT, VFS_MODEL_PM_ENDPOINT,
                                    VFS_MODEL_RAMFS_ENDPOINT,
                                    VFS_MODEL_TTY_ENDPOINT) != MICROS_VFS_CORE_OK ||
        micros_vfs_mount(&selected->production, &selected->fixture.io) != MICROS_VFS_CORE_OK ||
        micros_vfs_attach_console(&selected->production, VFS_MODEL_FIRST_ENDPOINT, 0, &result) !=
            MICROS_VFS_CORE_OK ||
        result != MICROS_VFS_TRUSTED_OK) {
        return false;
    }
    selected->reference.next_tty_request_id = 1;
    selected->reference.vnodes[0] = (struct micros_vfs_vnode){
        .state = MICROS_VFS_VNODE_ACTIVE,
        .mount_root = true,
        .node = reference_node_handle(0),
        .mode = MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0755),
        .local_reference_count = 1,
        .backend_reference_count = 1,
    };
    if (!reference_attach(&selected->reference, VFS_MODEL_FIRST_ENDPOINT)) {
        return false;
    }
    return compare_complete_state(selected);
}

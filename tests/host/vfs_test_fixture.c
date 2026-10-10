#include "tests/host/vfs_test_fixture.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static uint64_t node_handle(size_t slot, uint32_t generation)
{
    return (uint64_t)generation << 32 | slot;
}

static bool decode_handle(
    uint64_t handle,
    size_t *slot,
    uint32_t *generation
)
{
    uint16_t decoded_slot = (uint16_t)handle;
    uint16_t reserved = (uint16_t)(handle >> 16);
    uint32_t decoded_generation = (uint32_t)(handle >> 32);

    if (
        handle == 0
        || reserved != 0
        || decoded_slot >= MICROS_RAMFS_NODE_CAPACITY
        || decoded_generation == 0
    ) {
        return false;
    }
    *slot = decoded_slot;
    *generation = decoded_generation;
    return true;
}

static struct vfs_test_node *resolve_node(
    struct vfs_test_fixture *fixture,
    uint64_t handle
)
{
    size_t slot;
    uint32_t generation;

    if (
        !decode_handle(handle, &slot, &generation)
        || !fixture->nodes[slot].live
        || fixture->nodes[slot].generation != generation
    ) {
        return NULL;
    }
    return &fixture->nodes[slot];
}

static bool mode_is_directory(uint32_t mode)
{
    return (
        mode & MICROS_RAMFS_MODE_TYPE_MASK
    ) == MICROS_RAMFS_MODE_DIRECTORY;
}

static struct vfs_test_backend_grant *resolve_backend_grant(
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

static enum micros_vfs_client_result fixture_client_validate(
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

static enum micros_vfs_client_result fixture_client_copy(
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

static enum micros_vfs_backend_grant_result
fixture_backend_grant_create(
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
        || length > MICROS_VFS_TRANSFER_MAX
        || (
            permission != MICROS_GRANT_PERMISSION_READ
            && permission != MICROS_GRANT_PERMISSION_WRITE
        )
        || (
            endpoint != fixture->ramfs_endpoint
            && endpoint != fixture->tty_endpoint
        )
        || grant == NULL
    ) {
        return MICROS_VFS_BACKEND_GRANT_INVARIANT;
    }
    if (
        fixture->force_backend_grant_capacity
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

static enum micros_vfs_backend_status fixture_backend_grant_revoke(
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

static bool node_name_equals(
    const struct vfs_test_node *node,
    const uint8_t *name,
    size_t name_length
)
{
    return (
        node->name_length == name_length
        && memcmp(node->name, name, name_length) == 0
    );
}

static bool find_child(
    const struct vfs_test_fixture *fixture,
    size_t parent,
    const uint8_t *name,
    size_t name_length,
    size_t *slot
)
{
    size_t index;

    for (
        index = 1;
        index < MICROS_RAMFS_NODE_CAPACITY;
        ++index
    ) {
        if (
            fixture->nodes[index].live
            && fixture->nodes[index].parent == parent
            && node_name_equals(
                &fixture->nodes[index],
                name,
                name_length
            )
        ) {
            *slot = index;
            return true;
        }
    }
    return false;
}

static enum micros_ramfs_result fixture_lookup(
    struct vfs_test_fixture *fixture,
    const struct micros_vfs_ramfs_request *request,
    struct micros_vfs_ramfs_response *response
)
{
    struct vfs_test_backend_grant *grant;
    struct vfs_test_node *start;
    struct vfs_test_node *root;
    size_t current_slot;
    size_t root_slot;
    uint32_t generation;
    const uint8_t *path;
    size_t path_length;
    size_t index = 0;
    bool trailing_separator = false;

    grant = resolve_backend_grant(
        fixture,
        request->grant,
        fixture->ramfs_endpoint,
        MICROS_GRANT_PERMISSION_READ,
        request->length
    );
    start = resolve_node(fixture, request->start);
    root = resolve_node(fixture, request->root);
    if (
        grant == NULL
        || start == NULL
        || root == NULL
        || start->backend_references == 0
        || root->backend_references == 0
        || request->length == 0
        || request->length > MICROS_RAMFS_PATH_MAX
    ) {
        return grant == NULL
            ? MICROS_RAMFS_RESULT_GRANT
            : MICROS_RAMFS_RESULT_NODE;
    }
    path = grant->local;
    path_length = request->length;
    if (
        path[path_length - 1] != 0
        || memchr(path, 0, path_length - 1) != NULL
    ) {
        return MICROS_RAMFS_RESULT_MALFORMED;
    }
    if (
        !decode_handle(request->start, &current_slot, &generation)
        || !decode_handle(request->root, &root_slot, &generation)
    ) {
        return MICROS_RAMFS_RESULT_NODE;
    }
    if (path_length > 1 && path[0] == '/') {
        current_slot = root_slot;
    }
    while (index + 1 < path_length) {
        size_t component_start;
        size_t component_length;
        size_t child_slot;

        while (index + 1 < path_length && path[index] == '/') {
            trailing_separator = true;
            ++index;
        }
        if (index + 1 >= path_length) {
            break;
        }
        trailing_separator = false;
        component_start = index;
        while (
            index + 1 < path_length
            && path[index] != '/'
        ) {
            ++index;
        }
        component_length = index - component_start;
        if (component_length > MICROS_RAMFS_NAME_MAX) {
            return MICROS_RAMFS_RESULT_MALFORMED;
        }
        if (!mode_is_directory(fixture->nodes[current_slot].mode)) {
            return MICROS_RAMFS_RESULT_NOT_DIRECTORY;
        }
        if (
            component_length == 1
            && path[component_start] == '.'
        ) {
            continue;
        }
        if (
            component_length == 2
            && path[component_start] == '.'
            && path[component_start + 1] == '.'
        ) {
            if (current_slot != root_slot) {
                current_slot =
                    fixture->nodes[current_slot].parent;
            }
            continue;
        }
        if (
            !find_child(
                fixture,
                current_slot,
                &path[component_start],
                component_length,
                &child_slot
            )
        ) {
            return MICROS_RAMFS_RESULT_NOT_FOUND;
        }
        current_slot = child_slot;
    }
    if (
        trailing_separator
        && !mode_is_directory(fixture->nodes[current_slot].mode)
    ) {
        return MICROS_RAMFS_RESULT_NOT_DIRECTORY;
    }
    ++fixture->nodes[current_slot].backend_references;
    response->node = node_handle(
        current_slot,
        fixture->nodes[current_slot].generation
    );
    response->file_size = fixture->nodes[current_slot].size;
    response->mode = fixture->nodes[current_slot].mode;
    return MICROS_RAMFS_RESULT_OK;
}

static enum micros_ramfs_result fixture_create(
    struct vfs_test_fixture *fixture,
    const struct micros_vfs_ramfs_request *request,
    bool directory,
    struct micros_vfs_ramfs_response *response
)
{
    struct vfs_test_backend_grant *grant;
    struct vfs_test_node *parent;
    size_t parent_slot;
    uint32_t parent_generation;
    size_t name_length;
    size_t existing;
    size_t free_slot;

    grant = resolve_backend_grant(
        fixture,
        request->grant,
        fixture->ramfs_endpoint,
        MICROS_GRANT_PERMISSION_READ,
        request->length
    );
    parent = resolve_node(fixture, request->node);
    if (
        grant == NULL
        || parent == NULL
        || parent->backend_references == 0
        || !mode_is_directory(parent->mode)
        || request->length < 2
        || request->length > MICROS_RAMFS_NAME_MAX + 1
    ) {
        if (grant == NULL) {
            return MICROS_RAMFS_RESULT_GRANT;
        }
        if (parent == NULL || parent->backend_references == 0) {
            return MICROS_RAMFS_RESULT_NODE;
        }
        return MICROS_RAMFS_RESULT_NOT_DIRECTORY;
    }
    name_length = request->length - 1;
    if (
        grant->local[name_length] != 0
        || memchr(grant->local, 0, name_length) != NULL
        || memchr(grant->local, '/', name_length) != NULL
        || (
            name_length == 1
            && grant->local[0] == '.'
        )
        || (
            name_length == 2
            && grant->local[0] == '.'
            && grant->local[1] == '.'
        )
    ) {
        return MICROS_RAMFS_RESULT_MALFORMED;
    }
    if (
        !decode_handle(
            request->node,
            &parent_slot,
            &parent_generation
        )
    ) {
        return MICROS_RAMFS_RESULT_NODE;
    }
    if (
        find_child(
            fixture,
            parent_slot,
            grant->local,
            name_length,
            &existing
        )
    ) {
        return MICROS_RAMFS_RESULT_EXISTS;
    }
    for (
        free_slot = 1;
        free_slot < MICROS_RAMFS_NODE_CAPACITY;
        ++free_slot
    ) {
        if (!fixture->nodes[free_slot].live) {
            break;
        }
    }
    if (free_slot == MICROS_RAMFS_NODE_CAPACITY) {
        return MICROS_RAMFS_RESULT_NO_SPACE;
    }
    fixture->nodes[free_slot].live = true;
    fixture->nodes[free_slot].generation = 1;
    fixture->nodes[free_slot].mode = request->mode;
    fixture->nodes[free_slot].backend_references =
        directory ? 0 : 1;
    fixture->nodes[free_slot].parent = (uint16_t)parent_slot;
    fixture->nodes[free_slot].name_length =
        (uint16_t)name_length;
    memcpy(
        fixture->nodes[free_slot].name,
        grant->local,
        name_length
    );
    if (!directory) {
        response->node = node_handle(free_slot, 1);
        response->mode = request->mode;
    }
    return MICROS_RAMFS_RESULT_OK;
}

static enum micros_ramfs_result fixture_read(
    struct vfs_test_fixture *fixture,
    const struct micros_vfs_ramfs_request *request,
    struct micros_vfs_ramfs_response *response
)
{
    struct vfs_test_backend_grant *grant;
    struct vfs_test_node *node = resolve_node(
        fixture,
        request->node
    );
    size_t count;

    if (node == NULL || node->backend_references == 0) {
        return MICROS_RAMFS_RESULT_NODE;
    }
    if (mode_is_directory(node->mode)) {
        return MICROS_RAMFS_RESULT_IS_DIRECTORY;
    }
    if (
        request->file_offset > node->size
        || request->count > MICROS_RAMFS_TRANSFER_MAX
    ) {
        return MICROS_RAMFS_RESULT_RANGE;
    }
    grant = resolve_backend_grant(
        fixture,
        request->grant,
        fixture->ramfs_endpoint,
        MICROS_GRANT_PERMISSION_WRITE,
        request->count
    );
    if (grant == NULL) {
        return MICROS_RAMFS_RESULT_GRANT;
    }
    count = request->count;
    if (count > node->size - (size_t)request->file_offset) {
        count = node->size - (size_t)request->file_offset;
    }
    memcpy(
        grant->local,
        &node->data[request->file_offset],
        count
    );
    response->file_size = node->size;
    response->position = request->file_offset + count;
    response->count = (uint32_t)count;
    if (fixture->corrupt_read_count && response->count != 0) {
        --response->count;
        --response->position;
    }
    return MICROS_RAMFS_RESULT_OK;
}

static enum micros_ramfs_result fixture_write(
    struct vfs_test_fixture *fixture,
    const struct micros_vfs_ramfs_request *request,
    struct micros_vfs_ramfs_response *response
)
{
    struct vfs_test_backend_grant *grant;
    struct vfs_test_node *node = resolve_node(
        fixture,
        request->node
    );
    uint64_t end;

    if (node == NULL || node->backend_references == 0) {
        return MICROS_RAMFS_RESULT_NODE;
    }
    if (mode_is_directory(node->mode)) {
        return MICROS_RAMFS_RESULT_IS_DIRECTORY;
    }
    if (
        request->count > MICROS_RAMFS_TRANSFER_MAX
        || UINT64_MAX - request->file_offset < request->count
    ) {
        return MICROS_RAMFS_RESULT_RANGE;
    }
    end = request->file_offset + request->count;
    if (end > VFS_TEST_NODE_DATA_CAPACITY) {
        return MICROS_RAMFS_RESULT_NO_SPACE;
    }
    grant = resolve_backend_grant(
        fixture,
        request->grant,
        fixture->ramfs_endpoint,
        MICROS_GRANT_PERMISSION_READ,
        request->count
    );
    if (grant == NULL) {
        return MICROS_RAMFS_RESULT_GRANT;
    }
    if (request->file_offset > node->size) {
        memset(
            &node->data[node->size],
            0,
            request->file_offset - node->size
        );
    }
    memcpy(
        &node->data[request->file_offset],
        grant->local,
        request->count
    );
    if (end > node->size) {
        node->size = end;
    }
    response->file_size = node->size;
    response->position = end;
    response->count = request->count;
    return MICROS_RAMFS_RESULT_OK;
}

static void write_u32_le(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void write_u64_le(uint8_t *bytes, uint64_t value)
{
    size_t index;

    for (index = 0; index < 8; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}

static bool directory_record(
    const struct vfs_test_fixture *fixture,
    size_t directory_slot,
    uint64_t cursor,
    uint64_t *handle,
    uint32_t *mode,
    const uint8_t **name,
    size_t *name_length
)
{
    static const uint8_t dot[] = ".";
    static const uint8_t dotdot[] = "..";

    if (cursor == 0) {
        *handle = node_handle(
            directory_slot,
            fixture->nodes[directory_slot].generation
        );
        *mode = fixture->nodes[directory_slot].mode;
        *name = dot;
        *name_length = 1;
        return true;
    }
    if (cursor == 1) {
        size_t parent = fixture->nodes[directory_slot].parent;

        *handle = node_handle(
            parent,
            fixture->nodes[parent].generation
        );
        *mode = fixture->nodes[parent].mode;
        *name = dotdot;
        *name_length = 2;
        return true;
    }
    if (cursor < MICROS_RAMFS_DIRECTORY_CURSOR_END) {
        size_t slot = cursor - 2;

        if (
            slot < MICROS_RAMFS_NODE_CAPACITY
            && fixture->nodes[slot].live
            && fixture->nodes[slot].parent == directory_slot
            && slot != directory_slot
        ) {
            *handle = node_handle(
                slot,
                fixture->nodes[slot].generation
            );
            *mode = fixture->nodes[slot].mode;
            *name = fixture->nodes[slot].name;
            *name_length = fixture->nodes[slot].name_length;
            return true;
        }
    }
    return false;
}

static enum micros_ramfs_result fixture_getdents(
    struct vfs_test_fixture *fixture,
    const struct micros_vfs_ramfs_request *request,
    struct micros_vfs_ramfs_response *response
)
{
    struct vfs_test_backend_grant *grant;
    struct vfs_test_node *node = resolve_node(
        fixture,
        request->node
    );
    size_t directory_slot;
    uint32_t generation;
    uint64_t cursor;
    size_t output_count = 0;

    if (node == NULL || node->backend_references == 0) {
        return MICROS_RAMFS_RESULT_NODE;
    }
    if (!mode_is_directory(node->mode)) {
        return MICROS_RAMFS_RESULT_NOT_DIRECTORY;
    }
    if (
        request->cursor > MICROS_RAMFS_DIRECTORY_CURSOR_END
        || request->count < MICROS_RAMFS_DIRECTORY_RECORD_SIZE
        || request->count > MICROS_RAMFS_TRANSFER_MAX
        || (
            request->count % MICROS_RAMFS_DIRECTORY_RECORD_SIZE
        ) != 0
    ) {
        return MICROS_RAMFS_RESULT_RANGE;
    }
    grant = resolve_backend_grant(
        fixture,
        request->grant,
        fixture->ramfs_endpoint,
        MICROS_GRANT_PERMISSION_WRITE,
        request->count
    );
    if (grant == NULL) {
        return MICROS_RAMFS_RESULT_GRANT;
    }
    if (
        !decode_handle(
            request->node,
            &directory_slot,
            &generation
        )
    ) {
        return MICROS_RAMFS_RESULT_NODE;
    }
    cursor = request->cursor;
    while (cursor < MICROS_RAMFS_DIRECTORY_CURSOR_END) {
        uint64_t handle;
        uint32_t mode;
        const uint8_t *name;
        size_t name_length;
        uint8_t *record;

        if (
            !directory_record(
                fixture,
                directory_slot,
                cursor,
                &handle,
                &mode,
                &name,
                &name_length
            )
        ) {
            ++cursor;
            continue;
        }
        if (
            output_count + MICROS_RAMFS_DIRECTORY_RECORD_SIZE
                > request->count
        ) {
            break;
        }
        record = &grant->local[output_count];
        memset(record, 0, MICROS_RAMFS_DIRECTORY_RECORD_SIZE);
        write_u64_le(&record[0], handle);
        write_u32_le(&record[8], mode);
        write_u32_le(&record[12], (uint32_t)name_length);
        memcpy(&record[16], name, name_length);
        output_count += MICROS_RAMFS_DIRECTORY_RECORD_SIZE;
        ++cursor;
    }
    response->position = cursor;
    response->count = (uint32_t)output_count;
    if (
        fixture->corrupt_directory_record
        && output_count != 0
    ) {
        grant->local[
            MICROS_RAMFS_DIRECTORY_RECORD_SIZE - 1
        ] = UINT8_C(0xff);
    }
    return MICROS_RAMFS_RESULT_OK;
}

static enum micros_vfs_backend_status fixture_ramfs_call(
    void *context,
    const struct micros_vfs_ramfs_request *request,
    struct micros_vfs_ramfs_response *response
)
{
    struct vfs_test_fixture *fixture = context;
    enum micros_ramfs_result result;

    ++fixture->ramfs_calls;
    if (
        fixture->force_ramfs_invariant
        || request == NULL
        || response == NULL
    ) {
        return MICROS_VFS_BACKEND_INVARIANT;
    }
    memset(response, 0, sizeof(*response));
    if (
        fixture->inject_ramfs_result
        && request->operation
            == fixture->injected_ramfs_operation
    ) {
        fixture->inject_ramfs_result = false;
        response->result = fixture->injected_ramfs_result;
        return MICROS_VFS_BACKEND_OK;
    }
    switch (request->operation) {
    case MICROS_VFS_RAMFS_MOUNT:
        if (fixture->mounted) {
            result = MICROS_RAMFS_RESULT_STATE;
            break;
        }
        fixture->mounted = true;
        ++fixture->nodes[0].backend_references;
        response->node = node_handle(
            0,
            fixture->nodes[0].generation
        );
        response->mode = fixture->nodes[0].mode;
        result = MICROS_RAMFS_RESULT_OK;
        break;
    case MICROS_VFS_RAMFS_LOOKUP:
        if (
            request->length < 2
            || request->length > MICROS_RAMFS_PATH_MAX
        ) {
            return MICROS_VFS_BACKEND_INVARIANT;
        }
        result = fixture_lookup(fixture, request, response);
        break;
    case MICROS_VFS_RAMFS_CREATE:
        result = fixture_create(
            fixture,
            request,
            false,
            response
        );
        break;
    case MICROS_VFS_RAMFS_MKDIR:
        result = fixture_create(
            fixture,
            request,
            true,
            response
        );
        break;
    case MICROS_VFS_RAMFS_READ:
        result = fixture_read(fixture, request, response);
        break;
    case MICROS_VFS_RAMFS_WRITE:
        result = fixture_write(fixture, request, response);
        break;
    case MICROS_VFS_RAMFS_GETDENTS:
        result = fixture_getdents(fixture, request, response);
        break;
    case MICROS_VFS_RAMFS_PUTNODE: {
        struct vfs_test_node *node = resolve_node(
            fixture,
            request->node
        );

        ++fixture->putnode_calls;
        fixture->putnode_count += request->count;
        fixture->last_putnode = request->node;
        fixture->last_putnode_count = request->count;
        if (
            node == NULL
            || request->count == 0
            || node->backend_references < request->count
            || (
                node == &fixture->nodes[0]
                && node->backend_references
                    == request->count
            )
        ) {
            result = MICROS_RAMFS_RESULT_REFERENCE;
            break;
        }
        node->backend_references -= request->count;
        result = MICROS_RAMFS_RESULT_OK;
        break;
    }
    default:
        return MICROS_VFS_BACKEND_INVARIANT;
    }
    response->result = result;
    return MICROS_VFS_BACKEND_OK;
}

static enum micros_vfs_backend_status fixture_tty_call(
    void *context,
    const struct micros_vfs_tty_request *request,
    struct micros_vfs_tty_response *response
)
{
    struct vfs_test_fixture *fixture = context;
    struct vfs_test_backend_grant *grant;

    ++fixture->tty_calls;
    if (
        fixture->force_tty_invariant
        || request == NULL
        || response == NULL
    ) {
        return MICROS_VFS_BACKEND_INVARIANT;
    }
    memset(response, 0, sizeof(*response));
    response->request_id = request->request_id;
    if (
        fixture->inject_tty_result
        && request->operation == fixture->injected_tty_operation
    ) {
        fixture->inject_tty_result = false;
        response->result = fixture->injected_tty_result;
        return MICROS_VFS_BACKEND_OK;
    }
    switch (request->operation) {
    case MICROS_VFS_TTY_SUBMIT_READ:
        if (
            request->request_id == 0
            || request->request_id
                <= fixture->tty_last_accepted_request_id
            || fixture->pending_read.active
            || fixture->read_completion.active
        ) {
            response->result = MICROS_TTY_RESULT_REQUEST;
            break;
        }
        grant = resolve_backend_grant(
            fixture,
            request->grant,
            fixture->tty_endpoint,
            MICROS_GRANT_PERMISSION_WRITE,
            request->count
        );
        if (grant == NULL) {
            response->result = MICROS_TTY_RESULT_GRANT;
            break;
        }
        fixture->tty_last_accepted_request_id =
            request->request_id;
        if (fixture->tty_queued_input_length != 0) {
            size_t count = fixture->tty_queued_input_length;

            if (count > request->count) {
                count = request->count;
            }
            memcpy(
                grant->local,
                fixture->tty_queued_input,
                count
            );
            memset(
                fixture->tty_queued_input,
                0,
                sizeof(fixture->tty_queued_input)
            );
            fixture->tty_queued_input_length = 0;
            fixture->read_completion =
                (struct vfs_test_tty_completion){
                    .active = true,
                    .request_id = request->request_id,
                    .count = count,
                };
        } else {
            fixture->pending_read =
                (struct vfs_test_tty_request){
                    .active = true,
                    .request_id = request->request_id,
                    .grant = request->grant,
                    .count = request->count,
                };
        }
        response->result = MICROS_TTY_RESULT_OK;
        break;
    case MICROS_VFS_TTY_SUBMIT_WRITE:
        if (
            request->request_id == 0
            || request->request_id
                <= fixture->tty_last_accepted_request_id
        ) {
            response->result = MICROS_TTY_RESULT_REQUEST;
            break;
        }
        if (fixture->tty_output_busy) {
            fixture->tty_writable_armed = true;
            response->result = MICROS_TTY_RESULT_BUSY;
            break;
        }
        grant = resolve_backend_grant(
            fixture,
            request->grant,
            fixture->tty_endpoint,
            MICROS_GRANT_PERMISSION_READ,
            request->count
        );
        if (grant == NULL) {
            response->result = MICROS_TTY_RESULT_GRANT;
            break;
        }
        memcpy(
            fixture->tty_output,
            grant->local,
            request->count
        );
        fixture->tty_output_length = request->count;
        fixture->tty_output_busy = true;
        fixture->write_completion =
            (struct vfs_test_tty_completion){
                .active = true,
                .request_id = request->request_id,
                .count = request->count,
            };
        fixture->tty_last_accepted_request_id =
            request->request_id;
        response->result = MICROS_TTY_RESULT_OK;
        break;
    case MICROS_VFS_TTY_CANCEL:
        if (
            fixture->pending_read.active
            && fixture->pending_read.request_id
                == request->request_id
        ) {
            memset(
                &fixture->pending_read,
                0,
                sizeof(fixture->pending_read)
            );
            response->result = MICROS_TTY_RESULT_OK;
            break;
        }
        if (
            fixture->read_completion.active
            && fixture->read_completion.request_id
                == request->request_id
        ) {
            response->result = MICROS_TTY_RESULT_REQUEST;
            break;
        }
        response->result = MICROS_TTY_RESULT_REQUEST;
        break;
    case MICROS_VFS_TTY_COLLECT:
        if (
            fixture->read_completion.active
            && fixture->read_completion.request_id
                == request->request_id
        ) {
            response->result = MICROS_TTY_RESULT_OK;
            response->transferred_count =
                fixture->read_completion.count;
            memset(
                &fixture->read_completion,
                0,
                sizeof(fixture->read_completion)
            );
            break;
        }
        if (
            fixture->write_completion.active
            && fixture->write_completion.request_id
                == request->request_id
        ) {
            response->result = MICROS_TTY_RESULT_OK;
            response->transferred_count =
                fixture->write_completion.count;
            memset(
                &fixture->write_completion,
                0,
                sizeof(fixture->write_completion)
            );
            break;
        }
        if (fixture->pending_read.active) {
            response->result = MICROS_TTY_RESULT_PENDING;
            break;
        }
        response->result = MICROS_TTY_RESULT_REQUEST;
        break;
    default:
        return MICROS_VFS_BACKEND_INVARIANT;
    }
    return MICROS_VFS_BACKEND_OK;
}

static void seed_node(
    struct vfs_test_fixture *fixture,
    size_t slot,
    size_t parent,
    const char *name,
    uint32_t mode,
    const uint8_t *data,
    size_t size
)
{
    size_t name_length = strlen(name);

    fixture->nodes[slot].live = true;
    fixture->nodes[slot].generation = 1;
    fixture->nodes[slot].mode = mode;
    fixture->nodes[slot].parent = (uint16_t)parent;
    fixture->nodes[slot].name_length = (uint16_t)name_length;
    fixture->nodes[slot].size = size;
    memcpy(fixture->nodes[slot].name, name, name_length);
    if (size != 0) {
        memcpy(fixture->nodes[slot].data, data, size);
    }
}

void vfs_test_fixture_initialize(
    struct vfs_test_fixture *fixture,
    micros_endpoint_t vfs_endpoint,
    micros_endpoint_t ramfs_endpoint,
    micros_endpoint_t tty_endpoint
)
{
    static const uint8_t motd[] = "micros test fixture\n";

    memset(fixture, 0, sizeof(*fixture));
    fixture->vfs_endpoint = vfs_endpoint;
    fixture->ramfs_endpoint = ramfs_endpoint;
    fixture->tty_endpoint = tty_endpoint;
    fixture->io.client_validate = fixture_client_validate;
    fixture->io.client_copy = fixture_client_copy;
    fixture->io.backend_grant_create =
        fixture_backend_grant_create;
    fixture->io.backend_grant_revoke =
        fixture_backend_grant_revoke;
    fixture->io.ramfs_call = fixture_ramfs_call;
    fixture->io.tty_call = fixture_tty_call;
    fixture->io.context = fixture;
    seed_node(
        fixture,
        0,
        0,
        "",
        MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0755),
        NULL,
        0
    );
    seed_node(
        fixture,
        1,
        0,
        "etc",
        MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0755),
        NULL,
        0
    );
    seed_node(
        fixture,
        2,
        1,
        "motd",
        MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
        motd,
        sizeof(motd) - 1
    );
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

uint64_t vfs_test_fixture_find_path(
    const struct vfs_test_fixture *fixture,
    const char *path
)
{
    size_t current = 0;
    size_t index = 0;
    size_t length = strlen(path);

    while (index < length) {
        size_t start;
        size_t child;

        while (index < length && path[index] == '/') {
            ++index;
        }
        if (index == length) {
            break;
        }
        start = index;
        while (index < length && path[index] != '/') {
            ++index;
        }
        if (
            !find_child(
                fixture,
                current,
                (const uint8_t *)&path[start],
                index - start,
                &child
            )
        ) {
            return 0;
        }
        current = child;
    }
    return node_handle(
        current,
        fixture->nodes[current].generation
    );
}

bool vfs_test_fixture_add_root_file(
    struct vfs_test_fixture *fixture,
    const char *name,
    const uint8_t *bytes,
    size_t length
)
{
    size_t name_length = strlen(name);
    size_t slot;
    size_t existing;

    if (
        name_length == 0
        || name_length > MICROS_RAMFS_NAME_MAX
        || length > VFS_TEST_NODE_DATA_CAPACITY
        || memchr(name, '/', name_length) != NULL
        || find_child(
            fixture,
            0,
            (const uint8_t *)name,
            name_length,
            &existing
        )
    ) {
        return false;
    }
    for (slot = 1; slot < MICROS_RAMFS_NODE_CAPACITY; ++slot) {
        if (!fixture->nodes[slot].live) {
            seed_node(
                fixture,
                slot,
                0,
                name,
                MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
                bytes,
                length
            );
            return true;
        }
    }
    return false;
}

bool vfs_test_fixture_complete_read(
    struct vfs_test_fixture *fixture,
    const uint8_t *bytes,
    size_t length
)
{
    struct vfs_test_backend_grant *grant;

    if (
        !fixture->pending_read.active
        || fixture->read_completion.active
        || length == 0
        || length > fixture->pending_read.count
    ) {
        return false;
    }
    grant = resolve_backend_grant(
        fixture,
        fixture->pending_read.grant,
        fixture->tty_endpoint,
        MICROS_GRANT_PERMISSION_WRITE,
        length
    );
    if (grant == NULL) {
        return false;
    }
    memcpy(grant->local, bytes, length);
    fixture->read_completion =
        (struct vfs_test_tty_completion){
            .active = true,
            .request_id = fixture->pending_read.request_id,
            .count = length,
        };
    memset(
        &fixture->pending_read,
        0,
        sizeof(fixture->pending_read)
    );
    return true;
}

bool vfs_test_fixture_queue_input(
    struct vfs_test_fixture *fixture,
    const uint8_t *bytes,
    size_t length
)
{
    if (
        fixture == NULL
        || bytes == NULL
        || length == 0
        || length > MICROS_TTY_TRANSFER_MAX
        || fixture->pending_read.active
        || fixture->read_completion.active
        || fixture->tty_queued_input_length != 0
    ) {
        return false;
    }
    memcpy(fixture->tty_queued_input, bytes, length);
    fixture->tty_queued_input_length = length;
    return true;
}

uint64_t vfs_test_fixture_drain_output(
    struct vfs_test_fixture *fixture
)
{
    uint64_t events = 0;

    fixture->tty_output_busy = false;
    fixture->tty_output_length = 0;
    if (fixture->tty_writable_armed) {
        fixture->tty_writable_armed = false;
        events |= MICROS_TTY_EVENT_WRITABLE;
    }
    return events;
}

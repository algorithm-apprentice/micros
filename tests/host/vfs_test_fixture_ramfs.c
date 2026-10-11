#include "tests/host/vfs_test_fixture_internal.h"

#include <string.h>

struct fixture_lookup_context {
    const uint8_t *path;
    size_t path_length;
    size_t current_slot;
    size_t root_slot;
    bool trailing_separator;
};

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

static enum micros_ramfs_result lookup_resolve_authority(
    struct vfs_test_fixture *fixture,
    const struct micros_vfs_ramfs_request *request,
    struct fixture_lookup_context *selected
)
{
    struct vfs_test_backend_grant *grant =
        vfs_test_fixture_lookup_backend_grant(
            fixture,
            request->grant,
            fixture->ramfs_endpoint,
            MICROS_GRANT_PERMISSION_READ,
            request->length
        );
    struct vfs_test_node *start = resolve_node(
        fixture,
        request->start
    );
    struct vfs_test_node *root = resolve_node(
        fixture,
        request->root
    );

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
    selected->path = grant->local;
    selected->path_length = request->length;
    return MICROS_RAMFS_RESULT_OK;
}

static enum micros_ramfs_result lookup_select_start(
    const struct micros_vfs_ramfs_request *request,
    struct fixture_lookup_context *selected
)
{
    uint32_t generation;

    if (
        selected->path[selected->path_length - 1] != 0
        || memchr(
            selected->path,
            0,
            selected->path_length - 1
        ) != NULL
    ) {
        return MICROS_RAMFS_RESULT_MALFORMED;
    }
    if (
        !decode_handle(
            request->start,
            &selected->current_slot,
            &generation
        )
        || !decode_handle(
            request->root,
            &selected->root_slot,
            &generation
        )
    ) {
        return MICROS_RAMFS_RESULT_NODE;
    }
    if (
        selected->path_length > 1
        && selected->path[0] == '/'
    ) {
        selected->current_slot = selected->root_slot;
    }
    return MICROS_RAMFS_RESULT_OK;
}

static enum micros_ramfs_result lookup_walk_component(
    const struct vfs_test_fixture *fixture,
    const struct fixture_lookup_context *selected,
    size_t component_start,
    size_t component_length,
    size_t *current_slot
)
{
    size_t child_slot;

    if (component_length > MICROS_RAMFS_NAME_MAX) {
        return MICROS_RAMFS_RESULT_MALFORMED;
    }
    if (!mode_is_directory(fixture->nodes[*current_slot].mode)) {
        return MICROS_RAMFS_RESULT_NOT_DIRECTORY;
    }
    if (
        component_length == 1
        && selected->path[component_start] == '.'
    ) {
        return MICROS_RAMFS_RESULT_OK;
    }
    if (
        component_length == 2
        && selected->path[component_start] == '.'
        && selected->path[component_start + 1] == '.'
    ) {
        if (*current_slot != selected->root_slot) {
            *current_slot = fixture->nodes[*current_slot].parent;
        }
        return MICROS_RAMFS_RESULT_OK;
    }
    if (
        !find_child(
            fixture,
            *current_slot,
            &selected->path[component_start],
            component_length,
            &child_slot
        )
    ) {
        return MICROS_RAMFS_RESULT_NOT_FOUND;
    }
    *current_slot = child_slot;
    return MICROS_RAMFS_RESULT_OK;
}

static enum micros_ramfs_result lookup_walk_path(
    const struct vfs_test_fixture *fixture,
    struct fixture_lookup_context *selected
)
{
    size_t index = 0;

    while (index + 1 < selected->path_length) {
        enum micros_ramfs_result result;
        size_t component_start;
        size_t component_length;

        while (
            index + 1 < selected->path_length
            && selected->path[index] == '/'
        ) {
            selected->trailing_separator = true;
            ++index;
        }
        if (index + 1 >= selected->path_length) {
            break;
        }
        selected->trailing_separator = false;
        component_start = index;
        while (
            index + 1 < selected->path_length
            && selected->path[index] != '/'
        ) {
            ++index;
        }
        component_length = index - component_start;
        result = lookup_walk_component(
            fixture,
            selected,
            component_start,
            component_length,
            &selected->current_slot
        );
        if (result != MICROS_RAMFS_RESULT_OK) {
            return result;
        }
    }
    return MICROS_RAMFS_RESULT_OK;
}

static enum micros_ramfs_result lookup_publish(
    struct vfs_test_fixture *fixture,
    const struct fixture_lookup_context *selected,
    struct micros_vfs_ramfs_response *response
)
{
    struct vfs_test_node *node =
        &fixture->nodes[selected->current_slot];

    if (
        selected->trailing_separator
        && !mode_is_directory(node->mode)
    ) {
        return MICROS_RAMFS_RESULT_NOT_DIRECTORY;
    }
    ++node->backend_references;
    response->node = node_handle(
        selected->current_slot,
        node->generation
    );
    response->file_size = node->size;
    response->mode = node->mode;
    return MICROS_RAMFS_RESULT_OK;
}

static enum micros_ramfs_result fixture_lookup(
    struct vfs_test_fixture *fixture,
    const struct micros_vfs_ramfs_request *request,
    struct micros_vfs_ramfs_response *response
)
{
    struct fixture_lookup_context selected = {0};
    enum micros_ramfs_result result;

    result = lookup_resolve_authority(
        fixture,
        request,
        &selected
    );
    if (result != MICROS_RAMFS_RESULT_OK) {
        return result;
    }
    result = lookup_select_start(request, &selected);
    if (result != MICROS_RAMFS_RESULT_OK) {
        return result;
    }
    result = lookup_walk_path(fixture, &selected);
    if (result != MICROS_RAMFS_RESULT_OK) {
        return result;
    }
    return lookup_publish(fixture, &selected, response);
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

    grant = vfs_test_fixture_lookup_backend_grant(
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
    grant = vfs_test_fixture_lookup_backend_grant(
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
    grant = vfs_test_fixture_lookup_backend_grant(
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
    grant = vfs_test_fixture_lookup_backend_grant(
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

static enum micros_ramfs_result fixture_putnode(
    struct vfs_test_fixture *fixture,
    const struct micros_vfs_ramfs_request *request
)
{
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
            && node->backend_references == request->count
        )
    ) {
        return MICROS_RAMFS_RESULT_REFERENCE;
    }
    node->backend_references -= request->count;
    return MICROS_RAMFS_RESULT_OK;
}

enum micros_vfs_backend_status vfs_test_fixture_ramfs_call(
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
    case MICROS_VFS_RAMFS_PUTNODE:
        result = fixture_putnode(fixture, request);
        break;
    default:
        return MICROS_VFS_BACKEND_INVARIANT;
    }
    response->result = result;
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

void vfs_test_fixture_seed_ramfs(
    struct vfs_test_fixture *fixture
)
{
    static const uint8_t motd[] = "micros test fixture\n";

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
    return vfs_test_fixture_add_root_file_with_mode(
        fixture,
        name,
        MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
        bytes,
        length
    );
}

bool vfs_test_fixture_add_root_file_with_mode(
    struct vfs_test_fixture *fixture,
    const char *name,
    uint32_t mode,
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
        || (
            mode
            & ~(
                MICROS_RAMFS_MODE_TYPE_MASK
                | MICROS_RAMFS_MODE_PERMISSIONS
            )
        ) != 0
        || (
            mode & MICROS_RAMFS_MODE_TYPE_MASK
        ) != MICROS_RAMFS_MODE_REGULAR
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
                mode,
                bytes,
                length
            );
            return true;
        }
    }
    return false;
}

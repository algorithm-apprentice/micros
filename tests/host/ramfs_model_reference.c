#include "tests/host/ramfs_model_internal.h"

#include <stdio.h>
#include <string.h>

static void initialize_reference_node(struct reference_node *node)
{
    size_t logical;

    memset(node, 0, sizeof(*node));
    for (logical = 0; logical < MICROS_RAMFS_BLOCK_CAPACITY; ++logical) {
        node->blocks[logical] = MICROS_RAMFS_BLOCK_NONE;
    }
}

void ramfs_model_initialize_reference(struct reference_state *reference)
{
    size_t index;

    memset(reference, 0, sizeof(*reference));
    for (index = 0; index < MICROS_RAMFS_NODE_CAPACITY; ++index) {
        initialize_reference_node(&reference->nodes[index]);
    }
    for (index = 0; index < MICROS_RAMFS_BLOCK_CAPACITY; ++index) {
        reference->block_owner[index] = MICROS_RAMFS_NODE_NONE;
        reference->block_logical[index] = MICROS_RAMFS_BLOCK_NONE;
    }
    reference->node_count = 3;
    reference->allocated_block_count = 1;

    reference->nodes[0].live = true;
    reference->nodes[0].generation = 1;
    reference->nodes[0].mode =
        MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0755);
    reference->nodes[0].link_count = 3;
    reference->nodes[0].parent = 0;

    reference->nodes[1].live = true;
    reference->nodes[1].generation = 1;
    reference->nodes[1].mode =
        MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0755);
    reference->nodes[1].link_count = 2;
    reference->nodes[1].parent = 0;
    reference->nodes[1].name_length = 3;
    memcpy(reference->nodes[1].name, "etc", 3);

    reference->nodes[2].live = true;
    reference->nodes[2].generation = 1;
    reference->nodes[2].mode =
        MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644);
    reference->nodes[2].link_count = 1;
    reference->nodes[2].parent = 1;
    reference->nodes[2].name_length = 4;
    reference->nodes[2].size = 13;
    reference->nodes[2].blocks[0] = 0;
    memcpy(reference->nodes[2].name, "motd", 4);
    reference->block_owner[0] = 2;
    reference->block_logical[0] = 0;
    memcpy(reference->data[0], "micros ramfs\n", 13);
}

uint64_t ramfs_model_reference_handle(
    const struct reference_state *reference,
    uint16_t slot
)
{
    return (
        (uint64_t)reference->nodes[slot].generation << 32
        | slot
    );
}

static enum micros_ramfs_result reference_resolve(
    const struct reference_state *reference,
    uint64_t handle,
    bool require_reference,
    uint16_t *slot
)
{
    uint16_t decoded_slot = (uint16_t)handle;
    uint16_t reserved = (uint16_t)(handle >> 16);
    uint32_t generation = (uint32_t)(handle >> 32);

    if (
        handle == 0
        || reserved != 0
        || generation == 0
        || decoded_slot >= MICROS_RAMFS_NODE_CAPACITY
        || !reference->nodes[decoded_slot].live
        || reference->nodes[decoded_slot].generation != generation
    ) {
        return MICROS_RAMFS_RESULT_NODE;
    }
    if (
        require_reference
        && reference->nodes[decoded_slot].reference_count == 0
    ) {
        return MICROS_RAMFS_RESULT_REFERENCE;
    }
    *slot = decoded_slot;
    return MICROS_RAMFS_RESULT_OK;
}

static bool reference_is_directory(
    const struct reference_node *node
)
{
    return (
        node->mode & MICROS_RAMFS_MODE_TYPE_MASK
    ) == MICROS_RAMFS_MODE_DIRECTORY;
}

static bool reference_name_equal(
    const struct reference_node *node,
    const uint8_t *name,
    size_t length
)
{
    return (
        node->name_length == length
        && memcmp(node->name, name, length) == 0
    );
}

static bool reference_find_child(
    const struct reference_state *reference,
    uint16_t parent,
    const uint8_t *name,
    size_t length,
    uint16_t *slot
)
{
    uint16_t index;

    for (index = 1; index < MICROS_RAMFS_NODE_CAPACITY; ++index) {
        if (
            reference->nodes[index].live
            && reference->nodes[index].parent == parent
            && reference_name_equal(
                &reference->nodes[index],
                name,
                length
            )
        ) {
            *slot = index;
            return true;
        }
    }
    return false;
}

static bool reference_is_ancestor(
    const struct reference_state *reference,
    uint16_t ancestor,
    uint16_t node
)
{
    size_t depth;

    for (depth = 0; depth < MICROS_RAMFS_NODE_CAPACITY; ++depth) {
        if (node == ancestor) {
            return true;
        }
        if (reference->nodes[node].parent == node) {
            return false;
        }
        node = reference->nodes[node].parent;
    }
    return false;
}

static bool reference_path_components_are_bounded(
    const uint8_t *path,
    size_t path_size
)
{
    size_t position = 0;

    while (position < path_size) {
        size_t component_start;

        while (position < path_size && path[position] == '/') {
            ++position;
        }
        component_start = position;
        while (position < path_size && path[position] != '/') {
            ++position;
        }
        if (position - component_start > MICROS_RAMFS_NAME_MAX) {
            return false;
        }
    }
    return true;
}

enum micros_ramfs_result ramfs_model_reference_mount(
    struct reference_state *reference,
    struct model_fields *fields
)
{
    if (reference->mounted) {
        return MICROS_RAMFS_RESULT_STATE;
    }
    reference->mounted = true;
    reference->nodes[0].reference_count = 1;
    fields->node = ramfs_model_reference_handle(reference, 0);
    fields->mode = reference->nodes[0].mode;
    return MICROS_RAMFS_RESULT_OK;
}

static enum micros_ramfs_result reference_walk_lookup(
    struct reference_state *reference,
    uint16_t start,
    uint16_t root,
    const uint8_t *path,
    size_t path_size,
    struct model_fields *fields
)
{
    uint16_t current;
    size_t position;
    bool absolute;
    bool trailing = false;

    absolute = path_size != 0 && path[0] == '/';
    current = absolute ? root : start;
    position = 0;
    if (absolute) {
        while (position < path_size && path[position] == '/') {
            ++position;
        }
        trailing = position == path_size;
    }
    if (
        !absolute
        && path_size == 0
        && !reference_is_directory(&reference->nodes[current])
    ) {
        return MICROS_RAMFS_RESULT_NOT_DIRECTORY;
    }
    while (position < path_size) {
        size_t component_start;
        size_t component_length;
        uint16_t child;

        while (position < path_size && path[position] == '/') {
            ++position;
        }
        if (position == path_size) {
            trailing = true;
            break;
        }
        component_start = position;
        while (position < path_size && path[position] != '/') {
            ++position;
        }
        component_length = position - component_start;
        if (!reference_is_directory(&reference->nodes[current])) {
            return MICROS_RAMFS_RESULT_NOT_DIRECTORY;
        }
        if (
            component_length == 1
            && path[component_start] == '.'
        ) {
        } else if (
            component_length == 2
            && path[component_start] == '.'
            && path[component_start + 1] == '.'
        ) {
            if (current != root) {
                current = reference->nodes[current].parent;
            }
        } else if (
            !reference_find_child(
                reference,
                current,
                &path[component_start],
                component_length,
                &child
            )
        ) {
            return MICROS_RAMFS_RESULT_NOT_FOUND;
        } else {
            current = child;
        }
        trailing = position < path_size && path[position] == '/';
    }
    if (
        trailing
        && !reference_is_directory(&reference->nodes[current])
    ) {
        return MICROS_RAMFS_RESULT_NOT_DIRECTORY;
    }
    if (reference->nodes[current].reference_count == UINT32_MAX) {
        return MICROS_RAMFS_RESULT_NO_SPACE;
    }
    ++reference->nodes[current].reference_count;
    fields->node = ramfs_model_reference_handle(reference, current);
    fields->file_size = reference->nodes[current].size;
    fields->mode = reference->nodes[current].mode;
    return MICROS_RAMFS_RESULT_OK;
}

enum micros_ramfs_result ramfs_model_reference_lookup(
    struct reference_state *reference,
    uint64_t start_handle,
    uint64_t root_handle,
    const uint8_t *path,
    size_t length,
    struct model_fields *fields
)
{
    uint16_t start;
    uint16_t root;
    size_t path_size;
    size_t position;
    enum micros_ramfs_result result;

    result = reference_resolve(
        reference,
        start_handle,
        true,
        &start
    );
    if (result != MICROS_RAMFS_RESULT_OK) {
        return result;
    }
    result = reference_resolve(
        reference,
        root_handle,
        true,
        &root
    );
    if (result != MICROS_RAMFS_RESULT_OK) {
        return result;
    }
    if (!reference_is_directory(&reference->nodes[root])) {
        return MICROS_RAMFS_RESULT_NOT_DIRECTORY;
    }
    if (!reference_is_ancestor(reference, root, start)) {
        return MICROS_RAMFS_RESULT_RANGE;
    }
    if (
        length == 0
        || length > MICROS_RAMFS_PATH_MAX
        || path[length - 1] != 0
    ) {
        return MICROS_RAMFS_RESULT_RANGE;
    }
    path_size = length - 1;
    for (position = 0; position < path_size; ++position) {
        if (path[position] == 0) {
            return MICROS_RAMFS_RESULT_MALFORMED;
        }
    }
    if (!reference_path_components_are_bounded(path, path_size)) {
        return MICROS_RAMFS_RESULT_RANGE;
    }
    return reference_walk_lookup(reference, start, root, path, path_size, fields);
}

static bool reference_find_free_node(
    const struct reference_state *reference,
    uint16_t *slot
)
{
    uint16_t index;

    for (index = 0; index < MICROS_RAMFS_NODE_CAPACITY; ++index) {
        if (!reference->nodes[index].live) {
            *slot = index;
            return true;
        }
    }
    return false;
}

enum micros_ramfs_result ramfs_model_reference_create(
    struct reference_state *reference,
    uint32_t type,
    uint64_t parent_handle,
    uint32_t mode,
    const uint8_t *name,
    size_t length,
    struct model_fields *fields
)
{
    uint16_t parent;
    uint16_t slot;
    uint16_t existing;
    bool mkdir_request = type == MICROS_RAMFS_MESSAGE_MKDIR;
    enum micros_ramfs_result result;

    result = reference_resolve(
        reference,
        parent_handle,
        true,
        &parent
    );
    if (result != MICROS_RAMFS_RESULT_OK) {
        return result;
    }
    if (!reference_is_directory(&reference->nodes[parent])) {
        return MICROS_RAMFS_RESULT_NOT_DIRECTORY;
    }
    if (
        (
            mode
            & ~(
                MICROS_RAMFS_MODE_TYPE_MASK
                | MICROS_RAMFS_MODE_PERMISSIONS
            )
        ) != 0
        || (
            mkdir_request
                ? (
                    mode & MICROS_RAMFS_MODE_TYPE_MASK
                ) != MICROS_RAMFS_MODE_DIRECTORY
                : (
                    mode & MICROS_RAMFS_MODE_TYPE_MASK
                ) != MICROS_RAMFS_MODE_REGULAR
        )
    ) {
        return MICROS_RAMFS_RESULT_MALFORMED;
    }
    if (
        length < 2
        || length > MICROS_RAMFS_NAME_MAX + 1
        || name[length - 1] != 0
    ) {
        return MICROS_RAMFS_RESULT_RANGE;
    }
    if (!reference_find_free_node(reference, &slot)) {
        return MICROS_RAMFS_RESULT_NO_SPACE;
    }
    if (
        reference_find_child(
            reference,
            parent,
            name,
            length - 1,
            &existing
        )
    ) {
        return MICROS_RAMFS_RESULT_EXISTS;
    }
    initialize_reference_node(&reference->nodes[slot]);
    reference->nodes[slot].live = true;
    reference->nodes[slot].generation = 1;
    reference->nodes[slot].mode = mode;
    reference->nodes[slot].link_count = mkdir_request ? 2 : 1;
    reference->nodes[slot].reference_count = mkdir_request ? 0 : 1;
    reference->nodes[slot].parent = parent;
    reference->nodes[slot].name_length = (uint16_t)(length - 1);
    memcpy(reference->nodes[slot].name, name, length - 1);
    ++reference->node_count;
    if (mkdir_request) {
        ++reference->nodes[parent].link_count;
    } else {
        fields->node = ramfs_model_reference_handle(reference, slot);
        fields->mode = mode;
    }
    return MICROS_RAMFS_RESULT_OK;
}

static bool reference_find_free_block(
    const struct reference_state *reference,
    uint16_t *block
)
{
    uint16_t index;

    for (index = 0; index < MICROS_RAMFS_BLOCK_CAPACITY; ++index) {
        if (reference->block_owner[index] == MICROS_RAMFS_NODE_NONE) {
            *block = index;
            return true;
        }
    }
    return false;
}

enum micros_ramfs_result ramfs_model_reference_write(
    struct reference_state *reference,
    uint64_t handle,
    uint64_t offset,
    const uint8_t *bytes,
    uint32_t count,
    struct model_fields *fields
)
{
    uint16_t slot;
    uint64_t end;
    size_t first;
    size_t last;
    size_t logical;
    size_t missing = 0;
    enum micros_ramfs_result result;

    result = reference_resolve(reference, handle, true, &slot);
    if (result != MICROS_RAMFS_RESULT_OK) {
        return result;
    }
    if (reference_is_directory(&reference->nodes[slot])) {
        return MICROS_RAMFS_RESULT_IS_DIRECTORY;
    }
    if (
        count == 0
        || count > MICROS_RAMFS_TRANSFER_MAX
        || offset > MICROS_RAMFS_FILE_SIZE_MAX
        || UINT64_MAX - offset < count
        || offset + count > MICROS_RAMFS_FILE_SIZE_MAX
    ) {
        return MICROS_RAMFS_RESULT_RANGE;
    }
    end = offset + count;
    first = (size_t)(offset / MICROS_RAMFS_BLOCK_SIZE);
    last = (size_t)((end - 1) / MICROS_RAMFS_BLOCK_SIZE);
    for (logical = first; logical <= last; ++logical) {
        if (
            reference->nodes[slot].blocks[logical]
                == MICROS_RAMFS_BLOCK_NONE
        ) {
            ++missing;
        }
    }
    if (
        missing
        > MICROS_RAMFS_BLOCK_CAPACITY
            - reference->allocated_block_count
    ) {
        return MICROS_RAMFS_RESULT_NO_SPACE;
    }
    for (logical = first; logical <= last; ++logical) {
        if (
            reference->nodes[slot].blocks[logical]
                == MICROS_RAMFS_BLOCK_NONE
        ) {
            uint16_t block;

            if (!reference_find_free_block(reference, &block)) {
                return MICROS_RAMFS_RESULT_NO_SPACE;
            }
            memset(
                reference->data[block],
                0,
                MICROS_RAMFS_BLOCK_SIZE
            );
            reference->nodes[slot].blocks[logical] = block;
            reference->block_owner[block] = slot;
            reference->block_logical[block] = (uint16_t)logical;
            ++reference->allocated_block_count;
        }
    }
    for (logical = 0; logical < count; ++logical) {
        uint64_t position = offset + logical;
        size_t logical_block = (size_t)(
            position / MICROS_RAMFS_BLOCK_SIZE
        );
        size_t block_offset = (size_t)(
            position % MICROS_RAMFS_BLOCK_SIZE
        );
        uint16_t block =
            reference->nodes[slot].blocks[logical_block];

        reference->data[block][block_offset] = bytes[logical];
    }
    if (end > reference->nodes[slot].size) {
        reference->nodes[slot].size = end;
    }
    fields->file_size = reference->nodes[slot].size;
    fields->position = end;
    fields->count = count;
    return MICROS_RAMFS_RESULT_OK;
}

enum micros_ramfs_result ramfs_model_reference_read(
    const struct reference_state *reference,
    uint64_t handle,
    uint64_t offset,
    uint32_t count,
    uint8_t *output,
    struct model_fields *fields
)
{
    uint16_t slot;
    uint64_t available;
    uint32_t transferred;
    size_t index;
    enum micros_ramfs_result result;

    result = reference_resolve(
        reference,
        handle,
        true,
        &slot
    );
    if (result != MICROS_RAMFS_RESULT_OK) {
        return result;
    }
    if (reference_is_directory(&reference->nodes[slot])) {
        return MICROS_RAMFS_RESULT_IS_DIRECTORY;
    }
    if (
        count == 0
        || count > MICROS_RAMFS_TRANSFER_MAX
        || offset > MICROS_RAMFS_FILE_SIZE_MAX
    ) {
        return MICROS_RAMFS_RESULT_RANGE;
    }
    available = (
        offset < reference->nodes[slot].size
            ? reference->nodes[slot].size - offset
            : 0
    );
    transferred = available < count ? (uint32_t)available : count;
    for (index = 0; index < transferred; ++index) {
        uint64_t position = offset + index;
        size_t logical = (size_t)(
            position / MICROS_RAMFS_BLOCK_SIZE
        );
        size_t block_offset = (size_t)(
            position % MICROS_RAMFS_BLOCK_SIZE
        );
        uint16_t block = reference->nodes[slot].blocks[logical];

        output[index] = (
            block == MICROS_RAMFS_BLOCK_NONE
                ? 0
                : reference->data[block][block_offset]
        );
    }
    fields->file_size = reference->nodes[slot].size;
    fields->position = offset + transferred;
    fields->count = transferred;
    return MICROS_RAMFS_RESULT_OK;
}

static void reference_write_record(
    uint8_t *record,
    uint64_t handle,
    uint32_t mode,
    const uint8_t *name,
    uint32_t length
)
{
    memset(record, 0, MICROS_RAMFS_DIRECTORY_RECORD_SIZE);
    ramfs_model_write_u64_le(&record[0], handle);
    ramfs_model_write_u32_le(&record[8], mode);
    ramfs_model_write_u32_le(&record[12], length);
    memcpy(&record[16], name, length);
}

static bool reference_record_for_cursor(
    const struct reference_state *reference,
    uint16_t directory,
    uint64_t cursor,
    uint64_t *handle,
    uint32_t *mode,
    const uint8_t **name,
    uint32_t *length
)
{
    static const uint8_t dot[] = ".";
    static const uint8_t dotdot[] = "..";

    if (cursor == 0) {
        *handle = ramfs_model_reference_handle(reference, directory);
        *mode = reference->nodes[directory].mode;
        *name = dot;
        *length = 1;
        return true;
    }
    if (cursor == 1) {
        uint16_t parent = reference->nodes[directory].parent;

        *handle = ramfs_model_reference_handle(reference, parent);
        *mode = reference->nodes[parent].mode;
        *name = dotdot;
        *length = 2;
        return true;
    }
    if (cursor < MICROS_RAMFS_DIRECTORY_CURSOR_END) {
        uint16_t slot = (uint16_t)(cursor - 2);

        if (
            reference->nodes[slot].live
            && slot != directory
            && reference->nodes[slot].parent == directory
        ) {
            *handle = ramfs_model_reference_handle(reference, slot);
            *mode = reference->nodes[slot].mode;
            *name = reference->nodes[slot].name;
            *length = reference->nodes[slot].name_length;
            return true;
        }
    }
    return false;
}

enum micros_ramfs_result ramfs_model_reference_getdents(
    const struct reference_state *reference,
    uint64_t handle,
    uint64_t cursor,
    uint32_t capacity,
    uint8_t *output,
    struct model_fields *fields
)
{
    uint16_t directory;
    uint32_t count = 0;
    enum micros_ramfs_result result;

    result = reference_resolve(
        reference,
        handle,
        true,
        &directory
    );
    if (result != MICROS_RAMFS_RESULT_OK) {
        return result;
    }
    if (!reference_is_directory(&reference->nodes[directory])) {
        return MICROS_RAMFS_RESULT_NOT_DIRECTORY;
    }
    if (
        cursor > MICROS_RAMFS_DIRECTORY_CURSOR_END
        || capacity < MICROS_RAMFS_DIRECTORY_RECORD_SIZE
        || capacity > MICROS_RAMFS_TRANSFER_MAX
        || capacity % MICROS_RAMFS_DIRECTORY_RECORD_SIZE != 0
    ) {
        return MICROS_RAMFS_RESULT_RANGE;
    }
    while (cursor < MICROS_RAMFS_DIRECTORY_CURSOR_END) {
        uint64_t record_handle;
        uint32_t mode;
        const uint8_t *name;
        uint32_t length;

        if (
            !reference_record_for_cursor(
                reference,
                directory,
                cursor,
                &record_handle,
                &mode,
                &name,
                &length
            )
        ) {
            ++cursor;
            continue;
        }
        if (
            count + MICROS_RAMFS_DIRECTORY_RECORD_SIZE
            > capacity
        ) {
            break;
        }
        reference_write_record(
            &output[count],
            record_handle,
            mode,
            name,
            length
        );
        count += MICROS_RAMFS_DIRECTORY_RECORD_SIZE;
        ++cursor;
    }
    fields->position = cursor;
    fields->count = count;
    return MICROS_RAMFS_RESULT_OK;
}

enum micros_ramfs_result ramfs_model_reference_putnode(
    struct reference_state *reference,
    uint64_t handle,
    uint32_t count
)
{
    uint16_t slot;
    enum micros_ramfs_result result;

    result = reference_resolve(reference, handle, false, &slot);
    if (result != MICROS_RAMFS_RESULT_OK) {
        return result;
    }
    if (count == 0) {
        return MICROS_RAMFS_RESULT_RANGE;
    }
    if (
        count > reference->nodes[slot].reference_count
        || (
            slot == 0
            && reference->nodes[slot].reference_count - count < 1
        )
    ) {
        return MICROS_RAMFS_RESULT_REFERENCE;
    }
    reference->nodes[slot].reference_count -= count;
    return MICROS_RAMFS_RESULT_OK;
}

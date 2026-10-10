#include "servers/ramfs/ramfs_core.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    MODEL_STEP_COUNT = 8192,
    MODEL_GRANT = 1,
    MODEL_GRANT_CAPACITY = 8192,
    MODEL_CAPACITY_PHASE = 7900,
    MODEL_CAPACITY_PHASE_END = 7965,
};

enum model_operation {
    MODEL_OPERATION_MOUNT = 1,
    MODEL_OPERATION_LOOKUP_ABSOLUTE,
    MODEL_OPERATION_LOOKUP_RELATIVE,
    MODEL_OPERATION_CREATE,
    MODEL_OPERATION_MKDIR,
    MODEL_OPERATION_READ,
    MODEL_OPERATION_WRITE,
    MODEL_OPERATION_GETDENTS,
    MODEL_OPERATION_PUTNODE,
    MODEL_OPERATION_STALE,
    MODEL_OPERATION_MALFORMED,
    MODEL_OPERATION_GRANT_FAILURE,
};

struct reference_node {
    bool live;
    uint32_t generation;
    uint32_t mode;
    uint32_t link_count;
    uint32_t reference_count;
    uint16_t parent;
    uint16_t name_length;
    uint64_t size;
    uint8_t name[MICROS_RAMFS_NAME_MAX];
    uint16_t blocks[MICROS_RAMFS_BLOCK_CAPACITY];
};

struct reference_state {
    bool mounted;
    uint16_t node_count;
    uint16_t allocated_block_count;
    struct reference_node nodes[MICROS_RAMFS_NODE_CAPACITY];
    uint16_t block_owner[MICROS_RAMFS_BLOCK_CAPACITY];
    uint16_t block_logical[MICROS_RAMFS_BLOCK_CAPACITY];
    uint8_t data[MICROS_RAMFS_BLOCK_CAPACITY][MICROS_RAMFS_BLOCK_SIZE];
};

struct model_fields {
    uint64_t node;
    uint64_t file_size;
    uint64_t position;
    uint32_t count;
    uint32_t mode;
};

struct model_bus {
    uint8_t storage[MODEL_GRANT_CAPACITY];
    uint8_t expected[MODEL_GRANT_CAPACITY];
    bool reject;
};

struct model_coverage {
    size_t mount;
    size_t lookup_absolute;
    size_t lookup_relative;
    size_t create;
    size_t mkdir;
    size_t read;
    size_t write;
    size_t getdents;
    size_t putnode;
    size_t stale;
    size_t malformed;
    size_t grant_failure;
    size_t no_space;
};

struct ramfs_model {
    uint64_t seed;
    uint64_t random_state;
    uint64_t trace_hash;
    size_t step;
    uint32_t trace[MODEL_STEP_COUNT];
    struct micros_ramfs_state production;
    struct reference_state reference;
    struct model_bus bus;
    struct model_coverage coverage;
    uint64_t directory_cursor;
    uint16_t model_directory_slot;
    uint16_t capacity_file_slot;
};

static uint8_t seed_image[MICROS_RAMFS_SEED_IMAGE_MAX];
static size_t seed_image_size;
static struct ramfs_model model;

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

static uint32_t read_u32_le(const uint8_t *bytes)
{
    return (
        (uint32_t)bytes[0]
        | (uint32_t)bytes[1] << 8
        | (uint32_t)bytes[2] << 16
        | (uint32_t)bytes[3] << 24
    );
}

static uint64_t read_u64_le(const uint8_t *bytes)
{
    uint64_t value = 0;
    size_t index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8);
    }
    return value;
}

static bool load_seed(const char *path)
{
    FILE *stream;
    long length;

    stream = fopen(path, "rb");
    if (stream == NULL) {
        return false;
    }
    if (
        fseek(stream, 0, SEEK_END) != 0
        || (length = ftell(stream)) < 0
        || (uint64_t)length > MICROS_RAMFS_SEED_IMAGE_MAX
        || fseek(stream, 0, SEEK_SET) != 0
        || fread(seed_image, 1, (size_t)length, stream) != (size_t)length
        || fclose(stream) != 0
    ) {
        return false;
    }
    seed_image_size = (size_t)length;
    return true;
}

static bool model_failure(
    const struct ramfs_model *selected,
    const char *expression,
    int line
)
{
    size_t index;

    fprintf(
        stderr,
        "%s:%d: RAMFS model failure: %s "
        "seed=0x%016llx step=%zu\n",
        __FILE__,
        line,
        expression,
        (unsigned long long)selected->seed,
        selected->step
    );
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

#define MODEL_EXPECT(selected, expression) \
    do { \
        if (!(expression)) { \
            return model_failure( \
                (selected), \
                #expression, \
                __LINE__ \
            ); \
        } \
    } while (false)

static uint64_t next_random(struct ramfs_model *selected)
{
    uint64_t value = selected->random_state;

    value ^= value << 13;
    value ^= value >> 7;
    value ^= value << 17;
    selected->random_state = value;
    return value;
}

static void record_operation(
    struct ramfs_model *selected,
    enum model_operation operation,
    uint32_t detail
)
{
    uint32_t encoded = (
        (uint32_t)operation << 28
        | (detail & UINT32_C(0x0fffffff))
    );
    size_t byte;

    selected->trace[selected->step] = encoded;
    for (byte = 0; byte < 4; ++byte) {
        selected->trace_hash ^= (uint8_t)(encoded >> (byte * 8));
        selected->trace_hash *= MICROS_RAMFS_SEED_FNV_PRIME;
    }
}

static enum micros_ramfs_copy_result model_copy(
    void *context,
    enum micros_ramfs_copy_direction direction,
    micros_endpoint_t endpoint,
    micros_grant_t grant,
    uint64_t grant_offset,
    uint8_t *local,
    size_t length
)
{
    struct ramfs_model *selected = context;

    if (
        endpoint != UINT32_C(0x00001005)
        || local == NULL
    ) {
        return MICROS_RAMFS_COPY_INVARIANT;
    }
    if (
        selected->bus.reject
        || grant != MODEL_GRANT
        || grant_offset > MODEL_GRANT_CAPACITY
        || length > MODEL_GRANT_CAPACITY - (size_t)grant_offset
    ) {
        return MICROS_RAMFS_COPY_REJECTED;
    }
    if (direction == MICROS_RAMFS_COPY_FROM_VFS) {
        memcpy(local, &selected->bus.storage[grant_offset], length);
    } else {
        memcpy(&selected->bus.storage[grant_offset], local, length);
    }
    return MICROS_RAMFS_COPY_OK;
}

static const struct micros_ramfs_io model_io = {
    .copy = model_copy,
    .context = &model,
};

static struct micros_ipc_message request(uint32_t type)
{
    struct micros_ipc_message message = {0};

    message.type = type;
    message.source = UINT32_C(0x00001005);
    message.reply_token = 1;
    write_u32_le(
        &message.payload[0],
        MICROS_RAMFS_PROTOCOL_VERSION
    );
    return message;
}

static struct micros_ipc_message lookup_request(
    uint64_t start,
    uint64_t root,
    uint32_t length
)
{
    struct micros_ipc_message message =
        request(MICROS_RAMFS_MESSAGE_LOOKUP);

    write_u64_le(&message.payload[8], start);
    write_u64_le(&message.payload[16], root);
    write_u32_le(&message.payload[24], MODEL_GRANT);
    write_u32_le(&message.payload[28], length);
    return message;
}

static struct micros_ipc_message create_request(
    uint32_t type,
    uint64_t parent,
    uint32_t mode,
    uint32_t length
)
{
    struct micros_ipc_message message = request(type);

    write_u64_le(&message.payload[8], parent);
    write_u32_le(&message.payload[16], mode);
    write_u32_le(&message.payload[20], MODEL_GRANT);
    write_u32_le(&message.payload[32], length);
    return message;
}

static struct micros_ipc_message transfer_request(
    uint32_t type,
    uint64_t node,
    uint64_t file_offset,
    uint32_t count
)
{
    struct micros_ipc_message message = request(type);

    write_u64_le(&message.payload[8], node);
    write_u64_le(&message.payload[16], file_offset);
    write_u32_le(&message.payload[24], MODEL_GRANT);
    write_u32_le(&message.payload[28], count);
    return message;
}

static struct micros_ipc_message getdents_request(
    uint64_t node,
    uint64_t cursor,
    uint32_t count
)
{
    struct micros_ipc_message message =
        request(MICROS_RAMFS_MESSAGE_GETDENTS);

    write_u64_le(&message.payload[8], node);
    write_u64_le(&message.payload[16], cursor);
    write_u32_le(&message.payload[24], MODEL_GRANT);
    write_u32_le(&message.payload[28], count);
    return message;
}

static struct micros_ipc_message putnode_request(
    uint64_t node,
    uint32_t count
)
{
    struct micros_ipc_message message =
        request(MICROS_RAMFS_MESSAGE_PUTNODE);

    write_u64_le(&message.payload[8], node);
    write_u32_le(&message.payload[16], count);
    return message;
}

static void initialize_reference_node(struct reference_node *node)
{
    size_t logical;

    memset(node, 0, sizeof(*node));
    for (logical = 0; logical < MICROS_RAMFS_BLOCK_CAPACITY; ++logical) {
        node->blocks[logical] = MICROS_RAMFS_BLOCK_NONE;
    }
}

static void initialize_reference(struct reference_state *reference)
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

static uint64_t reference_handle(
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

static enum micros_ramfs_result reference_mount(
    struct reference_state *reference,
    struct model_fields *fields
)
{
    if (reference->mounted) {
        return MICROS_RAMFS_RESULT_STATE;
    }
    reference->mounted = true;
    reference->nodes[0].reference_count = 1;
    fields->node = reference_handle(reference, 0);
    fields->mode = reference->nodes[0].mode;
    return MICROS_RAMFS_RESULT_OK;
}

static enum micros_ramfs_result reference_lookup(
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
    uint16_t current;
    size_t path_size;
    size_t position;
    bool absolute;
    bool trailing = false;
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
    fields->node = reference_handle(reference, current);
    fields->file_size = reference->nodes[current].size;
    fields->mode = reference->nodes[current].mode;
    return MICROS_RAMFS_RESULT_OK;
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

static enum micros_ramfs_result reference_create(
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
        fields->node = reference_handle(reference, slot);
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

static enum micros_ramfs_result reference_write(
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

static enum micros_ramfs_result reference_read(
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
    write_u64_le(&record[0], handle);
    write_u32_le(&record[8], mode);
    write_u32_le(&record[12], length);
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
        *handle = reference_handle(reference, directory);
        *mode = reference->nodes[directory].mode;
        *name = dot;
        *length = 1;
        return true;
    }
    if (cursor == 1) {
        uint16_t parent = reference->nodes[directory].parent;

        *handle = reference_handle(reference, parent);
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
            *handle = reference_handle(reference, slot);
            *mode = reference->nodes[slot].mode;
            *name = reference->nodes[slot].name;
            *length = reference->nodes[slot].name_length;
            return true;
        }
    }
    return false;
}

static enum micros_ramfs_result reference_getdents(
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

static enum micros_ramfs_result reference_putnode(
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

static bool compare_state(struct ramfs_model *selected)
{
    const struct micros_ramfs_state *production =
        &selected->production;
    const struct reference_state *reference =
        &selected->reference;
    size_t index;

    MODEL_EXPECT(
        selected,
        micros_ramfs_state_validate(production)
            == MICROS_RAMFS_CORE_OK
    );
    MODEL_EXPECT(
        selected,
        (
            production->phase == MICROS_RAMFS_PHASE_MOUNTED
        ) == reference->mounted
    );
    MODEL_EXPECT(
        selected,
        production->node_count == reference->node_count
    );
    MODEL_EXPECT(
        selected,
        production->allocated_block_count
            == reference->allocated_block_count
    );
    MODEL_EXPECT(
        selected,
        memcmp(
            production->scratch,
            (uint8_t[MICROS_RAMFS_TRANSFER_MAX]){0},
            MICROS_RAMFS_TRANSFER_MAX
        ) == 0
    );
    for (index = 0; index < MICROS_RAMFS_NODE_CAPACITY; ++index) {
        const struct micros_ramfs_node *actual =
            &production->nodes[index];
        const struct reference_node *expected =
            &reference->nodes[index];
        size_t logical;

        MODEL_EXPECT(selected, actual->live == expected->live);
        MODEL_EXPECT(
            selected,
            actual->generation == expected->generation
        );
        MODEL_EXPECT(selected, actual->mode == expected->mode);
        MODEL_EXPECT(
            selected,
            actual->link_count == expected->link_count
        );
        MODEL_EXPECT(
            selected,
            actual->reference_count == expected->reference_count
        );
        MODEL_EXPECT(
            selected,
            actual->name_length == expected->name_length
        );
        MODEL_EXPECT(
            selected,
            actual->size == expected->size
        );
        MODEL_EXPECT(
            selected,
            actual->parent == (
                expected->live
                    ? reference_handle(reference, expected->parent)
                    : 0
            )
        );
        MODEL_EXPECT(
            selected,
            memcmp(
                actual->name,
                expected->name,
                MICROS_RAMFS_NAME_MAX
            ) == 0
        );
        for (
            logical = 0;
            logical < MICROS_RAMFS_BLOCK_CAPACITY;
            ++logical
        ) {
            MODEL_EXPECT(
                selected,
                actual->blocks[logical]
                    == expected->blocks[logical]
            );
        }
    }
    for (index = 0; index < MICROS_RAMFS_BLOCK_CAPACITY; ++index) {
        MODEL_EXPECT(
            selected,
            production->block_owner[index]
                == reference->block_owner[index]
        );
        MODEL_EXPECT(
            selected,
            production->block_logical[index]
                == reference->block_logical[index]
        );
        MODEL_EXPECT(
            selected,
            memcmp(
                production->data[index],
                reference->data[index],
                MICROS_RAMFS_BLOCK_SIZE
            ) == 0
        );
    }
    return true;
}

static bool execute_call(
    struct ramfs_model *selected,
    enum model_operation operation,
    uint32_t detail,
    const struct micros_ipc_message *message,
    enum micros_ramfs_result expected_result,
    const struct model_fields *expected_fields
)
{
    struct micros_ramfs_reply_action action;
    enum micros_ramfs_result actual_result;

    record_operation(selected, operation, detail);
    MODEL_EXPECT(
        selected,
        micros_ramfs_handle_call(
            &selected->production,
            message,
            &model_io,
            &action
        ) == MICROS_RAMFS_CORE_OK
    );
    actual_result = (enum micros_ramfs_result)(int32_t)read_u32_le(
        &action.message.payload[8]
    );
    MODEL_EXPECT(selected, action.active);
    MODEL_EXPECT(selected, action.reply_token == message->reply_token);
    MODEL_EXPECT(
        selected,
        action.message.type == MICROS_RAMFS_MESSAGE_RESULT
    );
    MODEL_EXPECT(
        selected,
        read_u32_le(&action.message.payload[4]) == message->type
    );
    MODEL_EXPECT(selected, actual_result == expected_result);
    if (expected_result == MICROS_RAMFS_RESULT_OK) {
        MODEL_EXPECT(
            selected,
            read_u64_le(&action.message.payload[16])
                == expected_fields->node
        );
        MODEL_EXPECT(
            selected,
            read_u64_le(&action.message.payload[24])
                == expected_fields->file_size
        );
        MODEL_EXPECT(
            selected,
            read_u64_le(&action.message.payload[32])
                == expected_fields->position
        );
        MODEL_EXPECT(
            selected,
            read_u32_le(&action.message.payload[40])
                == expected_fields->count
        );
        MODEL_EXPECT(
            selected,
            read_u32_le(&action.message.payload[44])
                == expected_fields->mode
        );
    } else {
        MODEL_EXPECT(
            selected,
            memcmp(
                &action.message.payload[12],
                (uint8_t[36]){0},
                36
            ) == 0
        );
    }
    MODEL_EXPECT(
        selected,
        memcmp(
            selected->bus.storage,
            selected->bus.expected,
            MODEL_GRANT_CAPACITY
        ) == 0
    );
    MODEL_EXPECT(selected, compare_state(selected));
    if (expected_result == MICROS_RAMFS_RESULT_NO_SPACE) {
        ++selected->coverage.no_space;
    }
    return true;
}

static void prepare_bus(struct ramfs_model *selected, uint8_t fill)
{
    memset(selected->bus.storage, fill, MODEL_GRANT_CAPACITY);
    memset(selected->bus.expected, fill, MODEL_GRANT_CAPACITY);
    selected->bus.reject = false;
}

static bool execute_mount(struct ramfs_model *selected)
{
    struct micros_ipc_message message =
        request(MICROS_RAMFS_MESSAGE_MOUNT);
    struct model_fields fields = {0};
    enum micros_ramfs_result result;

    prepare_bus(selected, 0xa5);
    result = reference_mount(&selected->reference, &fields);
    ++selected->coverage.mount;
    return execute_call(
        selected,
        MODEL_OPERATION_MOUNT,
        0,
        &message,
        result,
        &fields
    );
}

static bool execute_lookup(
    struct ramfs_model *selected,
    uint64_t start,
    uint64_t root,
    const uint8_t *path,
    size_t length,
    bool relative,
    bool reject
)
{
    struct micros_ipc_message message;
    struct model_fields fields = {0};
    enum micros_ramfs_result result;

    prepare_bus(selected, 0);
    memcpy(selected->bus.storage, path, length);
    memcpy(selected->bus.expected, path, length);
    selected->bus.reject = reject;
    message = lookup_request(start, root, (uint32_t)length);
    if (reject) {
        result = MICROS_RAMFS_RESULT_GRANT;
        ++selected->coverage.grant_failure;
    } else {
        result = reference_lookup(
            &selected->reference,
            start,
            root,
            path,
            length,
            &fields
        );
    }
    if (relative) {
        ++selected->coverage.lookup_relative;
    } else {
        ++selected->coverage.lookup_absolute;
    }
    return execute_call(
        selected,
        reject
            ? MODEL_OPERATION_GRANT_FAILURE
            : (
                relative
                    ? MODEL_OPERATION_LOOKUP_RELATIVE
                    : MODEL_OPERATION_LOOKUP_ABSOLUTE
            ),
        (uint32_t)length,
        &message,
        result,
        &fields
    );
}

static bool execute_create(
    struct ramfs_model *selected,
    uint32_t type,
    uint64_t parent,
    const char *name
)
{
    struct micros_ipc_message message;
    struct model_fields fields = {0};
    enum micros_ramfs_result result;
    uint32_t mode = (
        type == MICROS_RAMFS_MESSAGE_MKDIR
            ? MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0755)
            : MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644)
    );
    size_t length = strlen(name) + 1;

    prepare_bus(selected, 0);
    memcpy(selected->bus.storage, name, length);
    memcpy(selected->bus.expected, name, length);
    message = create_request(
        type,
        parent,
        mode,
        (uint32_t)length
    );
    result = reference_create(
        &selected->reference,
        type,
        parent,
        mode,
        (const uint8_t *)name,
        length,
        &fields
    );
    if (type == MICROS_RAMFS_MESSAGE_MKDIR) {
        ++selected->coverage.mkdir;
    } else {
        ++selected->coverage.create;
    }
    return execute_call(
        selected,
        type == MICROS_RAMFS_MESSAGE_MKDIR
            ? MODEL_OPERATION_MKDIR
            : MODEL_OPERATION_CREATE,
        (uint32_t)selected->reference.node_count,
        &message,
        result,
        &fields
    );
}

static bool execute_write(
    struct ramfs_model *selected,
    uint64_t handle,
    uint64_t offset,
    const uint8_t *bytes,
    uint32_t count
)
{
    struct micros_ipc_message message;
    struct model_fields fields = {0};
    enum micros_ramfs_result result;

    prepare_bus(selected, 0);
    memcpy(selected->bus.storage, bytes, count);
    memcpy(selected->bus.expected, bytes, count);
    message = transfer_request(
        MICROS_RAMFS_MESSAGE_WRITE,
        handle,
        offset,
        count
    );
    result = reference_write(
        &selected->reference,
        handle,
        offset,
        bytes,
        count,
        &fields
    );
    ++selected->coverage.write;
    return execute_call(
        selected,
        MODEL_OPERATION_WRITE,
        (uint32_t)(offset / MICROS_RAMFS_BLOCK_SIZE),
        &message,
        result,
        &fields
    );
}

static bool execute_read(
    struct ramfs_model *selected,
    uint64_t handle,
    uint64_t offset,
    uint32_t count
)
{
    struct micros_ipc_message message;
    struct model_fields fields = {0};
    enum micros_ramfs_result result;

    prepare_bus(selected, 0xa5);
    message = transfer_request(
        MICROS_RAMFS_MESSAGE_READ,
        handle,
        offset,
        count
    );
    result = reference_read(
        &selected->reference,
        handle,
        offset,
        count,
        selected->bus.expected,
        &fields
    );
    ++selected->coverage.read;
    return execute_call(
        selected,
        MODEL_OPERATION_READ,
        count,
        &message,
        result,
        &fields
    );
}

static bool execute_getdents(
    struct ramfs_model *selected,
    uint64_t handle,
    uint32_t capacity
)
{
    struct micros_ipc_message message;
    struct model_fields fields = {0};
    enum micros_ramfs_result result;

    if (
        selected->directory_cursor
            == MICROS_RAMFS_DIRECTORY_CURSOR_END
    ) {
        selected->directory_cursor = 0;
    }
    prepare_bus(selected, 0xa5);
    message = getdents_request(
        handle,
        selected->directory_cursor,
        capacity
    );
    result = reference_getdents(
        &selected->reference,
        handle,
        selected->directory_cursor,
        capacity,
        selected->bus.expected,
        &fields
    );
    if (result == MICROS_RAMFS_RESULT_OK) {
        selected->directory_cursor = fields.position;
    }
    ++selected->coverage.getdents;
    return execute_call(
        selected,
        MODEL_OPERATION_GETDENTS,
        capacity,
        &message,
        result,
        &fields
    );
}

static bool execute_putnode(
    struct ramfs_model *selected,
    uint64_t handle,
    uint32_t count
)
{
    struct micros_ipc_message message =
        putnode_request(handle, count);
    struct model_fields fields = {0};
    enum micros_ramfs_result result;

    prepare_bus(selected, 0xa5);
    result = reference_putnode(
        &selected->reference,
        handle,
        count
    );
    ++selected->coverage.putnode;
    return execute_call(
        selected,
        MODEL_OPERATION_PUTNODE,
        count,
        &message,
        result,
        &fields
    );
}

static bool execute_stale(struct ramfs_model *selected)
{
    struct micros_ipc_message message =
        putnode_request(micros_ramfs_handle(2, 2), 1);
    struct model_fields fields = {0};

    prepare_bus(selected, 0xa5);
    ++selected->coverage.stale;
    return execute_call(
        selected,
        MODEL_OPERATION_STALE,
        2,
        &message,
        MICROS_RAMFS_RESULT_NODE,
        &fields
    );
}

static bool execute_malformed(struct ramfs_model *selected)
{
    struct micros_ipc_message message =
        request(MICROS_RAMFS_MESSAGE_READ);
    struct model_fields fields = {0};

    write_u32_le(&message.payload[4], 1);
    prepare_bus(selected, 0xa5);
    ++selected->coverage.malformed;
    return execute_call(
        selected,
        MODEL_OPERATION_MALFORMED,
        1,
        &message,
        MICROS_RAMFS_RESULT_MALFORMED,
        &fields
    );
}

static bool build_absolute_path(
    const struct reference_state *reference,
    uint16_t slot,
    uint8_t path[MICROS_RAMFS_PATH_MAX],
    size_t *length
)
{
    uint16_t chain[MICROS_RAMFS_NODE_CAPACITY];
    size_t depth = 0;
    size_t position = 0;

    if (!reference->nodes[slot].live) {
        return false;
    }
    while (slot != 0) {
        if (depth == MICROS_RAMFS_NODE_CAPACITY) {
            return false;
        }
        chain[depth++] = slot;
        slot = reference->nodes[slot].parent;
    }
    path[position++] = '/';
    while (depth != 0) {
        const struct reference_node *node =
            &reference->nodes[chain[--depth]];

        if (
            position + node->name_length + 1
            > MICROS_RAMFS_PATH_MAX
        ) {
            return false;
        }
        memcpy(&path[position], node->name, node->name_length);
        position += node->name_length;
        if (depth != 0) {
            path[position++] = '/';
        }
    }
    path[position++] = 0;
    *length = position;
    return true;
}

static uint16_t random_live_slot(
    struct ramfs_model *selected
)
{
    uint16_t start = (uint16_t)(
        next_random(selected) % MICROS_RAMFS_NODE_CAPACITY
    );
    uint16_t offset;

    for (offset = 0; offset < MICROS_RAMFS_NODE_CAPACITY; ++offset) {
        uint16_t slot = (uint16_t)(
            (start + offset) % MICROS_RAMFS_NODE_CAPACITY
        );

        if (selected->reference.nodes[slot].live) {
            return slot;
        }
    }
    return 0;
}

static uint16_t random_referenced_nonroot_slot(
    struct ramfs_model *selected
)
{
    uint16_t start = (uint16_t)(
        1 + next_random(selected) % (MICROS_RAMFS_NODE_CAPACITY - 1)
    );
    uint16_t offset;

    for (
        offset = 0;
        offset < MICROS_RAMFS_NODE_CAPACITY - 1;
        ++offset
    ) {
        uint16_t slot = (uint16_t)(
            1
            + (
                start - 1 + offset
            ) % (MICROS_RAMFS_NODE_CAPACITY - 1)
        );

        if (
            selected->reference.nodes[slot].live
            && selected->reference.nodes[slot].reference_count != 0
            && slot != selected->capacity_file_slot
        ) {
            return slot;
        }
    }
    return MICROS_RAMFS_NODE_NONE;
}

static bool execute_random_operation(struct ramfs_model *selected)
{
    uint64_t random = next_random(selected);
    uint64_t root = reference_handle(&selected->reference, 0);
    uint8_t path[MICROS_RAMFS_PATH_MAX];
    uint8_t bytes[64];
    size_t length;
    uint16_t slot;
    size_t index;

    switch (random % 9) {
    case 0:
        slot = random_live_slot(selected);
        MODEL_EXPECT(
            selected,
            build_absolute_path(
                &selected->reference,
                slot,
                path,
                &length
            )
        );
        return execute_lookup(
            selected,
            root,
            root,
            path,
            length,
            false,
            false
        );
    case 1:
    case 2: {
        char name[8];
        uint32_t type = (
            (random & 1) == 0
                ? MICROS_RAMFS_MESSAGE_CREATE
                : MICROS_RAMFS_MESSAGE_MKDIR
        );

        (void)snprintf(
            name,
            sizeof(name),
            "%c%04u",
            type == MICROS_RAMFS_MESSAGE_CREATE ? 'f' : 'd',
            (unsigned)(selected->step % 10000)
        );
        return execute_create(selected, type, root, name);
    }
    case 3:
        for (index = 0; index < sizeof(bytes); ++index) {
            bytes[index] = (uint8_t)(random >> (index % 8));
        }
        return execute_write(
            selected,
            reference_handle(
                &selected->reference,
                selected->capacity_file_slot
            ),
            (random >> 8) % (MICROS_RAMFS_BLOCK_SIZE * 5),
            bytes,
            (uint32_t)(1 + (random % sizeof(bytes)))
        );
    case 4:
        return execute_read(
            selected,
            reference_handle(
                &selected->reference,
                selected->capacity_file_slot
            ),
            (random >> 8) % (MICROS_RAMFS_BLOCK_SIZE * 5),
            (uint32_t)(1 + (random % sizeof(bytes)))
        );
    case 5:
        return execute_getdents(
            selected,
            root,
            (uint32_t)(
                MICROS_RAMFS_DIRECTORY_RECORD_SIZE
                * (1 + random % 4)
            )
        );
    case 6:
        slot = random_referenced_nonroot_slot(selected);
        if (slot == MICROS_RAMFS_NODE_NONE) {
            return execute_putnode(selected, root, 1);
        }
        return execute_putnode(
            selected,
            reference_handle(&selected->reference, slot),
            1
        );
    case 7:
        return execute_stale(selected);
    case 8:
    default:
        if ((random & UINT64_C(0x100)) != 0) {
            static const uint8_t etc_path[] = "/etc";

            return execute_lookup(
                selected,
                root,
                root,
                etc_path,
                sizeof(etc_path),
                false,
                true
            );
        }
        return execute_malformed(selected);
    }
}

static bool execute_scripted_operation(struct ramfs_model *selected)
{
    static const uint8_t etc_path[] = "/etc";
    static const uint8_t motd_relative[] = "motd";
    static const uint8_t model_path[] = "/model";
    static const uint8_t grant_path[] = "/etc";
    static const uint8_t sparse_bytes[] = {'x', 'y'};
    uint64_t root = reference_handle(&selected->reference, 0);

    switch (selected->step) {
    case 0:
        return execute_mount(selected);
    case 1:
        return execute_lookup(
            selected,
            root,
            root,
            etc_path,
            sizeof(etc_path),
            false,
            false
        );
    case 2:
        return execute_lookup(
            selected,
            reference_handle(&selected->reference, 1),
            reference_handle(&selected->reference, 1),
            motd_relative,
            sizeof(motd_relative),
            true,
            false
        );
    case 3:
        MODEL_EXPECT(
            selected,
            execute_create(
                selected,
                MICROS_RAMFS_MESSAGE_MKDIR,
                root,
                "model"
            )
        );
        selected->model_directory_slot = 3;
        return true;
    case 4:
        return execute_lookup(
            selected,
            root,
            root,
            model_path,
            sizeof(model_path),
            false,
            false
        );
    case 5:
        MODEL_EXPECT(
            selected,
            execute_create(
                selected,
                MICROS_RAMFS_MESSAGE_CREATE,
                reference_handle(
                    &selected->reference,
                    selected->model_directory_slot
                ),
                "capacity"
            )
        );
        selected->capacity_file_slot = 4;
        return true;
    case 6:
        return execute_write(
            selected,
            reference_handle(
                &selected->reference,
                selected->capacity_file_slot
            ),
            8194,
            sparse_bytes,
            sizeof(sparse_bytes)
        );
    case 7:
        return execute_read(
            selected,
            reference_handle(
                &selected->reference,
                selected->capacity_file_slot
            ),
            8188,
            8
        );
    case 8:
    case 9:
        return execute_getdents(selected, root, 128);
    case 10:
        return execute_putnode(
            selected,
            reference_handle(&selected->reference, 2),
            1
        );
    case 11:
        return execute_stale(selected);
    case 12:
        return execute_malformed(selected);
    case 13:
        return execute_lookup(
            selected,
            root,
            root,
            grant_path,
            sizeof(grant_path),
            false,
            true
        );
    case 14:
        return execute_mount(selected);
    case 15: {
        uint8_t path[
            sizeof("/missing/") + MICROS_RAMFS_NAME_MAX + 1
        ];

        memcpy(path, "/missing/", sizeof("/missing/") - 1);
        memset(
            &path[sizeof("/missing/") - 1],
            'a',
            MICROS_RAMFS_NAME_MAX + 1
        );
        path[sizeof(path) - 1] = 0;
        return execute_lookup(
            selected,
            root,
            root,
            path,
            sizeof(path),
            false,
            false
        );
    }
    default:
        return false;
    }
}

static bool run_model(uint64_t seed)
{
    size_t step;

    memset(&model, 0, sizeof(model));
    model.seed = seed;
    model.random_state = seed;
    model.trace_hash = MICROS_RAMFS_SEED_FNV_OFFSET;
    model.capacity_file_slot = MICROS_RAMFS_NODE_NONE;
    initialize_reference(&model.reference);
    MODEL_EXPECT(
        &model,
        micros_ramfs_state_initialize(
            &model.production,
            UINT32_C(0x00001005),
            seed_image,
            seed_image_size
        ) == MICROS_RAMFS_CORE_OK
    );
    MODEL_EXPECT(&model, compare_state(&model));

    for (step = 0; step < MODEL_STEP_COUNT; ++step) {
        model.step = step;
        if (step <= 15) {
            MODEL_EXPECT(&model, execute_scripted_operation(&model));
        } else if (
            step >= MODEL_CAPACITY_PHASE
            && step < MODEL_CAPACITY_PHASE_END
        ) {
            uint8_t byte = (uint8_t)step;
            size_t logical = step - MODEL_CAPACITY_PHASE;

            MODEL_EXPECT(
                &model,
                execute_write(
                    &model,
                    reference_handle(
                        &model.reference,
                        model.capacity_file_slot
                    ),
                    logical * MICROS_RAMFS_BLOCK_SIZE,
                    &byte,
                    1
                )
            );
        } else {
            MODEL_EXPECT(&model, execute_random_operation(&model));
        }
    }

    MODEL_EXPECT(&model, model.coverage.mount >= 2);
    MODEL_EXPECT(&model, model.coverage.lookup_absolute != 0);
    MODEL_EXPECT(&model, model.coverage.lookup_relative != 0);
    MODEL_EXPECT(&model, model.coverage.create != 0);
    MODEL_EXPECT(&model, model.coverage.mkdir != 0);
    MODEL_EXPECT(&model, model.coverage.read != 0);
    MODEL_EXPECT(&model, model.coverage.write != 0);
    MODEL_EXPECT(&model, model.coverage.getdents != 0);
    MODEL_EXPECT(&model, model.coverage.putnode != 0);
    MODEL_EXPECT(&model, model.coverage.stale != 0);
    MODEL_EXPECT(&model, model.coverage.malformed != 0);
    MODEL_EXPECT(&model, model.coverage.grant_failure != 0);
    MODEL_EXPECT(&model, model.coverage.no_space != 0);
    printf(
        "RAMFS_MODEL_PASS seed=0x%016llx steps=%d "
        "trace=0x%016llx\n",
        (unsigned long long)seed,
        MODEL_STEP_COUNT,
        (unsigned long long)model.trace_hash
    );
    return true;
}

int main(int argc, char **argv)
{
    const uint64_t seed = UINT64_C(0x52414d46534d4f44);
    const char *seed_path;

    if (argc == 1) {
        seed_path = "generated/ramfs_seed.bin";
    } else if (argc == 2) {
        seed_path = argv[1];
    } else {
        return 1;
    }
    if (!load_seed(seed_path) || !run_model(seed)) {
        return 1;
    }
    return 0;
}

#include "servers/ramfs/ramfs_internal.h"

bool ramfs_endpoint_is_valid(micros_endpoint_t endpoint)
{
    uint32_t generation = endpoint >> MICROS_ENDPOINT_SLOT_BITS;

    return (
        endpoint != MICROS_ENDPOINT_NONE
        && endpoint != MICROS_ENDPOINT_ANY
        && generation != 0
        && generation <= MICROS_ENDPOINT_GENERATION_MAX
    );
}

bool ramfs_mode_is_canonical(uint32_t mode)
{
    uint32_t type = mode & MICROS_RAMFS_MODE_TYPE_MASK;

    return (
        (type == MICROS_RAMFS_MODE_DIRECTORY
            || type == MICROS_RAMFS_MODE_REGULAR)
        && (
            mode
            & ~(
                MICROS_RAMFS_MODE_TYPE_MASK
                | MICROS_RAMFS_MODE_PERMISSIONS
            )
        ) == 0
    );
}

bool ramfs_node_is_directory(const struct micros_ramfs_node *node)
{
    return (
        node->mode & MICROS_RAMFS_MODE_TYPE_MASK
    ) == MICROS_RAMFS_MODE_DIRECTORY;
}

bool ramfs_node_is_regular(const struct micros_ramfs_node *node)
{
    return (
        node->mode & MICROS_RAMFS_MODE_TYPE_MASK
    ) == MICROS_RAMFS_MODE_REGULAR;
}

uint64_t micros_ramfs_handle(uint16_t slot, uint32_t generation)
{
    if (
        slot >= MICROS_RAMFS_NODE_CAPACITY
        || generation == 0
    ) {
        return 0;
    }
    return (uint64_t)generation << 32 | slot;
}

bool ramfs_decode_handle(
    uint64_t handle,
    uint16_t *slot,
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

void ramfs_initialize_empty_node(struct micros_ramfs_node *node)
{
    size_t index;

    ramfs_zero_bytes(node, sizeof(*node));
    for (index = 0; index < MICROS_RAMFS_BLOCK_CAPACITY; ++index) {
        node->blocks[index] = MICROS_RAMFS_BLOCK_NONE;
    }
}

uint64_t ramfs_node_handle(
    uint16_t slot,
    const struct micros_ramfs_node *node
)
{
    return micros_ramfs_handle(slot, node->generation);
}

static enum micros_ramfs_result resolve_node(
    const struct micros_ramfs_state *state,
    uint64_t handle,
    uint16_t *slot,
    const struct micros_ramfs_node **node
)
{
    uint16_t decoded_slot;
    uint32_t generation;
    const struct micros_ramfs_node *candidate;

    if (!ramfs_decode_handle(handle, &decoded_slot, &generation)) {
        return MICROS_RAMFS_RESULT_NODE;
    }
    candidate = &state->nodes[decoded_slot];
    if (!candidate->live || candidate->generation != generation) {
        return MICROS_RAMFS_RESULT_NODE;
    }
    *slot = decoded_slot;
    *node = candidate;
    return MICROS_RAMFS_RESULT_OK;
}

enum micros_ramfs_result ramfs_resolve_referenced_node(
    const struct micros_ramfs_state *state,
    uint64_t handle,
    uint16_t *slot,
    const struct micros_ramfs_node **node
)
{
    enum micros_ramfs_result result;

    result = resolve_node(state, handle, slot, node);
    if (result != MICROS_RAMFS_RESULT_OK) {
        return result;
    }
    if ((*node)->reference_count == 0) {
        return MICROS_RAMFS_RESULT_REFERENCE;
    }
    return MICROS_RAMFS_RESULT_OK;
}

bool ramfs_names_equal(
    const struct micros_ramfs_node *left,
    const struct micros_ramfs_node *right
)
{
    size_t index;

    if (left->name_length != right->name_length) {
        return false;
    }
    for (index = 0; index < left->name_length; ++index) {
        if (left->name[index] != right->name[index]) {
            return false;
        }
    }
    return true;
}

bool ramfs_name_equals_bytes(
    const struct micros_ramfs_node *node,
    const uint8_t *name,
    size_t name_length
)
{
    size_t index;

    if (node->name_length != name_length) {
        return false;
    }
    for (index = 0; index < name_length; ++index) {
        if (node->name[index] != name[index]) {
            return false;
        }
    }
    return true;
}

bool ramfs_find_free_node(
    const struct micros_ramfs_state *state,
    uint16_t *slot
)
{
    uint16_t index;

    for (index = 0; index < MICROS_RAMFS_NODE_CAPACITY; ++index) {
        if (
            !state->nodes[index].live
            && state->nodes[index].generation < UINT32_MAX
        ) {
            *slot = index;
            return true;
        }
    }
    return false;
}

bool ramfs_find_free_block(
    const struct micros_ramfs_state *state,
    uint16_t *block
)
{
    uint16_t index;

    for (index = 0; index < MICROS_RAMFS_BLOCK_CAPACITY; ++index) {
        if (state->block_owner[index] == MICROS_RAMFS_NODE_NONE) {
            *block = index;
            return true;
        }
    }
    return false;
}

enum micros_ramfs_result ramfs_handle_mount(
    struct micros_ramfs_state *state,
    struct ramfs_result_fields *fields
)
{
    struct micros_ramfs_node *root = &state->nodes[0];

    if (root->reference_count == UINT32_MAX) {
        return MICROS_RAMFS_RESULT_NO_SPACE;
    }
    ++root->reference_count;
    state->phase = MICROS_RAMFS_PHASE_MOUNTED;
    fields->node = ramfs_node_handle(0, root);
    fields->mode = root->mode;
    return MICROS_RAMFS_RESULT_OK;
}

enum micros_ramfs_result ramfs_handle_putnode(
    struct micros_ramfs_state *state,
    const struct micros_ramfs_request *request
)
{
    const struct micros_ramfs_node *resolved_node;
    struct micros_ramfs_node *node;
    uint16_t slot;
    enum micros_ramfs_result result;

    result = resolve_node(
        state,
        request->node,
        &slot,
        &resolved_node
    );
    if (result != MICROS_RAMFS_RESULT_OK) {
        return result;
    }
    if (request->count == 0) {
        return MICROS_RAMFS_RESULT_RANGE;
    }
    if (
        request->count > resolved_node->reference_count
        || (
            slot == 0
            && resolved_node->reference_count - request->count < 1
        )
    ) {
        return MICROS_RAMFS_RESULT_REFERENCE;
    }
    node = &state->nodes[slot];
    node->reference_count -= request->count;
    return MICROS_RAMFS_RESULT_OK;
}

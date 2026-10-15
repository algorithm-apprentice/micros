#include "servers/ramfs/ramfs_internal.h"

#include "servers/ramfs/ramfs_seed.h"

#define MICROS_RAMFS_INITIALIZATION_MAGIC \
    UINT64_C(0x4d4943524f535246)

static bool empty_node_is_canonical(
    const struct micros_ramfs_node *node
)
{
    size_t logical;

    if (
        node->generation != 0
        || node->mode != 0
        || node->link_count != 0
        || node->reference_count != 0
        || node->name_length != 0
        || node->reserved1 != 0
        || node->parent != 0
        || node->size != 0
        || !ramfs_bytes_are_zero(
            node->reserved0,
            sizeof(node->reserved0)
        )
        || !ramfs_bytes_are_zero(node->name, sizeof(node->name))
    ) {
        return false;
    }
    for (
        logical = 0;
        logical < MICROS_RAMFS_BLOCK_CAPACITY;
        ++logical
    ) {
        if (node->blocks[logical] != MICROS_RAMFS_BLOCK_NONE) {
            return false;
        }
    }
    return true;
}

static bool node_namespace_is_canonical(
    const struct micros_ramfs_state *state,
    const struct micros_ramfs_node *node,
    size_t index
)
{
    size_t sibling;

    if (index == 0) {
        if (
            node->generation != 1
            || !ramfs_node_is_directory(node)
            || node->parent != ramfs_node_handle(0, node)
            || node->name_length != 0
            || !ramfs_bytes_are_zero(node->name, sizeof(node->name))
        ) {
            return false;
        }
    } else {
        uint16_t parent_slot;
        uint32_t parent_generation;

        if (
            node->name_length == 0
            || node->name_length > MICROS_RAMFS_NAME_MAX
            || !ramfs_decode_handle(
                node->parent,
                &parent_slot,
                &parent_generation
            )
            || parent_slot >= index
            || !state->nodes[parent_slot].live
            || state->nodes[parent_slot].generation
                != parent_generation
            || !ramfs_node_is_directory(&state->nodes[parent_slot])
        ) {
            return false;
        }
        for (sibling = 0; sibling < node->name_length; ++sibling) {
            if (
                node->name[sibling] == 0
                || node->name[sibling] == '/'
            ) {
                return false;
            }
        }
        if (
            (
                node->name_length == 1
                && node->name[0] == '.'
            )
            || (
                node->name_length == 2
                && node->name[0] == '.'
                && node->name[1] == '.'
            )
            || !ramfs_bytes_are_zero(
                &node->name[node->name_length],
                MICROS_RAMFS_NAME_MAX - node->name_length
            )
        ) {
            return false;
        }
        for (sibling = 1; sibling < index; ++sibling) {
            const struct micros_ramfs_node *candidate =
                &state->nodes[sibling];

            if (
                candidate->live
                && candidate->parent == node->parent
                && ramfs_names_equal(candidate, node)
            ) {
                return false;
            }
        }
    }
    return true;
}

static bool node_blocks_are_canonical(
    const struct micros_ramfs_state *state,
    const struct micros_ramfs_node *node,
    size_t index,
    bool *seen_blocks
)
{
    size_t logical;

    if (ramfs_node_is_directory(node)) {
        if (node->size != 0) {
            return false;
        }
        for (
            logical = 0;
            logical < MICROS_RAMFS_BLOCK_CAPACITY;
            ++logical
        ) {
            if (node->blocks[logical] != MICROS_RAMFS_BLOCK_NONE) {
                return false;
            }
        }
    } else {
        if (
            !ramfs_node_is_regular(node)
            || node->size > MICROS_RAMFS_FILE_SIZE_MAX
        ) {
            return false;
        }
        for (
            logical = 0;
            logical < MICROS_RAMFS_BLOCK_CAPACITY;
            ++logical
        ) {
            uint16_t block = node->blocks[logical];

            if (block == MICROS_RAMFS_BLOCK_NONE) {
                continue;
            }
            if (
                block >= MICROS_RAMFS_BLOCK_CAPACITY
                || seen_blocks[block]
                || (uint64_t)logical * MICROS_RAMFS_BLOCK_SIZE
                    >= node->size
                || state->block_owner[block] != index
                || state->block_logical[block] != logical
            ) {
                return false;
            }
            seen_blocks[block] = true;
        }
    }
    return true;
}

static bool node_links_are_canonical(
    const struct micros_ramfs_state *state
)
{
    size_t index;

    for (index = 0; index < MICROS_RAMFS_NODE_CAPACITY; ++index) {
        const struct micros_ramfs_node *node = &state->nodes[index];
        uint32_t expected_links;
        size_t child;

        if (!node->live) {
            continue;
        }
        expected_links = ramfs_node_is_directory(node) ? 2 : 1;
        if (ramfs_node_is_directory(node)) {
            for (child = 1; child < MICROS_RAMFS_NODE_CAPACITY; ++child) {
                if (
                    state->nodes[child].live
                    && ramfs_node_is_directory(&state->nodes[child])
                    && state->nodes[child].parent
                        == ramfs_node_handle((uint16_t)index, node)
                ) {
                    ++expected_links;
                }
            }
        }
        if (node->link_count != expected_links) {
            return false;
        }
    }
    return true;
}

static bool block_ownership_is_canonical(
    const struct micros_ramfs_state *state,
    const bool *seen_blocks,
    size_t *used_blocks
)
{
    size_t index;

    for (index = 0; index < MICROS_RAMFS_BLOCK_CAPACITY; ++index) {
        uint16_t owner = state->block_owner[index];
        uint16_t logical = state->block_logical[index];

        if (owner == MICROS_RAMFS_NODE_NONE) {
            if (
                logical != MICROS_RAMFS_BLOCK_NONE
                || seen_blocks[index]
                || !ramfs_bytes_are_zero(
                    state->data[index],
                    MICROS_RAMFS_BLOCK_SIZE
                )
            ) {
                return false;
            }
            continue;
        }
        if (
            owner >= MICROS_RAMFS_NODE_CAPACITY
            || logical >= MICROS_RAMFS_BLOCK_CAPACITY
            || !seen_blocks[index]
            || !state->nodes[owner].live
            || !ramfs_node_is_regular(&state->nodes[owner])
            || state->nodes[owner].blocks[logical] != index
        ) {
            return false;
        }
        ++*used_blocks;
    }
    return true;
}

enum micros_ramfs_core_error ramfs_validate_state(
    const struct micros_ramfs_state *state,
    bool require_clear_scratch
)
{
    bool seen_blocks[MICROS_RAMFS_BLOCK_CAPACITY] = {false};
    size_t live_count = 0;
    size_t used_blocks = 0;
    size_t index;

    if (state == NULL) {
        return MICROS_RAMFS_CORE_ERROR_ARGUMENT;
    }
    if (
        state->initialization_magic
            != MICROS_RAMFS_INITIALIZATION_MAGIC
        || !ramfs_endpoint_is_valid(state->vfs_endpoint)
        || (
            state->phase != MICROS_RAMFS_PHASE_READY_UNMOUNTED
            && state->phase != MICROS_RAMFS_PHASE_MOUNTED
        )
        || state->node_count == 0
        || state->node_count > MICROS_RAMFS_NODE_CAPACITY
        || state->allocated_block_count
            > MICROS_RAMFS_BLOCK_CAPACITY
        || state->reserved != 0
        || (
            require_clear_scratch
            && !ramfs_bytes_are_zero(
                state->scratch,
                sizeof(state->scratch)
            )
        )
    ) {
        return MICROS_RAMFS_CORE_ERROR_INVARIANT;
    }

    for (index = 0; index < MICROS_RAMFS_NODE_CAPACITY; ++index) {
        const struct micros_ramfs_node *node =
            &state->nodes[index];

        if (!node->live) {
            if (!empty_node_is_canonical(node)) {
                return MICROS_RAMFS_CORE_ERROR_INVARIANT;
            }
            continue;
        }

        ++live_count;
        if (
            node->generation == 0
            || !ramfs_mode_is_canonical(node->mode)
            || node->reserved1 != 0
            || !ramfs_bytes_are_zero(
                node->reserved0,
                sizeof(node->reserved0)
            )
            || (
                state->phase == MICROS_RAMFS_PHASE_READY_UNMOUNTED
                && node->reference_count != 0
            )
        ) {
            return MICROS_RAMFS_CORE_ERROR_INVARIANT;
        }

        if (!node_namespace_is_canonical(state, node, index)) {
            return MICROS_RAMFS_CORE_ERROR_INVARIANT;
        }

        if (!node_blocks_are_canonical(state, node, index, seen_blocks)) {
            return MICROS_RAMFS_CORE_ERROR_INVARIANT;
        }
    }

    if (
        live_count != state->node_count
        || !state->nodes[0].live
        || (
            state->phase == MICROS_RAMFS_PHASE_MOUNTED
            && state->nodes[0].reference_count == 0
        )
    ) {
        return MICROS_RAMFS_CORE_ERROR_INVARIANT;
    }

    if (!node_links_are_canonical(state)) {
        return MICROS_RAMFS_CORE_ERROR_INVARIANT;
    }

    if (!block_ownership_is_canonical(state, seen_blocks, &used_blocks)) {
        return MICROS_RAMFS_CORE_ERROR_INVARIANT;
    }
    if (used_blocks != state->allocated_block_count) {
        return MICROS_RAMFS_CORE_ERROR_INVARIANT;
    }
    return MICROS_RAMFS_CORE_OK;
}

enum micros_ramfs_core_error micros_ramfs_state_validate(
    const struct micros_ramfs_state *state
)
{
    return ramfs_validate_state(state, true);
}

enum micros_ramfs_core_error micros_ramfs_state_initialize(
    struct micros_ramfs_state *state,
    micros_endpoint_t vfs_endpoint,
    const uint8_t *seed_image,
    size_t seed_image_size
)
{
    struct micros_ramfs_seed_plan plan;
    enum micros_ramfs_seed_error seed_error;
    size_t index;

    if (
        state == NULL
        || !ramfs_endpoint_is_valid(vfs_endpoint)
        || seed_image == NULL
    ) {
        return MICROS_RAMFS_CORE_ERROR_ARGUMENT;
    }
    seed_error = micros_ramfs_seed_validate(
        seed_image,
        seed_image_size,
        &plan
    );
    if (seed_error != MICROS_RAMFS_SEED_OK) {
        return MICROS_RAMFS_CORE_ERROR_SEED;
    }

    ramfs_zero_bytes(state, sizeof(*state));
    state->initialization_magic = MICROS_RAMFS_INITIALIZATION_MAGIC;
    state->vfs_endpoint = vfs_endpoint;
    state->phase = MICROS_RAMFS_PHASE_READY_UNMOUNTED;
    state->node_count = plan.entry_count;
    state->allocated_block_count = plan.allocated_block_count;
    for (index = 0; index < MICROS_RAMFS_NODE_CAPACITY; ++index) {
        ramfs_initialize_empty_node(&state->nodes[index]);
    }
    for (index = 0; index < MICROS_RAMFS_BLOCK_CAPACITY; ++index) {
        state->block_owner[index] = MICROS_RAMFS_NODE_NONE;
        state->block_logical[index] = MICROS_RAMFS_BLOCK_NONE;
    }

    for (index = 0; index < plan.entry_count; ++index) {
        const uint8_t *entry = &seed_image[
            MICROS_RAMFS_SEED_HEADER_SIZE
            + index * MICROS_RAMFS_SEED_ENTRY_SIZE
        ];
        struct micros_ramfs_node *node = &state->nodes[index];
        uint16_t parent = ramfs_read_u16_le(&entry[0]);
        uint32_t data_offset = ramfs_read_u32_le(&entry[8]);
        uint32_t data_size = ramfs_read_u32_le(&entry[12]);
        size_t logical;

        node->live = true;
        node->generation = 1;
        node->mode = ramfs_read_u32_le(&entry[4]);
        node->link_count = ramfs_node_is_directory(node) ? 2 : 1;
        node->name_length = ramfs_read_u16_le(&entry[2]);
        node->parent = micros_ramfs_handle(parent, 1);
        node->size = data_size;
        for (logical = 0; logical < node->name_length; ++logical) {
            node->name[logical] = entry[16 + logical];
        }
        for (
            logical = 0;
            logical < plan.block_count[index];
            ++logical
        ) {
            uint16_t block = (uint16_t)(
                plan.first_block[index] + logical
            );
            uint32_t copied = (uint32_t)(
                logical * MICROS_RAMFS_BLOCK_SIZE
            );
            uint32_t remaining = data_size - copied;
            uint32_t chunk = remaining < MICROS_RAMFS_BLOCK_SIZE
                ? remaining
                : MICROS_RAMFS_BLOCK_SIZE;
            size_t byte;

            node->blocks[logical] = block;
            state->block_owner[block] = (uint16_t)index;
            state->block_logical[block] = (uint16_t)logical;
            for (byte = 0; byte < chunk; ++byte) {
                state->data[block][byte] = seed_image[
                    MICROS_RAMFS_SEED_DATA_OFFSET
                    + data_offset
                    + copied
                    + byte
                ];
            }
        }
    }
    for (index = 1; index < plan.entry_count; ++index) {
        if (ramfs_node_is_directory(&state->nodes[index])) {
            uint16_t parent = (uint16_t)state->nodes[index].parent;

            ++state->nodes[parent].link_count;
        }
    }
    if (
        micros_ramfs_state_validate(state) != MICROS_RAMFS_CORE_OK
    ) {
        ramfs_zero_bytes(state, sizeof(*state));
        return MICROS_RAMFS_CORE_ERROR_INVARIANT;
    }
    return MICROS_RAMFS_CORE_OK;
}

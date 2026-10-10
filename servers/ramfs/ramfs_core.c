#include "servers/ramfs/ramfs_core.h"

#include <stddef.h>
#include <stdint.h>

#include "servers/ramfs/ramfs_seed.h"

#define MICROS_RAMFS_INITIALIZATION_MAGIC \
    UINT64_C(0x4d4943524f535246)

struct ramfs_result_fields {
    uint64_t node;
    uint64_t file_size;
    uint64_t position;
    uint32_t count;
    uint32_t mode;
};

static void zero_bytes(void *storage, size_t size)
{
    uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static bool bytes_are_zero(const uint8_t *bytes, size_t size)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static uint16_t read_u16_le(const uint8_t *bytes)
{
    return (uint16_t)(
        (uint16_t)bytes[0]
        | (uint16_t)bytes[1] << 8
    );
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

static bool endpoint_is_valid(micros_endpoint_t endpoint)
{
    uint32_t generation = endpoint >> MICROS_ENDPOINT_SLOT_BITS;

    return (
        endpoint != MICROS_ENDPOINT_NONE
        && endpoint != MICROS_ENDPOINT_ANY
        && generation != 0
        && generation <= MICROS_ENDPOINT_GENERATION_MAX
    );
}

static bool request_type_is_known(uint32_t type)
{
    return (
        type >= MICROS_RAMFS_MESSAGE_MOUNT
        && type <= MICROS_RAMFS_MESSAGE_PUTNODE
    );
}

static bool result_is_defined(enum micros_ramfs_result result)
{
    return (
        result >= MICROS_RAMFS_RESULT_REFERENCE
        && result <= MICROS_RAMFS_RESULT_OK
    );
}

static bool mode_is_canonical(uint32_t mode)
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

static bool node_is_directory(const struct micros_ramfs_node *node)
{
    return (
        node->mode & MICROS_RAMFS_MODE_TYPE_MASK
    ) == MICROS_RAMFS_MODE_DIRECTORY;
}

static bool node_is_regular(const struct micros_ramfs_node *node)
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

static bool decode_handle(
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

static void initialize_empty_node(struct micros_ramfs_node *node)
{
    size_t index;

    zero_bytes(node, sizeof(*node));
    for (index = 0; index < MICROS_RAMFS_BLOCK_CAPACITY; ++index) {
        node->blocks[index] = MICROS_RAMFS_BLOCK_NONE;
    }
}

static uint64_t node_handle(
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

    if (!decode_handle(handle, &decoded_slot, &generation)) {
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

static enum micros_ramfs_result resolve_referenced_node(
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

static bool names_equal(
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

static bool name_equals_bytes(
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

static enum micros_ramfs_core_error validate_state(
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
        || !endpoint_is_valid(state->vfs_endpoint)
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
            && !bytes_are_zero(
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
        size_t logical;
        size_t sibling;

        if (!node->live) {
            if (
                node->generation != 0
                || node->mode != 0
                || node->link_count != 0
                || node->reference_count != 0
                || node->name_length != 0
                || node->reserved1 != 0
                || node->parent != 0
                || node->size != 0
                || !bytes_are_zero(
                    node->reserved0,
                    sizeof(node->reserved0)
                )
                || !bytes_are_zero(node->name, sizeof(node->name))
            ) {
                return MICROS_RAMFS_CORE_ERROR_INVARIANT;
            }
            for (
                logical = 0;
                logical < MICROS_RAMFS_BLOCK_CAPACITY;
                ++logical
            ) {
                if (node->blocks[logical] != MICROS_RAMFS_BLOCK_NONE) {
                    return MICROS_RAMFS_CORE_ERROR_INVARIANT;
                }
            }
            continue;
        }

        ++live_count;
        if (
            node->generation == 0
            || !mode_is_canonical(node->mode)
            || node->reserved1 != 0
            || !bytes_are_zero(
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

        if (index == 0) {
            if (
                node->generation != 1
                || !node_is_directory(node)
                || node->parent != node_handle(0, node)
                || node->name_length != 0
                || !bytes_are_zero(node->name, sizeof(node->name))
            ) {
                return MICROS_RAMFS_CORE_ERROR_INVARIANT;
            }
        } else {
            uint16_t parent_slot;
            uint32_t parent_generation;

            if (
                node->name_length == 0
                || node->name_length > MICROS_RAMFS_NAME_MAX
                || !decode_handle(
                    node->parent,
                    &parent_slot,
                    &parent_generation
                )
                || parent_slot >= index
                || !state->nodes[parent_slot].live
                || state->nodes[parent_slot].generation
                    != parent_generation
                || !node_is_directory(&state->nodes[parent_slot])
            ) {
                return MICROS_RAMFS_CORE_ERROR_INVARIANT;
            }
            for (sibling = 0; sibling < node->name_length; ++sibling) {
                if (
                    node->name[sibling] == 0
                    || node->name[sibling] == '/'
                ) {
                    return MICROS_RAMFS_CORE_ERROR_INVARIANT;
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
                || !bytes_are_zero(
                    &node->name[node->name_length],
                    MICROS_RAMFS_NAME_MAX - node->name_length
                )
            ) {
                return MICROS_RAMFS_CORE_ERROR_INVARIANT;
            }
            for (sibling = 1; sibling < index; ++sibling) {
                const struct micros_ramfs_node *candidate =
                    &state->nodes[sibling];

                if (
                    candidate->live
                    && candidate->parent == node->parent
                    && names_equal(candidate, node)
                ) {
                    return MICROS_RAMFS_CORE_ERROR_INVARIANT;
                }
            }
        }

        if (node_is_directory(node)) {
            if (node->size != 0) {
                return MICROS_RAMFS_CORE_ERROR_INVARIANT;
            }
            for (
                logical = 0;
                logical < MICROS_RAMFS_BLOCK_CAPACITY;
                ++logical
            ) {
                if (node->blocks[logical] != MICROS_RAMFS_BLOCK_NONE) {
                    return MICROS_RAMFS_CORE_ERROR_INVARIANT;
                }
            }
        } else {
            if (
                !node_is_regular(node)
                || node->size > MICROS_RAMFS_FILE_SIZE_MAX
            ) {
                return MICROS_RAMFS_CORE_ERROR_INVARIANT;
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
                    return MICROS_RAMFS_CORE_ERROR_INVARIANT;
                }
                seen_blocks[block] = true;
            }
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

    for (index = 0; index < MICROS_RAMFS_NODE_CAPACITY; ++index) {
        const struct micros_ramfs_node *node = &state->nodes[index];
        uint32_t expected_links;
        size_t child;

        if (!node->live) {
            continue;
        }
        expected_links = node_is_directory(node) ? 2 : 1;
        if (node_is_directory(node)) {
            for (child = 1; child < MICROS_RAMFS_NODE_CAPACITY; ++child) {
                if (
                    state->nodes[child].live
                    && node_is_directory(&state->nodes[child])
                    && state->nodes[child].parent
                        == node_handle((uint16_t)index, node)
                ) {
                    ++expected_links;
                }
            }
        }
        if (node->link_count != expected_links) {
            return MICROS_RAMFS_CORE_ERROR_INVARIANT;
        }
    }

    for (index = 0; index < MICROS_RAMFS_BLOCK_CAPACITY; ++index) {
        uint16_t owner = state->block_owner[index];
        uint16_t logical = state->block_logical[index];

        if (owner == MICROS_RAMFS_NODE_NONE) {
            if (
                logical != MICROS_RAMFS_BLOCK_NONE
                || seen_blocks[index]
                || !bytes_are_zero(
                    state->data[index],
                    MICROS_RAMFS_BLOCK_SIZE
                )
            ) {
                return MICROS_RAMFS_CORE_ERROR_INVARIANT;
            }
            continue;
        }
        if (
            owner >= MICROS_RAMFS_NODE_CAPACITY
            || logical >= MICROS_RAMFS_BLOCK_CAPACITY
            || !seen_blocks[index]
            || !state->nodes[owner].live
            || !node_is_regular(&state->nodes[owner])
            || state->nodes[owner].blocks[logical] != index
        ) {
            return MICROS_RAMFS_CORE_ERROR_INVARIANT;
        }
        ++used_blocks;
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
    return validate_state(state, true);
}

enum micros_ramfs_protocol_status micros_ramfs_decode_request(
    const struct micros_ipc_message *message,
    struct micros_ramfs_request *request
)
{
    struct micros_ramfs_request decoded;
    uint32_t version;
    uint32_t flags;

    if (message == NULL || request == NULL) {
        return MICROS_RAMFS_PROTOCOL_INVARIANT;
    }
    if (!request_type_is_known(message->type)) {
        return MICROS_RAMFS_PROTOCOL_BAD_TYPE;
    }
    version = read_u32_le(&message->payload[0]);
    if (version != MICROS_RAMFS_PROTOCOL_VERSION) {
        return MICROS_RAMFS_PROTOCOL_BAD_VERSION;
    }
    flags = read_u32_le(&message->payload[4]);
    if (flags != 0 || message->reply_token == 0) {
        return (
            message->reply_token == 0
                ? MICROS_RAMFS_PROTOCOL_INVARIANT
                : MICROS_RAMFS_PROTOCOL_MALFORMED
        );
    }

    zero_bytes(&decoded, sizeof(decoded));
    decoded.type = message->type;
    decoded.source = message->source;
    decoded.reply_token = message->reply_token;

    switch (message->type) {
    case MICROS_RAMFS_MESSAGE_MOUNT:
        if (!bytes_are_zero(&message->payload[8], 40)) {
            return MICROS_RAMFS_PROTOCOL_MALFORMED;
        }
        break;
    case MICROS_RAMFS_MESSAGE_LOOKUP:
        if (!bytes_are_zero(&message->payload[40], 8)) {
            return MICROS_RAMFS_PROTOCOL_MALFORMED;
        }
        decoded.start = read_u64_le(&message->payload[8]);
        decoded.root = read_u64_le(&message->payload[16]);
        decoded.grant = read_u32_le(&message->payload[24]);
        decoded.length = read_u32_le(&message->payload[28]);
        decoded.grant_offset = read_u64_le(&message->payload[32]);
        break;
    case MICROS_RAMFS_MESSAGE_CREATE:
    case MICROS_RAMFS_MESSAGE_MKDIR:
        if (!bytes_are_zero(&message->payload[36], 12)) {
            return MICROS_RAMFS_PROTOCOL_MALFORMED;
        }
        decoded.node = read_u64_le(&message->payload[8]);
        decoded.mode = read_u32_le(&message->payload[16]);
        decoded.grant = read_u32_le(&message->payload[20]);
        decoded.grant_offset = read_u64_le(&message->payload[24]);
        decoded.length = read_u32_le(&message->payload[32]);
        break;
    case MICROS_RAMFS_MESSAGE_READ:
    case MICROS_RAMFS_MESSAGE_WRITE:
        if (!bytes_are_zero(&message->payload[40], 8)) {
            return MICROS_RAMFS_PROTOCOL_MALFORMED;
        }
        decoded.node = read_u64_le(&message->payload[8]);
        decoded.file_offset = read_u64_le(&message->payload[16]);
        decoded.grant = read_u32_le(&message->payload[24]);
        decoded.count = read_u32_le(&message->payload[28]);
        decoded.grant_offset = read_u64_le(&message->payload[32]);
        break;
    case MICROS_RAMFS_MESSAGE_GETDENTS:
        if (!bytes_are_zero(&message->payload[40], 8)) {
            return MICROS_RAMFS_PROTOCOL_MALFORMED;
        }
        decoded.node = read_u64_le(&message->payload[8]);
        decoded.cursor = read_u64_le(&message->payload[16]);
        decoded.grant = read_u32_le(&message->payload[24]);
        decoded.count = read_u32_le(&message->payload[28]);
        decoded.grant_offset = read_u64_le(&message->payload[32]);
        break;
    case MICROS_RAMFS_MESSAGE_PUTNODE:
        if (!bytes_are_zero(&message->payload[20], 28)) {
            return MICROS_RAMFS_PROTOCOL_MALFORMED;
        }
        decoded.node = read_u64_le(&message->payload[8]);
        decoded.count = read_u32_le(&message->payload[16]);
        break;
    default:
        return MICROS_RAMFS_PROTOCOL_INVARIANT;
    }
    *request = decoded;
    return MICROS_RAMFS_PROTOCOL_OK;
}

enum micros_ramfs_core_error micros_ramfs_build_result(
    uint32_t request_type,
    enum micros_ramfs_result result,
    uint64_t node,
    uint64_t file_size,
    uint64_t position,
    uint32_t count,
    uint32_t mode,
    struct micros_ipc_message *message
)
{
    struct micros_ipc_message built;

    if (
        message == NULL
        || !result_is_defined(result)
        || (
            result != MICROS_RAMFS_RESULT_OK
            && (
                node != 0
                || file_size != 0
                || position != 0
                || count != 0
                || mode != 0
            )
        )
    ) {
        return MICROS_RAMFS_CORE_ERROR_ARGUMENT;
    }
    zero_bytes(&built, sizeof(built));
    built.type = MICROS_RAMFS_MESSAGE_RESULT;
    write_u32_le(
        &built.payload[0],
        MICROS_RAMFS_PROTOCOL_VERSION
    );
    write_u32_le(&built.payload[4], request_type);
    write_u32_le(&built.payload[8], (uint32_t)(int32_t)result);
    write_u64_le(&built.payload[16], node);
    write_u64_le(&built.payload[24], file_size);
    write_u64_le(&built.payload[32], position);
    write_u32_le(&built.payload[40], count);
    write_u32_le(&built.payload[44], mode);
    *message = built;
    return MICROS_RAMFS_CORE_OK;
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
        || !endpoint_is_valid(vfs_endpoint)
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

    zero_bytes(state, sizeof(*state));
    state->initialization_magic = MICROS_RAMFS_INITIALIZATION_MAGIC;
    state->vfs_endpoint = vfs_endpoint;
    state->phase = MICROS_RAMFS_PHASE_READY_UNMOUNTED;
    state->node_count = plan.entry_count;
    state->allocated_block_count = plan.allocated_block_count;
    for (index = 0; index < MICROS_RAMFS_NODE_CAPACITY; ++index) {
        initialize_empty_node(&state->nodes[index]);
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
        uint16_t parent = read_u16_le(&entry[0]);
        uint32_t data_offset = read_u32_le(&entry[8]);
        uint32_t data_size = read_u32_le(&entry[12]);
        size_t logical;

        node->live = true;
        node->generation = 1;
        node->mode = read_u32_le(&entry[4]);
        node->link_count = node_is_directory(node) ? 2 : 1;
        node->name_length = read_u16_le(&entry[2]);
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
        if (node_is_directory(&state->nodes[index])) {
            uint16_t parent = (uint16_t)state->nodes[index].parent;

            ++state->nodes[parent].link_count;
        }
    }
    if (
        micros_ramfs_state_validate(state) != MICROS_RAMFS_CORE_OK
    ) {
        zero_bytes(state, sizeof(*state));
        return MICROS_RAMFS_CORE_ERROR_INVARIANT;
    }
    return MICROS_RAMFS_CORE_OK;
}

static enum micros_ramfs_core_error prepare_reply(
    const struct micros_ipc_message *request,
    enum micros_ramfs_result result,
    const struct ramfs_result_fields *fields,
    struct micros_ramfs_reply_action *action
)
{
    struct micros_ramfs_reply_action candidate;
    struct ramfs_result_fields empty = {0};

    if (result != MICROS_RAMFS_RESULT_OK) {
        fields = &empty;
    }
    zero_bytes(&candidate, sizeof(candidate));
    if (
        request->reply_token == 0
        || micros_ramfs_build_result(
            request->type,
            result,
            fields->node,
            fields->file_size,
            fields->position,
            fields->count,
            fields->mode,
            &candidate.message
        ) != MICROS_RAMFS_CORE_OK
    ) {
        return MICROS_RAMFS_CORE_ERROR_INVARIANT;
    }
    candidate.active = true;
    candidate.reply_token = request->reply_token;
    *action = candidate;
    return MICROS_RAMFS_CORE_OK;
}

static enum micros_ramfs_copy_result copy_grant(
    const struct micros_ramfs_state *state,
    const struct micros_ramfs_io *io,
    enum micros_ramfs_copy_direction direction,
    micros_grant_t grant,
    uint64_t grant_offset,
    uint8_t *local,
    size_t length
)
{
    if (io == NULL || io->copy == NULL) {
        return MICROS_RAMFS_COPY_INVARIANT;
    }
    return io->copy(
        io->context,
        direction,
        state->vfs_endpoint,
        grant,
        grant_offset,
        local,
        length
    );
}

static bool find_free_node(
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

static bool find_free_block(
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

static bool find_child(
    const struct micros_ramfs_state *state,
    uint16_t parent_slot,
    const uint8_t *name,
    size_t name_length,
    uint16_t *child_slot
)
{
    uint64_t parent = node_handle(
        parent_slot,
        &state->nodes[parent_slot]
    );
    uint16_t index;

    for (index = 1; index < MICROS_RAMFS_NODE_CAPACITY; ++index) {
        if (
            state->nodes[index].live
            && state->nodes[index].parent == parent
            && name_equals_bytes(
                &state->nodes[index],
                name,
                name_length
            )
        ) {
            *child_slot = index;
            return true;
        }
    }
    return false;
}

static bool is_ancestor(
    const struct micros_ramfs_state *state,
    uint16_t ancestor,
    uint16_t node
)
{
    size_t depth;

    for (depth = 0; depth < MICROS_RAMFS_NODE_CAPACITY; ++depth) {
        uint16_t parent;
        uint32_t generation;

        if (node == ancestor) {
            return true;
        }
        if (
            !decode_handle(
                state->nodes[node].parent,
                &parent,
                &generation
            )
            || !state->nodes[parent].live
            || state->nodes[parent].generation != generation
            || parent == node
        ) {
            return false;
        }
        node = parent;
    }
    return false;
}

static bool path_components_are_bounded(
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

static enum micros_ramfs_result handle_mount(
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
    fields->node = node_handle(0, root);
    fields->mode = root->mode;
    return MICROS_RAMFS_RESULT_OK;
}

static enum micros_ramfs_core_error handle_lookup(
    struct micros_ramfs_state *state,
    const struct micros_ramfs_request *request,
    const struct micros_ramfs_io *io,
    struct ramfs_result_fields *fields,
    enum micros_ramfs_result *result
)
{
    const struct micros_ramfs_node *start;
    const struct micros_ramfs_node *root;
    uint16_t start_slot;
    uint16_t root_slot;
    uint16_t current_slot;
    size_t path_size;
    size_t position;
    bool absolute;
    bool trailing_separator = false;
    enum micros_ramfs_copy_result copy_result;

    *result = resolve_referenced_node(
        state,
        request->start,
        &start_slot,
        &start
    );
    if (*result != MICROS_RAMFS_RESULT_OK) {
        return MICROS_RAMFS_CORE_OK;
    }
    *result = resolve_referenced_node(
        state,
        request->root,
        &root_slot,
        &root
    );
    if (*result != MICROS_RAMFS_RESULT_OK) {
        return MICROS_RAMFS_CORE_OK;
    }
    if (!node_is_directory(root)) {
        *result = MICROS_RAMFS_RESULT_NOT_DIRECTORY;
        return MICROS_RAMFS_CORE_OK;
    }
    if (!is_ancestor(state, root_slot, start_slot)) {
        *result = MICROS_RAMFS_RESULT_RANGE;
        return MICROS_RAMFS_CORE_OK;
    }
    if (
        request->length == 0
        || request->length > MICROS_RAMFS_PATH_MAX
        || UINT64_MAX - request->grant_offset < request->length
    ) {
        *result = MICROS_RAMFS_RESULT_RANGE;
        return MICROS_RAMFS_CORE_OK;
    }

    copy_result = copy_grant(
        state,
        io,
        MICROS_RAMFS_COPY_FROM_VFS,
        request->grant,
        request->grant_offset,
        state->scratch,
        request->length
    );
    if (copy_result == MICROS_RAMFS_COPY_INVARIANT) {
        zero_bytes(state->scratch, sizeof(state->scratch));
        return MICROS_RAMFS_CORE_ERROR_INVARIANT;
    }
    if (copy_result == MICROS_RAMFS_COPY_REJECTED) {
        zero_bytes(state->scratch, sizeof(state->scratch));
        *result = MICROS_RAMFS_RESULT_GRANT;
        return MICROS_RAMFS_CORE_OK;
    }

    path_size = request->length - 1;
    if (
        state->scratch[path_size] != 0
        || !bytes_are_zero(
            &state->scratch[path_size],
            1
        )
    ) {
        zero_bytes(state->scratch, sizeof(state->scratch));
        *result = MICROS_RAMFS_RESULT_MALFORMED;
        return MICROS_RAMFS_CORE_OK;
    }
    for (position = 0; position < path_size; ++position) {
        if (state->scratch[position] == 0) {
            zero_bytes(state->scratch, sizeof(state->scratch));
            *result = MICROS_RAMFS_RESULT_MALFORMED;
            return MICROS_RAMFS_CORE_OK;
        }
    }
    if (!path_components_are_bounded(state->scratch, path_size)) {
        zero_bytes(state->scratch, sizeof(state->scratch));
        *result = MICROS_RAMFS_RESULT_RANGE;
        return MICROS_RAMFS_CORE_OK;
    }

    absolute = path_size != 0 && state->scratch[0] == '/';
    current_slot = absolute ? root_slot : start_slot;
    position = 0;
    if (absolute) {
        while (
            position < path_size
            && state->scratch[position] == '/'
        ) {
            ++position;
        }
        trailing_separator = position == path_size;
    }
    if (!absolute && path_size == 0) {
        if (!node_is_directory(&state->nodes[current_slot])) {
            *result = MICROS_RAMFS_RESULT_NOT_DIRECTORY;
            goto lookup_done;
        }
    }

    while (position < path_size) {
        size_t component_start;
        size_t component_length;
        uint16_t child_slot;

        while (
            position < path_size
            && state->scratch[position] == '/'
        ) {
            ++position;
        }
        if (position == path_size) {
            trailing_separator = true;
            break;
        }
        component_start = position;
        while (
            position < path_size
            && state->scratch[position] != '/'
        ) {
            ++position;
        }
        component_length = position - component_start;
        if (!node_is_directory(&state->nodes[current_slot])) {
            *result = MICROS_RAMFS_RESULT_NOT_DIRECTORY;
            goto lookup_done;
        }
        if (
            component_length == 1
            && state->scratch[component_start] == '.'
        ) {
        } else if (
            component_length == 2
            && state->scratch[component_start] == '.'
            && state->scratch[component_start + 1] == '.'
        ) {
            if (current_slot != root_slot) {
                current_slot =
                    (uint16_t)state->nodes[current_slot].parent;
            }
        } else if (
            !find_child(
                state,
                current_slot,
                &state->scratch[component_start],
                component_length,
                &child_slot
            )
        ) {
            *result = MICROS_RAMFS_RESULT_NOT_FOUND;
            goto lookup_done;
        } else {
            current_slot = child_slot;
        }
        trailing_separator = (
            position < path_size
            && state->scratch[position] == '/'
        );
    }

    if (
        trailing_separator
        && !node_is_directory(&state->nodes[current_slot])
    ) {
        *result = MICROS_RAMFS_RESULT_NOT_DIRECTORY;
        goto lookup_done;
    }
    if (state->nodes[current_slot].reference_count == UINT32_MAX) {
        *result = MICROS_RAMFS_RESULT_NO_SPACE;
        goto lookup_done;
    }
    if (
        validate_state(state, false) != MICROS_RAMFS_CORE_OK
    ) {
        zero_bytes(state->scratch, sizeof(state->scratch));
        return MICROS_RAMFS_CORE_ERROR_INVARIANT;
    }
    zero_bytes(state->scratch, sizeof(state->scratch));
    ++state->nodes[current_slot].reference_count;
    fields->node = node_handle(
        current_slot,
        &state->nodes[current_slot]
    );
    fields->file_size = state->nodes[current_slot].size;
    fields->mode = state->nodes[current_slot].mode;
    *result = MICROS_RAMFS_RESULT_OK;
    return MICROS_RAMFS_CORE_OK;

lookup_done:
    zero_bytes(state->scratch, sizeof(state->scratch));
    return MICROS_RAMFS_CORE_OK;
}

static enum micros_ramfs_core_error handle_create_or_mkdir(
    struct micros_ramfs_state *state,
    const struct micros_ramfs_request *request,
    const struct micros_ramfs_io *io,
    struct ramfs_result_fields *fields,
    enum micros_ramfs_result *result
)
{
    const struct micros_ramfs_node *resolved_parent;
    struct micros_ramfs_node *parent;
    uint16_t parent_slot;
    uint16_t free_slot;
    uint16_t existing_slot;
    size_t name_length;
    size_t index;
    bool mkdir_request =
        request->type == MICROS_RAMFS_MESSAGE_MKDIR;
    enum micros_ramfs_copy_result copy_result;

    *result = resolve_referenced_node(
        state,
        request->node,
        &parent_slot,
        &resolved_parent
    );
    if (*result != MICROS_RAMFS_RESULT_OK) {
        return MICROS_RAMFS_CORE_OK;
    }
    if (!node_is_directory(resolved_parent)) {
        *result = MICROS_RAMFS_RESULT_NOT_DIRECTORY;
        return MICROS_RAMFS_CORE_OK;
    }
    if (
        !mode_is_canonical(request->mode)
        || (
            mkdir_request
                ? (
                    request->mode & MICROS_RAMFS_MODE_TYPE_MASK
                ) != MICROS_RAMFS_MODE_DIRECTORY
                : (
                    request->mode & MICROS_RAMFS_MODE_TYPE_MASK
                ) != MICROS_RAMFS_MODE_REGULAR
        )
    ) {
        *result = MICROS_RAMFS_RESULT_MALFORMED;
        return MICROS_RAMFS_CORE_OK;
    }
    if (
        request->length < 2
        || request->length > MICROS_RAMFS_NAME_MAX + 1
        || UINT64_MAX - request->grant_offset < request->length
    ) {
        *result = MICROS_RAMFS_RESULT_RANGE;
        return MICROS_RAMFS_CORE_OK;
    }
    if (!find_free_node(state, &free_slot)) {
        *result = MICROS_RAMFS_RESULT_NO_SPACE;
        return MICROS_RAMFS_CORE_OK;
    }
    if (
        mkdir_request
        && resolved_parent->link_count == UINT32_MAX
    ) {
        *result = MICROS_RAMFS_RESULT_NO_SPACE;
        return MICROS_RAMFS_CORE_OK;
    }

    copy_result = copy_grant(
        state,
        io,
        MICROS_RAMFS_COPY_FROM_VFS,
        request->grant,
        request->grant_offset,
        state->scratch,
        request->length
    );
    if (copy_result == MICROS_RAMFS_COPY_INVARIANT) {
        zero_bytes(state->scratch, sizeof(state->scratch));
        return MICROS_RAMFS_CORE_ERROR_INVARIANT;
    }
    if (copy_result == MICROS_RAMFS_COPY_REJECTED) {
        zero_bytes(state->scratch, sizeof(state->scratch));
        *result = MICROS_RAMFS_RESULT_GRANT;
        return MICROS_RAMFS_CORE_OK;
    }

    name_length = request->length - 1;
    if (state->scratch[name_length] != 0) {
        *result = MICROS_RAMFS_RESULT_MALFORMED;
        goto create_done;
    }
    for (index = 0; index < name_length; ++index) {
        if (
            state->scratch[index] == 0
            || state->scratch[index] == '/'
        ) {
            *result = MICROS_RAMFS_RESULT_MALFORMED;
            goto create_done;
        }
    }
    if (
        (
            name_length == 1
            && state->scratch[0] == '.'
        )
        || (
            name_length == 2
            && state->scratch[0] == '.'
            && state->scratch[1] == '.'
        )
    ) {
        *result = MICROS_RAMFS_RESULT_MALFORMED;
        goto create_done;
    }
    if (
        find_child(
            state,
            parent_slot,
            state->scratch,
            name_length,
            &existing_slot
        )
    ) {
        *result = MICROS_RAMFS_RESULT_EXISTS;
        goto create_done;
    }
    if (
        validate_state(state, false) != MICROS_RAMFS_CORE_OK
    ) {
        zero_bytes(state->scratch, sizeof(state->scratch));
        return MICROS_RAMFS_CORE_ERROR_INVARIANT;
    }

    parent = &state->nodes[parent_slot];
    initialize_empty_node(&state->nodes[free_slot]);
    state->nodes[free_slot].live = true;
    state->nodes[free_slot].generation = 1;
    state->nodes[free_slot].mode = request->mode;
    state->nodes[free_slot].link_count = mkdir_request ? 2 : 1;
    state->nodes[free_slot].reference_count = mkdir_request ? 0 : 1;
    state->nodes[free_slot].name_length = (uint16_t)name_length;
    state->nodes[free_slot].parent = node_handle(
        parent_slot,
        parent
    );
    for (index = 0; index < name_length; ++index) {
        state->nodes[free_slot].name[index] = state->scratch[index];
    }
    if (mkdir_request) {
        ++parent->link_count;
    }
    ++state->node_count;
    zero_bytes(state->scratch, sizeof(state->scratch));
    if (!mkdir_request) {
        fields->node = node_handle(
            free_slot,
            &state->nodes[free_slot]
        );
        fields->mode = state->nodes[free_slot].mode;
    }
    *result = MICROS_RAMFS_RESULT_OK;
    return MICROS_RAMFS_CORE_OK;

create_done:
    zero_bytes(state->scratch, sizeof(state->scratch));
    return MICROS_RAMFS_CORE_OK;
}

static void stage_read(
    const struct micros_ramfs_state *state,
    const struct micros_ramfs_node *node,
    uint64_t offset,
    size_t count,
    uint8_t *output
)
{
    size_t index;

    for (index = 0; index < count; ++index) {
        uint64_t file_position = offset + index;
        size_t logical = (size_t)(
            file_position / MICROS_RAMFS_BLOCK_SIZE
        );
        size_t block_offset = (size_t)(
            file_position % MICROS_RAMFS_BLOCK_SIZE
        );
        uint16_t block = node->blocks[logical];

        output[index] = (
            block == MICROS_RAMFS_BLOCK_NONE
                ? 0
                : state->data[block][block_offset]
        );
    }
}

static enum micros_ramfs_core_error handle_read(
    struct micros_ramfs_state *state,
    const struct micros_ramfs_request *request,
    const struct micros_ramfs_io *io,
    struct ramfs_result_fields *fields,
    enum micros_ramfs_result *result
)
{
    const struct micros_ramfs_node *node;
    uint16_t slot;
    uint64_t available;
    uint32_t transferred;
    enum micros_ramfs_copy_result copy_result;

    *result = resolve_referenced_node(
        state,
        request->node,
        &slot,
        &node
    );
    if (*result != MICROS_RAMFS_RESULT_OK) {
        return MICROS_RAMFS_CORE_OK;
    }
    if (node_is_directory(node)) {
        *result = MICROS_RAMFS_RESULT_IS_DIRECTORY;
        return MICROS_RAMFS_CORE_OK;
    }
    if (
        request->count == 0
        || request->count > MICROS_RAMFS_TRANSFER_MAX
        || request->file_offset > MICROS_RAMFS_FILE_SIZE_MAX
        || UINT64_MAX - request->grant_offset < request->count
    ) {
        *result = MICROS_RAMFS_RESULT_RANGE;
        return MICROS_RAMFS_CORE_OK;
    }
    available = (
        request->file_offset < node->size
            ? node->size - request->file_offset
            : 0
    );
    transferred = (
        available < request->count
            ? (uint32_t)available
            : request->count
    );
    if (transferred != 0) {
        stage_read(
            state,
            node,
            request->file_offset,
            transferred,
            state->scratch
        );
        copy_result = copy_grant(
            state,
            io,
            MICROS_RAMFS_COPY_TO_VFS,
            request->grant,
            request->grant_offset,
            state->scratch,
            transferred
        );
        zero_bytes(state->scratch, sizeof(state->scratch));
        if (copy_result == MICROS_RAMFS_COPY_INVARIANT) {
            return MICROS_RAMFS_CORE_ERROR_INVARIANT;
        }
        if (copy_result == MICROS_RAMFS_COPY_REJECTED) {
            *result = MICROS_RAMFS_RESULT_GRANT;
            return MICROS_RAMFS_CORE_OK;
        }
    }
    fields->file_size = node->size;
    fields->position = request->file_offset + transferred;
    fields->count = transferred;
    *result = MICROS_RAMFS_RESULT_OK;
    return MICROS_RAMFS_CORE_OK;
}

static enum micros_ramfs_core_error handle_write(
    struct micros_ramfs_state *state,
    const struct micros_ramfs_request *request,
    const struct micros_ramfs_io *io,
    struct ramfs_result_fields *fields,
    enum micros_ramfs_result *result
)
{
    const struct micros_ramfs_node *resolved_node;
    struct micros_ramfs_node *node;
    uint16_t slot;
    uint64_t write_end;
    size_t first_logical;
    size_t last_logical;
    size_t missing_blocks = 0;
    size_t logical;
    enum micros_ramfs_copy_result copy_result;

    *result = resolve_referenced_node(
        state,
        request->node,
        &slot,
        &resolved_node
    );
    if (*result != MICROS_RAMFS_RESULT_OK) {
        return MICROS_RAMFS_CORE_OK;
    }
    if (node_is_directory(resolved_node)) {
        *result = MICROS_RAMFS_RESULT_IS_DIRECTORY;
        return MICROS_RAMFS_CORE_OK;
    }
    if (
        request->count == 0
        || request->count > MICROS_RAMFS_TRANSFER_MAX
        || request->file_offset > MICROS_RAMFS_FILE_SIZE_MAX
        || UINT64_MAX - request->file_offset < request->count
        || request->file_offset + request->count
            > MICROS_RAMFS_FILE_SIZE_MAX
        || UINT64_MAX - request->grant_offset < request->count
    ) {
        *result = MICROS_RAMFS_RESULT_RANGE;
        return MICROS_RAMFS_CORE_OK;
    }
    write_end = request->file_offset + request->count;
    first_logical = (size_t)(
        request->file_offset / MICROS_RAMFS_BLOCK_SIZE
    );
    last_logical = (size_t)(
        (write_end - 1) / MICROS_RAMFS_BLOCK_SIZE
    );
    for (logical = first_logical; logical <= last_logical; ++logical) {
        if (
            resolved_node->blocks[logical]
                == MICROS_RAMFS_BLOCK_NONE
        ) {
            ++missing_blocks;
        }
    }
    if (
        missing_blocks
        > MICROS_RAMFS_BLOCK_CAPACITY
            - state->allocated_block_count
    ) {
        *result = MICROS_RAMFS_RESULT_NO_SPACE;
        return MICROS_RAMFS_CORE_OK;
    }

    copy_result = copy_grant(
        state,
        io,
        MICROS_RAMFS_COPY_FROM_VFS,
        request->grant,
        request->grant_offset,
        state->scratch,
        request->count
    );
    if (copy_result == MICROS_RAMFS_COPY_INVARIANT) {
        zero_bytes(state->scratch, sizeof(state->scratch));
        return MICROS_RAMFS_CORE_ERROR_INVARIANT;
    }
    if (copy_result == MICROS_RAMFS_COPY_REJECTED) {
        zero_bytes(state->scratch, sizeof(state->scratch));
        *result = MICROS_RAMFS_RESULT_GRANT;
        return MICROS_RAMFS_CORE_OK;
    }
    if (
        validate_state(state, false) != MICROS_RAMFS_CORE_OK
    ) {
        zero_bytes(state->scratch, sizeof(state->scratch));
        return MICROS_RAMFS_CORE_ERROR_INVARIANT;
    }

    node = &state->nodes[slot];
    for (logical = first_logical; logical <= last_logical; ++logical) {
        if (node->blocks[logical] == MICROS_RAMFS_BLOCK_NONE) {
            uint16_t block;

            if (!find_free_block(state, &block)) {
                zero_bytes(state->scratch, sizeof(state->scratch));
                return MICROS_RAMFS_CORE_ERROR_INVARIANT;
            }
            zero_bytes(
                state->data[block],
                MICROS_RAMFS_BLOCK_SIZE
            );
            node->blocks[logical] = block;
            state->block_owner[block] = slot;
            state->block_logical[block] = (uint16_t)logical;
            ++state->allocated_block_count;
        }
    }
    for (logical = 0; logical < request->count; ++logical) {
        uint64_t file_position = request->file_offset + logical;
        size_t logical_block = (size_t)(
            file_position / MICROS_RAMFS_BLOCK_SIZE
        );
        size_t block_offset = (size_t)(
            file_position % MICROS_RAMFS_BLOCK_SIZE
        );
        uint16_t block = node->blocks[logical_block];

        state->data[block][block_offset] = state->scratch[logical];
    }
    if (write_end > node->size) {
        node->size = write_end;
    }
    zero_bytes(state->scratch, sizeof(state->scratch));
    fields->file_size = node->size;
    fields->position = write_end;
    fields->count = request->count;
    *result = MICROS_RAMFS_RESULT_OK;
    return MICROS_RAMFS_CORE_OK;
}

static void write_directory_record(
    uint8_t *record,
    uint64_t handle,
    uint32_t mode,
    const uint8_t *name,
    uint32_t name_length
)
{
    zero_bytes(record, MICROS_RAMFS_DIRECTORY_RECORD_SIZE);
    write_u64_le(&record[0], handle);
    write_u32_le(&record[8], mode);
    write_u32_le(&record[12], name_length);
    while (name_length != 0) {
        --name_length;
        record[16 + name_length] = name[name_length];
    }
}

static bool directory_entry_for_cursor(
    const struct micros_ramfs_state *state,
    uint16_t directory_slot,
    uint64_t cursor,
    uint64_t *handle,
    uint32_t *mode,
    const uint8_t **name,
    uint32_t *name_length
)
{
    static const uint8_t dot[] = ".";
    static const uint8_t dotdot[] = "..";
    const struct micros_ramfs_node *directory =
        &state->nodes[directory_slot];

    if (cursor == 0) {
        *handle = node_handle(directory_slot, directory);
        *mode = directory->mode;
        *name = dot;
        *name_length = 1;
        return true;
    }
    if (cursor == 1) {
        uint16_t parent_slot = (uint16_t)directory->parent;
        const struct micros_ramfs_node *parent =
            &state->nodes[parent_slot];

        *handle = node_handle(parent_slot, parent);
        *mode = parent->mode;
        *name = dotdot;
        *name_length = 2;
        return true;
    }
    if (cursor < MICROS_RAMFS_DIRECTORY_CURSOR_END) {
        uint16_t slot = (uint16_t)(cursor - 2);
        const struct micros_ramfs_node *candidate =
            &state->nodes[slot];

        if (
            candidate->live
            && candidate->parent
                == node_handle(directory_slot, directory)
            && slot != directory_slot
        ) {
            *handle = node_handle(slot, candidate);
            *mode = candidate->mode;
            *name = candidate->name;
            *name_length = candidate->name_length;
            return true;
        }
    }
    return false;
}

static enum micros_ramfs_core_error handle_getdents(
    struct micros_ramfs_state *state,
    const struct micros_ramfs_request *request,
    const struct micros_ramfs_io *io,
    struct ramfs_result_fields *fields,
    enum micros_ramfs_result *result
)
{
    const struct micros_ramfs_node *directory;
    uint16_t directory_slot;
    uint64_t cursor;
    uint32_t output_count = 0;
    enum micros_ramfs_copy_result copy_result;

    *result = resolve_referenced_node(
        state,
        request->node,
        &directory_slot,
        &directory
    );
    if (*result != MICROS_RAMFS_RESULT_OK) {
        return MICROS_RAMFS_CORE_OK;
    }
    if (!node_is_directory(directory)) {
        *result = MICROS_RAMFS_RESULT_NOT_DIRECTORY;
        return MICROS_RAMFS_CORE_OK;
    }
    if (
        request->cursor > MICROS_RAMFS_DIRECTORY_CURSOR_END
        || request->count < MICROS_RAMFS_DIRECTORY_RECORD_SIZE
        || request->count > MICROS_RAMFS_TRANSFER_MAX
        || (
            request->count % MICROS_RAMFS_DIRECTORY_RECORD_SIZE
        ) != 0
        || UINT64_MAX - request->grant_offset < request->count
    ) {
        *result = MICROS_RAMFS_RESULT_RANGE;
        return MICROS_RAMFS_CORE_OK;
    }
    if (request->cursor == MICROS_RAMFS_DIRECTORY_CURSOR_END) {
        fields->position = MICROS_RAMFS_DIRECTORY_CURSOR_END;
        *result = MICROS_RAMFS_RESULT_OK;
        return MICROS_RAMFS_CORE_OK;
    }

    cursor = request->cursor;
    while (cursor < MICROS_RAMFS_DIRECTORY_CURSOR_END) {
        uint64_t handle;
        uint32_t mode;
        const uint8_t *name;
        uint32_t name_length;

        if (
            !directory_entry_for_cursor(
                state,
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
        write_directory_record(
            &state->scratch[output_count],
            handle,
            mode,
            name,
            name_length
        );
        output_count += MICROS_RAMFS_DIRECTORY_RECORD_SIZE;
        ++cursor;
    }
    if (output_count != 0) {
        copy_result = copy_grant(
            state,
            io,
            MICROS_RAMFS_COPY_TO_VFS,
            request->grant,
            request->grant_offset,
            state->scratch,
            output_count
        );
        zero_bytes(state->scratch, sizeof(state->scratch));
        if (copy_result == MICROS_RAMFS_COPY_INVARIANT) {
            return MICROS_RAMFS_CORE_ERROR_INVARIANT;
        }
        if (copy_result == MICROS_RAMFS_COPY_REJECTED) {
            *result = MICROS_RAMFS_RESULT_GRANT;
            return MICROS_RAMFS_CORE_OK;
        }
    }
    fields->position = cursor;
    fields->count = output_count;
    *result = MICROS_RAMFS_RESULT_OK;
    return MICROS_RAMFS_CORE_OK;
}

static enum micros_ramfs_result handle_putnode(
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

static enum micros_ramfs_result protocol_result(
    enum micros_ramfs_protocol_status status
)
{
    switch (status) {
    case MICROS_RAMFS_PROTOCOL_BAD_TYPE:
        return MICROS_RAMFS_RESULT_BAD_TYPE;
    case MICROS_RAMFS_PROTOCOL_BAD_VERSION:
        return MICROS_RAMFS_RESULT_BAD_VERSION;
    case MICROS_RAMFS_PROTOCOL_MALFORMED:
        return MICROS_RAMFS_RESULT_MALFORMED;
    case MICROS_RAMFS_PROTOCOL_OK:
    case MICROS_RAMFS_PROTOCOL_INVARIANT:
    default:
        return MICROS_RAMFS_RESULT_STATE;
    }
}

enum micros_ramfs_core_error micros_ramfs_handle_call(
    struct micros_ramfs_state *state,
    const struct micros_ipc_message *message,
    const struct micros_ramfs_io *io,
    struct micros_ramfs_reply_action *action
)
{
    struct micros_ramfs_request request;
    struct ramfs_result_fields fields;
    enum micros_ramfs_protocol_status protocol_status;
    enum micros_ramfs_result result;
    enum micros_ramfs_core_error error;

    if (state == NULL || message == NULL || action == NULL) {
        return MICROS_RAMFS_CORE_ERROR_ARGUMENT;
    }
    if (
        micros_ramfs_state_validate(state) != MICROS_RAMFS_CORE_OK
    ) {
        return MICROS_RAMFS_CORE_ERROR_INVARIANT;
    }
    protocol_status = micros_ramfs_decode_request(message, &request);
    if (protocol_status == MICROS_RAMFS_PROTOCOL_INVARIANT) {
        return MICROS_RAMFS_CORE_ERROR_INVARIANT;
    }
    zero_bytes(&fields, sizeof(fields));
    if (protocol_status != MICROS_RAMFS_PROTOCOL_OK) {
        return prepare_reply(
            message,
            protocol_result(protocol_status),
            &fields,
            action
        );
    }
    if (request.source != state->vfs_endpoint) {
        return prepare_reply(
            message,
            MICROS_RAMFS_RESULT_CALLER,
            &fields,
            action
        );
    }
    if (
        (
            request.type == MICROS_RAMFS_MESSAGE_MOUNT
            && state->phase != MICROS_RAMFS_PHASE_READY_UNMOUNTED
        )
        || (
            request.type != MICROS_RAMFS_MESSAGE_MOUNT
            && state->phase != MICROS_RAMFS_PHASE_MOUNTED
        )
    ) {
        return prepare_reply(
            message,
            MICROS_RAMFS_RESULT_STATE,
            &fields,
            action
        );
    }

    result = MICROS_RAMFS_RESULT_OK;
    error = MICROS_RAMFS_CORE_OK;
    switch (request.type) {
    case MICROS_RAMFS_MESSAGE_MOUNT:
        result = handle_mount(state, &fields);
        break;
    case MICROS_RAMFS_MESSAGE_LOOKUP:
        error = handle_lookup(
            state,
            &request,
            io,
            &fields,
            &result
        );
        break;
    case MICROS_RAMFS_MESSAGE_CREATE:
    case MICROS_RAMFS_MESSAGE_MKDIR:
        error = handle_create_or_mkdir(
            state,
            &request,
            io,
            &fields,
            &result
        );
        break;
    case MICROS_RAMFS_MESSAGE_READ:
        error = handle_read(
            state,
            &request,
            io,
            &fields,
            &result
        );
        break;
    case MICROS_RAMFS_MESSAGE_WRITE:
        error = handle_write(
            state,
            &request,
            io,
            &fields,
            &result
        );
        break;
    case MICROS_RAMFS_MESSAGE_GETDENTS:
        error = handle_getdents(
            state,
            &request,
            io,
            &fields,
            &result
        );
        break;
    case MICROS_RAMFS_MESSAGE_PUTNODE:
        result = handle_putnode(state, &request);
        break;
    default:
        return MICROS_RAMFS_CORE_ERROR_INVARIANT;
    }
    if (error != MICROS_RAMFS_CORE_OK) {
        return error;
    }
    if (
        micros_ramfs_state_validate(state) != MICROS_RAMFS_CORE_OK
    ) {
        return MICROS_RAMFS_CORE_ERROR_INVARIANT;
    }
    return prepare_reply(message, result, &fields, action);
}

_Static_assert(
    sizeof(((struct micros_ramfs_state *)0)->data)
        == MICROS_RAMFS_FILE_SIZE_MAX,
    "RAMFS mutable data arena drift"
);
_Static_assert(
    sizeof(((struct micros_ramfs_state *)0)->scratch)
        == MICROS_RAMFS_TRANSFER_MAX,
    "RAMFS scratch page drift"
);

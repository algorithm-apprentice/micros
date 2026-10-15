#include "servers/ramfs/ramfs_internal.h"

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

enum micros_ramfs_core_error ramfs_handle_read(
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

    *result = ramfs_resolve_referenced_node(
        state,
        request->node,
        &slot,
        &node
    );
    if (*result != MICROS_RAMFS_RESULT_OK) {
        return MICROS_RAMFS_CORE_OK;
    }
    if (ramfs_node_is_directory(node)) {
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
        copy_result = ramfs_copy_grant(
            state,
            io,
            MICROS_RAMFS_COPY_TO_VFS,
            request->grant,
            request->grant_offset,
            state->scratch,
            transferred
        );
        ramfs_zero_bytes(state->scratch, sizeof(state->scratch));
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

static enum micros_ramfs_core_error commit_write(
    struct micros_ramfs_state *state,
    const struct micros_ramfs_request *request,
    struct ramfs_result_fields *fields,
    enum micros_ramfs_result *result,
    uint16_t slot,
    uint64_t write_end,
    size_t first_logical,
    size_t last_logical
)
{
    struct micros_ramfs_node *node;
    size_t logical;

    node = &state->nodes[slot];
    for (logical = first_logical; logical <= last_logical; ++logical) {
        if (node->blocks[logical] == MICROS_RAMFS_BLOCK_NONE) {
            uint16_t block;

            if (!ramfs_find_free_block(state, &block)) {
                ramfs_zero_bytes(state->scratch, sizeof(state->scratch));
                return MICROS_RAMFS_CORE_ERROR_INVARIANT;
            }
            ramfs_zero_bytes(
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
    ramfs_zero_bytes(state->scratch, sizeof(state->scratch));
    fields->file_size = node->size;
    fields->position = write_end;
    fields->count = request->count;
    *result = MICROS_RAMFS_RESULT_OK;
    return MICROS_RAMFS_CORE_OK;
}

enum micros_ramfs_core_error ramfs_handle_write(
    struct micros_ramfs_state *state,
    const struct micros_ramfs_request *request,
    const struct micros_ramfs_io *io,
    struct ramfs_result_fields *fields,
    enum micros_ramfs_result *result
)
{
    const struct micros_ramfs_node *resolved_node;
    uint16_t slot;
    uint64_t write_end;
    size_t first_logical;
    size_t last_logical;
    size_t missing_blocks = 0;
    size_t logical;
    enum micros_ramfs_copy_result copy_result;

    *result = ramfs_resolve_referenced_node(
        state,
        request->node,
        &slot,
        &resolved_node
    );
    if (*result != MICROS_RAMFS_RESULT_OK) {
        return MICROS_RAMFS_CORE_OK;
    }
    if (ramfs_node_is_directory(resolved_node)) {
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

    copy_result = ramfs_copy_grant(
        state,
        io,
        MICROS_RAMFS_COPY_FROM_VFS,
        request->grant,
        request->grant_offset,
        state->scratch,
        request->count
    );
    if (copy_result == MICROS_RAMFS_COPY_INVARIANT) {
        ramfs_zero_bytes(state->scratch, sizeof(state->scratch));
        return MICROS_RAMFS_CORE_ERROR_INVARIANT;
    }
    if (copy_result == MICROS_RAMFS_COPY_REJECTED) {
        ramfs_zero_bytes(state->scratch, sizeof(state->scratch));
        *result = MICROS_RAMFS_RESULT_GRANT;
        return MICROS_RAMFS_CORE_OK;
    }
    if (
        ramfs_validate_state(state, false) != MICROS_RAMFS_CORE_OK
    ) {
        ramfs_zero_bytes(state->scratch, sizeof(state->scratch));
        return MICROS_RAMFS_CORE_ERROR_INVARIANT;
    }

    return commit_write(
        state,
        request,
        fields,
        result,
        slot,
        write_end,
        first_logical,
        last_logical
    );
}

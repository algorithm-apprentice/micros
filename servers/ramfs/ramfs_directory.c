#include "servers/ramfs/ramfs_internal.h"

static void write_directory_record(
    uint8_t *record,
    uint64_t handle,
    uint32_t mode,
    const uint8_t *name,
    uint32_t name_length
)
{
    ramfs_zero_bytes(record, MICROS_RAMFS_DIRECTORY_RECORD_SIZE);
    ramfs_write_u64_le(&record[0], handle);
    ramfs_write_u32_le(&record[8], mode);
    ramfs_write_u32_le(&record[12], name_length);
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
        *handle = ramfs_node_handle(directory_slot, directory);
        *mode = directory->mode;
        *name = dot;
        *name_length = 1;
        return true;
    }
    if (cursor == 1) {
        uint16_t parent_slot = (uint16_t)directory->parent;
        const struct micros_ramfs_node *parent =
            &state->nodes[parent_slot];

        *handle = ramfs_node_handle(parent_slot, parent);
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
                == ramfs_node_handle(directory_slot, directory)
            && slot != directory_slot
        ) {
            *handle = ramfs_node_handle(slot, candidate);
            *mode = candidate->mode;
            *name = candidate->name;
            *name_length = candidate->name_length;
            return true;
        }
    }
    return false;
}

static void build_directory_records(
    struct micros_ramfs_state *state,
    const struct micros_ramfs_request *request,
    uint16_t directory_slot,
    uint64_t *cursor,
    uint32_t *output_count
)
{
    while (*cursor < MICROS_RAMFS_DIRECTORY_CURSOR_END) {
        uint64_t handle;
        uint32_t mode;
        const uint8_t *name;
        uint32_t name_length;

        if (
            !directory_entry_for_cursor(
                state,
                directory_slot,
                *cursor,
                &handle,
                &mode,
                &name,
                &name_length
            )
        ) {
            ++*cursor;
            continue;
        }
        if (
            *output_count + MICROS_RAMFS_DIRECTORY_RECORD_SIZE
            > request->count
        ) {
            break;
        }
        write_directory_record(
            &state->scratch[*output_count],
            handle,
            mode,
            name,
            name_length
        );
        *output_count += MICROS_RAMFS_DIRECTORY_RECORD_SIZE;
        ++*cursor;
    }
}

enum micros_ramfs_core_error ramfs_handle_getdents(
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

    *result = ramfs_resolve_referenced_node(
        state,
        request->node,
        &directory_slot,
        &directory
    );
    if (*result != MICROS_RAMFS_RESULT_OK) {
        return MICROS_RAMFS_CORE_OK;
    }
    if (!ramfs_node_is_directory(directory)) {
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
    build_directory_records(state, request, directory_slot, &cursor, &output_count);
    if (output_count != 0) {
        copy_result = ramfs_copy_grant(
            state,
            io,
            MICROS_RAMFS_COPY_TO_VFS,
            request->grant,
            request->grant_offset,
            state->scratch,
            output_count
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
    fields->position = cursor;
    fields->count = output_count;
    *result = MICROS_RAMFS_RESULT_OK;
    return MICROS_RAMFS_CORE_OK;
}

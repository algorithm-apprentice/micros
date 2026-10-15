#include "servers/ramfs/ramfs_internal.h"

static bool find_child(
    const struct micros_ramfs_state *state,
    uint16_t parent_slot,
    const uint8_t *name,
    size_t name_length,
    uint16_t *child_slot
)
{
    uint64_t parent = ramfs_node_handle(
        parent_slot,
        &state->nodes[parent_slot]
    );
    uint16_t index;

    for (index = 1; index < MICROS_RAMFS_NODE_CAPACITY; ++index) {
        if (
            state->nodes[index].live
            && state->nodes[index].parent == parent
            && ramfs_name_equals_bytes(
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
            !ramfs_decode_handle(
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

static enum micros_ramfs_core_error publish_lookup_reference(
    struct micros_ramfs_state *state,
    uint16_t current_slot,
    struct ramfs_result_fields *fields,
    enum micros_ramfs_result *result
)
{
    if (
        ramfs_validate_state(state, false) != MICROS_RAMFS_CORE_OK
    ) {
        ramfs_zero_bytes(state->scratch, sizeof(state->scratch));
        return MICROS_RAMFS_CORE_ERROR_INVARIANT;
    }
    ramfs_zero_bytes(state->scratch, sizeof(state->scratch));
    ++state->nodes[current_slot].reference_count;
    fields->node = ramfs_node_handle(
        current_slot,
        &state->nodes[current_slot]
    );
    fields->file_size = state->nodes[current_slot].size;
    fields->mode = state->nodes[current_slot].mode;
    *result = MICROS_RAMFS_RESULT_OK;
    return MICROS_RAMFS_CORE_OK;
}

static enum micros_ramfs_core_error walk_staged_lookup(
    struct micros_ramfs_state *state,
    uint16_t start_slot,
    uint16_t root_slot,
    size_t path_size,
    struct ramfs_result_fields *fields,
    enum micros_ramfs_result *result
)
{
    uint16_t current_slot;
    size_t position;
    bool absolute;
    bool trailing_separator = false;

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
        if (!ramfs_node_is_directory(&state->nodes[current_slot])) {
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
        if (!ramfs_node_is_directory(&state->nodes[current_slot])) {
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
        && !ramfs_node_is_directory(&state->nodes[current_slot])
    ) {
        *result = MICROS_RAMFS_RESULT_NOT_DIRECTORY;
        goto lookup_done;
    }
    if (state->nodes[current_slot].reference_count == UINT32_MAX) {
        *result = MICROS_RAMFS_RESULT_NO_SPACE;
        goto lookup_done;
    }
    return publish_lookup_reference(
        state,
        current_slot,
        fields,
        result
    );

lookup_done:
    ramfs_zero_bytes(state->scratch, sizeof(state->scratch));
    return MICROS_RAMFS_CORE_OK;
}

enum micros_ramfs_core_error ramfs_handle_lookup(
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
    size_t path_size;
    size_t position;
    enum micros_ramfs_copy_result copy_result;

    *result = ramfs_resolve_referenced_node(
        state,
        request->start,
        &start_slot,
        &start
    );
    if (*result != MICROS_RAMFS_RESULT_OK) {
        return MICROS_RAMFS_CORE_OK;
    }
    *result = ramfs_resolve_referenced_node(
        state,
        request->root,
        &root_slot,
        &root
    );
    if (*result != MICROS_RAMFS_RESULT_OK) {
        return MICROS_RAMFS_CORE_OK;
    }
    if (!ramfs_node_is_directory(root)) {
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

    copy_result = ramfs_copy_grant(
        state,
        io,
        MICROS_RAMFS_COPY_FROM_VFS,
        request->grant,
        request->grant_offset,
        state->scratch,
        request->length
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

    path_size = request->length - 1;
    if (
        state->scratch[path_size] != 0
        || !ramfs_bytes_are_zero(
            &state->scratch[path_size],
            1
        )
    ) {
        ramfs_zero_bytes(state->scratch, sizeof(state->scratch));
        *result = MICROS_RAMFS_RESULT_MALFORMED;
        return MICROS_RAMFS_CORE_OK;
    }
    for (position = 0; position < path_size; ++position) {
        if (state->scratch[position] == 0) {
            ramfs_zero_bytes(state->scratch, sizeof(state->scratch));
            *result = MICROS_RAMFS_RESULT_MALFORMED;
            return MICROS_RAMFS_CORE_OK;
        }
    }
    if (!path_components_are_bounded(state->scratch, path_size)) {
        ramfs_zero_bytes(state->scratch, sizeof(state->scratch));
        *result = MICROS_RAMFS_RESULT_RANGE;
        return MICROS_RAMFS_CORE_OK;
    }
    return walk_staged_lookup(
        state,
        start_slot,
        root_slot,
        path_size,
        fields,
        result
    );
}

static void publish_created_node(
    struct micros_ramfs_state *state,
    const struct micros_ramfs_request *request,
    struct ramfs_result_fields *fields,
    uint16_t parent_slot,
    uint16_t free_slot,
    size_t name_length,
    bool mkdir_request
)
{
    struct micros_ramfs_node *parent;
    size_t index;

    parent = &state->nodes[parent_slot];
    ramfs_initialize_empty_node(&state->nodes[free_slot]);
    state->nodes[free_slot].live = true;
    state->nodes[free_slot].generation = 1;
    state->nodes[free_slot].mode = request->mode;
    state->nodes[free_slot].link_count = mkdir_request ? 2 : 1;
    state->nodes[free_slot].reference_count = mkdir_request ? 0 : 1;
    state->nodes[free_slot].name_length = (uint16_t)name_length;
    state->nodes[free_slot].parent = ramfs_node_handle(
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
    ramfs_zero_bytes(state->scratch, sizeof(state->scratch));
    if (!mkdir_request) {
        fields->node = ramfs_node_handle(
            free_slot,
            &state->nodes[free_slot]
        );
        fields->mode = state->nodes[free_slot].mode;
    }
}

static enum micros_ramfs_core_error create_from_staged_name(
    struct micros_ramfs_state *state,
    const struct micros_ramfs_request *request,
    struct ramfs_result_fields *fields,
    enum micros_ramfs_result *result,
    uint16_t parent_slot,
    uint16_t free_slot,
    bool mkdir_request
)
{
    uint16_t existing_slot;
    size_t name_length;
    size_t index;

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
        ramfs_validate_state(state, false) != MICROS_RAMFS_CORE_OK
    ) {
        ramfs_zero_bytes(state->scratch, sizeof(state->scratch));
        return MICROS_RAMFS_CORE_ERROR_INVARIANT;
    }

    publish_created_node(
        state,
        request,
        fields,
        parent_slot,
        free_slot,
        name_length,
        mkdir_request
    );
    *result = MICROS_RAMFS_RESULT_OK;
    return MICROS_RAMFS_CORE_OK;

create_done:
    ramfs_zero_bytes(state->scratch, sizeof(state->scratch));
    return MICROS_RAMFS_CORE_OK;
}

enum micros_ramfs_core_error ramfs_handle_create_or_mkdir(
    struct micros_ramfs_state *state,
    const struct micros_ramfs_request *request,
    const struct micros_ramfs_io *io,
    struct ramfs_result_fields *fields,
    enum micros_ramfs_result *result
)
{
    const struct micros_ramfs_node *resolved_parent;
    uint16_t parent_slot;
    uint16_t free_slot;
    bool mkdir_request =
        request->type == MICROS_RAMFS_MESSAGE_MKDIR;
    enum micros_ramfs_copy_result copy_result;

    *result = ramfs_resolve_referenced_node(
        state,
        request->node,
        &parent_slot,
        &resolved_parent
    );
    if (*result != MICROS_RAMFS_RESULT_OK) {
        return MICROS_RAMFS_CORE_OK;
    }
    if (!ramfs_node_is_directory(resolved_parent)) {
        *result = MICROS_RAMFS_RESULT_NOT_DIRECTORY;
        return MICROS_RAMFS_CORE_OK;
    }
    if (
        !ramfs_mode_is_canonical(request->mode)
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
    if (!ramfs_find_free_node(state, &free_slot)) {
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

    copy_result = ramfs_copy_grant(
        state,
        io,
        MICROS_RAMFS_COPY_FROM_VFS,
        request->grant,
        request->grant_offset,
        state->scratch,
        request->length
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
    return create_from_staged_name(
        state,
        request,
        fields,
        result,
        parent_slot,
        free_slot,
        mkdir_request
    );
}

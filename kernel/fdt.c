#include "micros/fdt.h"

#include <stdbool.h>
#include <stdint.h>

enum {
    FDT_HEADER_SIZE = 40,
    FDT_BEGIN_NODE = 1,
    FDT_END_NODE = 2,
    FDT_PROP = 3,
    FDT_NOP = 4,
    FDT_END = 9,
    FDT_MAX_PROPERTY_NAME_LENGTH = 31,
};

#define FDT_MAGIC UINT32_C(0xd00dfeed)

struct fdt_view {
    const uint8_t *blob;
    size_t total_size;
    size_t structure_offset;
    size_t structure_size;
    size_t strings_offset;
    size_t strings_size;
    size_t reservation_offset;
};

struct fdt_node {
    bool children_started;
    bool name_is_memory;
    bool has_device_type;
    bool device_type_is_memory;
    bool is_reserved_memory;
    bool is_reserved_child;
    bool has_reg;
    bool has_address_cells;
    bool has_size_cells;
    bool has_ranges;
    const uint8_t *reg_value;
    size_t reg_length;
    uint32_t address_cells;
    uint32_t size_cells;
    size_t ranges_length;
};

struct fdt_parser {
    struct fdt_view view;
    struct micros_fdt_memory_map *memory_map;
    struct fdt_node nodes[MICROS_FDT_MAX_DEPTH];
    size_t depth;
    bool saw_root;
    bool root_closed;
    bool saw_reserved_memory;
};

static uint32_t read_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24)
        | ((uint32_t)bytes[1] << 16)
        | ((uint32_t)bytes[2] << 8)
        | (uint32_t)bytes[3];
}

static uint64_t read_be64(const uint8_t *bytes)
{
    return ((uint64_t)read_be32(bytes) << 32)
        | (uint64_t)read_be32(bytes + 4);
}

static bool add_size(size_t left, size_t right, size_t *result)
{
    if (right > SIZE_MAX - left) {
        return false;
    }
    *result = left + right;
    return true;
}

static bool align_size(size_t value, size_t alignment, size_t *result)
{
    size_t mask = alignment - 1;
    size_t adjusted;

    if (!add_size(value, mask, &adjusted)) {
        return false;
    }
    *result = adjusted & ~mask;
    return true;
}

static bool bytes_equal(
    const uint8_t *bytes,
    size_t length,
    const char *expected
)
{
    size_t index = 0;

    while (expected[index] != '\0') {
        if (index >= length || bytes[index] != (uint8_t)expected[index]) {
            return false;
        }
        ++index;
    }
    return index == length;
}

static bool property_name_equal(const char *actual, const char *expected)
{
    size_t index = 0;

    while (actual[index] != '\0' && expected[index] != '\0') {
        if (actual[index] != expected[index]) {
            return false;
        }
        ++index;
    }
    return actual[index] == expected[index];
}

static bool node_basename_equal(
    const uint8_t *name,
    size_t length,
    const char *expected
)
{
    size_t index = 0;

    while (
        index < length
        && name[index] != (uint8_t)'@'
        && expected[index] != '\0'
    ) {
        if (name[index] != (uint8_t)expected[index]) {
            return false;
        }
        ++index;
    }
    return expected[index] == '\0'
        && (index == length || name[index] == (uint8_t)'@');
}

static enum micros_fdt_error validate_header(
    const void *blob,
    size_t available_size,
    struct fdt_view *view
)
{
    const uint8_t *bytes = blob;
    uint32_t version;
    uint32_t last_compatible_version;
    size_t structure_end;
    size_t strings_end;

    if (available_size < FDT_HEADER_SIZE) {
        return MICROS_FDT_ERROR_TRUNCATED;
    }
    if (((uintptr_t)blob & (uintptr_t)7) != 0) {
        return MICROS_FDT_ERROR_LAYOUT;
    }
    if (read_be32(bytes) != FDT_MAGIC) {
        return MICROS_FDT_ERROR_MAGIC;
    }

    view->blob = bytes;
    view->total_size = read_be32(bytes + 4);
    view->structure_offset = read_be32(bytes + 8);
    view->strings_offset = read_be32(bytes + 12);
    view->reservation_offset = read_be32(bytes + 16);
    version = read_be32(bytes + 20);
    last_compatible_version = read_be32(bytes + 24);
    view->strings_size = read_be32(bytes + 32);
    view->structure_size = read_be32(bytes + 36);

    if (
        view->total_size < FDT_HEADER_SIZE
        || view->total_size > available_size
        || view->total_size > MICROS_FDT_MAX_BLOB_SIZE
    ) {
        return MICROS_FDT_ERROR_TRUNCATED;
    }
    if (
        version < 17
        || last_compatible_version > 17
        || last_compatible_version > version
    ) {
        return MICROS_FDT_ERROR_VERSION;
    }
    if (
        (view->reservation_offset & 7U) != 0
        || (view->structure_offset & 3U) != 0
        || view->reservation_offset < FDT_HEADER_SIZE
        || view->reservation_offset >= view->structure_offset
        || view->structure_offset >= view->strings_offset
        || view->structure_size < sizeof(uint32_t)
        || view->strings_size == 0
    ) {
        return MICROS_FDT_ERROR_LAYOUT;
    }
    if (
        !add_size(
            view->structure_offset,
            view->structure_size,
            &structure_end
        )
        || !add_size(
            view->strings_offset,
            view->strings_size,
            &strings_end
        )
        || structure_end > view->strings_offset
        || strings_end > view->total_size
    ) {
        return MICROS_FDT_ERROR_LAYOUT;
    }
    return MICROS_FDT_OK;
}

static enum micros_fdt_error append_range(
    struct micros_fdt_range *ranges,
    size_t *count,
    size_t capacity,
    uint64_t base,
    uint64_t size
)
{
    if (size == 0 || size > UINT64_MAX - base) {
        return MICROS_FDT_ERROR_RANGE;
    }
    if (*count >= capacity) {
        return MICROS_FDT_ERROR_CAPACITY;
    }
    ranges[*count].base = base;
    ranges[*count].size = size;
    ++*count;
    return MICROS_FDT_OK;
}

static enum micros_fdt_error parse_reservation_map(
    const struct fdt_view *view,
    struct micros_fdt_memory_map *memory_map
)
{
    size_t cursor = view->reservation_offset;

    while (cursor < view->structure_offset) {
        uint64_t base;
        uint64_t size;
        enum micros_fdt_error error;

        if (view->structure_offset - cursor < 16) {
            return MICROS_FDT_ERROR_LAYOUT;
        }
        base = read_be64(view->blob + cursor);
        size = read_be64(view->blob + cursor + 8);
        cursor += 16;
        if (base == 0 && size == 0) {
            return MICROS_FDT_OK;
        }
        error = append_range(
            memory_map->reservation_ranges,
            &memory_map->reservation_range_count,
            MICROS_FDT_MAX_RESERVATION_RANGES,
            base,
            size
        );
        if (error != MICROS_FDT_OK) {
            return error;
        }
    }
    return MICROS_FDT_ERROR_LAYOUT;
}

static enum micros_fdt_error read_property_name(
    const struct fdt_view *view,
    uint32_t name_offset,
    const char **name
)
{
    const uint8_t *strings;
    size_t available;
    size_t length;

    if (name_offset >= view->strings_size) {
        return MICROS_FDT_ERROR_STRING;
    }
    strings = view->blob + view->strings_offset;
    available = view->strings_size - name_offset;
    for (
        length = 0;
        length < available && length <= FDT_MAX_PROPERTY_NAME_LENGTH;
        ++length
    ) {
        if (strings[name_offset + length] == 0) {
            if (length == 0) {
                return MICROS_FDT_ERROR_STRING;
            }
            *name = (const char *)(strings + name_offset);
            return MICROS_FDT_OK;
        }
    }
    return MICROS_FDT_ERROR_STRING;
}

static enum micros_fdt_error read_cells(
    const uint8_t *value,
    uint32_t cells,
    uint64_t *result
)
{
    if (cells == 1) {
        *result = read_be32(value);
        return MICROS_FDT_OK;
    }
    if (cells == 2) {
        *result = read_be64(value);
        return MICROS_FDT_OK;
    }
    return MICROS_FDT_ERROR_CELLS;
}

static enum micros_fdt_error parse_reg_ranges(
    const uint8_t *value,
    size_t length,
    uint32_t address_cells,
    uint32_t size_cells,
    struct micros_fdt_range *ranges,
    size_t *count,
    size_t capacity
)
{
    size_t tuple_cells;
    size_t tuple_size;
    size_t cursor;

    if (
        (address_cells != 1 && address_cells != 2)
        || (size_cells != 1 && size_cells != 2)
    ) {
        return MICROS_FDT_ERROR_CELLS;
    }
    tuple_cells = address_cells + size_cells;
    tuple_size = tuple_cells * sizeof(uint32_t);
    if (length == 0 || length % tuple_size != 0) {
        return MICROS_FDT_ERROR_PROPERTY;
    }

    for (cursor = 0; cursor < length; cursor += tuple_size) {
        uint64_t base;
        uint64_t size;
        enum micros_fdt_error error;

        error = read_cells(value + cursor, address_cells, &base);
        if (error != MICROS_FDT_OK) {
            return error;
        }
        error = read_cells(
            value + cursor + (address_cells * sizeof(uint32_t)),
            size_cells,
            &size
        );
        if (error != MICROS_FDT_OK) {
            return error;
        }
        error = append_range(ranges, count, capacity, base, size);
        if (error != MICROS_FDT_OK) {
            return error;
        }
    }
    return MICROS_FDT_OK;
}

static enum micros_fdt_error parse_cell_property(
    const uint8_t *value,
    size_t length,
    bool *present,
    uint32_t *cells
)
{
    if (*present || length != sizeof(uint32_t)) {
        return MICROS_FDT_ERROR_PROPERTY;
    }
    *cells = read_be32(value);
    *present = true;
    if (*cells != 1 && *cells != 2) {
        return MICROS_FDT_ERROR_CELLS;
    }
    return MICROS_FDT_OK;
}

static enum micros_fdt_error handle_property(
    struct fdt_parser *parser,
    const char *name,
    const uint8_t *value,
    size_t length
)
{
    struct fdt_node *node = &parser->nodes[parser->depth - 1];

    if (node->children_started) {
        return MICROS_FDT_ERROR_STRUCTURE;
    }
    if (parser->depth == 1) {
        if (property_name_equal(name, "#address-cells")) {
            return parse_cell_property(
                value,
                length,
                &node->has_address_cells,
                &node->address_cells
            );
        }
        if (property_name_equal(name, "#size-cells")) {
            return parse_cell_property(
                value,
                length,
                &node->has_size_cells,
                &node->size_cells
            );
        }
        return MICROS_FDT_OK;
    }

    if (node->name_is_memory || parser->depth == 2) {
        if (property_name_equal(name, "device_type")) {
            if (node->has_device_type) {
                return MICROS_FDT_ERROR_PROPERTY;
            }
            if (length == 0 || value[length - 1] != 0) {
                return MICROS_FDT_ERROR_PROPERTY;
            }
            node->has_device_type = true;
            node->device_type_is_memory = bytes_equal(
                value,
                length - 1,
                "memory"
            );
        } else if (property_name_equal(name, "reg")) {
            if (node->has_reg) {
                return MICROS_FDT_ERROR_PROPERTY;
            }
            node->has_reg = true;
            node->reg_value = value;
            node->reg_length = length;
        }
    }

    if (node->is_reserved_memory) {
        if (property_name_equal(name, "#address-cells")) {
            return parse_cell_property(
                value,
                length,
                &node->has_address_cells,
                &node->address_cells
            );
        }
        if (property_name_equal(name, "#size-cells")) {
            return parse_cell_property(
                value,
                length,
                &node->has_size_cells,
                &node->size_cells
            );
        }
        if (property_name_equal(name, "ranges")) {
            if (node->has_ranges) {
                return MICROS_FDT_ERROR_PROPERTY;
            }
            node->has_ranges = true;
            node->ranges_length = length;
        }
    } else if (
        node->is_reserved_child
        && property_name_equal(name, "reg")
    ) {
        if (node->has_reg) {
            return MICROS_FDT_ERROR_PROPERTY;
        }
        node->has_reg = true;
        node->reg_value = value;
        node->reg_length = length;
    }

    return MICROS_FDT_OK;
}

static enum micros_fdt_error validate_reserved_memory_node(
    const struct fdt_node *node,
    const struct fdt_node *root
)
{
    if (node->device_type_is_memory) {
        return MICROS_FDT_ERROR_PROPERTY;
    }
    if (
        !node->has_address_cells
        || !node->has_size_cells
        || !node->has_ranges
        || node->ranges_length != 0
    ) {
        return MICROS_FDT_ERROR_PROPERTY;
    }
    if (
        node->address_cells != root->address_cells
        || node->size_cells != root->size_cells
    ) {
        return MICROS_FDT_ERROR_CELLS;
    }
    return MICROS_FDT_OK;
}

static void initialize_node(struct fdt_node *node)
{
    node->children_started = false;
    node->name_is_memory = false;
    node->has_device_type = false;
    node->device_type_is_memory = false;
    node->is_reserved_memory = false;
    node->is_reserved_child = false;
    node->has_reg = false;
    node->has_address_cells = false;
    node->has_size_cells = false;
    node->has_ranges = false;
    node->reg_value = NULL;
    node->reg_length = 0;
    node->address_cells = 0;
    node->size_cells = 0;
    node->ranges_length = 0;
}

static enum micros_fdt_error begin_node(
    struct fdt_parser *parser,
    const uint8_t *name,
    size_t length
)
{
    struct fdt_node *node;

    if (parser->depth >= MICROS_FDT_MAX_DEPTH) {
        return MICROS_FDT_ERROR_DEPTH;
    }
    if (parser->root_closed) {
        return MICROS_FDT_ERROR_STRUCTURE;
    }
    if (parser->depth == 0) {
        if (parser->saw_root || length != 0) {
            return MICROS_FDT_ERROR_STRUCTURE;
        }
        parser->saw_root = true;
    } else {
        if (parser->nodes[parser->depth - 1].is_reserved_memory) {
            enum micros_fdt_error error = validate_reserved_memory_node(
                &parser->nodes[parser->depth - 1],
                &parser->nodes[0]
            );

            if (error != MICROS_FDT_OK) {
                return error;
            }
        }
        parser->nodes[parser->depth - 1].children_started = true;
    }

    node = &parser->nodes[parser->depth];
    initialize_node(node);
    if (parser->depth == 1) {
        node->name_is_memory = node_basename_equal(
            name,
            length,
            "memory"
        );
        node->is_reserved_memory = node_basename_equal(
            name,
            length,
            "reserved-memory"
        );
        if (node->is_reserved_memory) {
            if (parser->saw_reserved_memory) {
                return MICROS_FDT_ERROR_STRUCTURE;
            }
            parser->saw_reserved_memory = true;
        }
    } else if (
        parser->depth == 2
        && parser->nodes[1].is_reserved_memory
    ) {
        node->is_reserved_child = true;
    }
    ++parser->depth;
    return MICROS_FDT_OK;
}

static enum micros_fdt_error finish_node(struct fdt_parser *parser)
{
    struct fdt_node *node;
    struct fdt_node *root;

    if (parser->depth == 0) {
        return MICROS_FDT_ERROR_STRUCTURE;
    }
    node = &parser->nodes[parser->depth - 1];
    root = &parser->nodes[0];

    if (parser->depth == 1) {
        if (!node->has_address_cells || !node->has_size_cells) {
            return MICROS_FDT_ERROR_CELLS;
        }
        parser->memory_map->address_cells = node->address_cells;
        parser->memory_map->size_cells = node->size_cells;
        parser->root_closed = true;
    } else if (node->is_reserved_memory) {
        enum micros_fdt_error error = validate_reserved_memory_node(
            node,
            root
        );

        if (error != MICROS_FDT_OK) {
            return error;
        }
    } else if (node->name_is_memory || node->device_type_is_memory) {
        if (!node->has_reg) {
            return MICROS_FDT_ERROR_PROPERTY;
        }
        {
            enum micros_fdt_error error = parse_reg_ranges(
                node->reg_value,
                node->reg_length,
                root->address_cells,
                root->size_cells,
                parser->memory_map->memory_ranges,
                &parser->memory_map->memory_range_count,
                MICROS_FDT_MAX_MEMORY_RANGES
            );

            if (error != MICROS_FDT_OK) {
                return error;
            }
        }
    } else if (node->is_reserved_child) {
        struct fdt_node *reserved_parent = &parser->nodes[1];

        if (!node->has_reg) {
            return MICROS_FDT_ERROR_PROPERTY;
        }
        {
            enum micros_fdt_error error = parse_reg_ranges(
                node->reg_value,
                node->reg_length,
                reserved_parent->address_cells,
                reserved_parent->size_cells,
                parser->memory_map->reserved_memory_ranges,
                &parser->memory_map->reserved_memory_range_count,
                MICROS_FDT_MAX_RESERVED_MEMORY_RANGES
            );

            if (error != MICROS_FDT_OK) {
                return error;
            }
        }
    }

    --parser->depth;
    return MICROS_FDT_OK;
}

static enum micros_fdt_error read_node_name(
    const struct fdt_view *view,
    size_t *cursor,
    const uint8_t **name,
    size_t *length
)
{
    size_t name_start = *cursor;
    size_t aligned;
    size_t index;

    while (
        *cursor < view->structure_offset + view->structure_size
        && view->blob[*cursor] != 0
    ) {
        ++*cursor;
    }
    if (*cursor == view->structure_offset + view->structure_size) {
        return MICROS_FDT_ERROR_STRUCTURE;
    }
    *name = view->blob + name_start;
    *length = *cursor - name_start;
    ++*cursor;
    if (!align_size(*cursor, 4, &aligned)) {
        return MICROS_FDT_ERROR_STRUCTURE;
    }
    if (aligned > view->structure_offset + view->structure_size) {
        return MICROS_FDT_ERROR_STRUCTURE;
    }
    for (index = *cursor; index < aligned; ++index) {
        if (view->blob[index] != 0) {
            return MICROS_FDT_ERROR_STRUCTURE;
        }
    }
    *cursor = aligned;
    return MICROS_FDT_OK;
}

static enum micros_fdt_error parse_structure(
    struct fdt_parser *parser
)
{
    const struct fdt_view *view = &parser->view;
    size_t cursor = view->structure_offset;
    size_t end = view->structure_offset + view->structure_size;

    while (cursor < end) {
        uint32_t token;

        if (end - cursor < sizeof(uint32_t)) {
            return MICROS_FDT_ERROR_STRUCTURE;
        }
        token = read_be32(view->blob + cursor);
        cursor += sizeof(uint32_t);

        if (token == FDT_BEGIN_NODE) {
            const uint8_t *name;
            size_t length;
            enum micros_fdt_error error = read_node_name(
                view,
                &cursor,
                &name,
                &length
            );

            if (error != MICROS_FDT_OK) {
                return error;
            }
            error = begin_node(parser, name, length);
            if (error != MICROS_FDT_OK) {
                return error;
            }
        } else if (token == FDT_END_NODE) {
            enum micros_fdt_error error = finish_node(parser);

            if (error != MICROS_FDT_OK) {
                return error;
            }
        } else if (token == FDT_PROP) {
            uint32_t length;
            uint32_t name_offset;
            size_t value_end;
            size_t aligned_end;
            const char *name;
            enum micros_fdt_error error;

            if (parser->depth == 0 || end - cursor < 8) {
                return MICROS_FDT_ERROR_STRUCTURE;
            }
            length = read_be32(view->blob + cursor);
            name_offset = read_be32(view->blob + cursor + 4);
            cursor += 8;
            if (
                !add_size(cursor, length, &value_end)
                || !align_size(value_end, 4, &aligned_end)
                || aligned_end > end
            ) {
                return MICROS_FDT_ERROR_STRUCTURE;
            }
            error = read_property_name(view, name_offset, &name);
            if (error != MICROS_FDT_OK) {
                return error;
            }
            error = handle_property(
                parser,
                name,
                view->blob + cursor,
                length
            );
            if (error != MICROS_FDT_OK) {
                return error;
            }
            cursor = aligned_end;
        } else if (token == FDT_NOP) {
            continue;
        } else if (token == FDT_END) {
            if (
                parser->depth != 0
                || !parser->saw_root
                || !parser->root_closed
                || cursor != end
            ) {
                return MICROS_FDT_ERROR_STRUCTURE;
            }
            if (parser->memory_map->memory_range_count == 0) {
                return MICROS_FDT_ERROR_NO_MEMORY;
            }
            return MICROS_FDT_OK;
        } else {
            return MICROS_FDT_ERROR_STRUCTURE;
        }
    }
    return MICROS_FDT_ERROR_STRUCTURE;
}

enum micros_fdt_error micros_fdt_parse_memory_map(
    const void *blob,
    size_t available_size,
    struct micros_fdt_memory_map *memory_map
)
{
    struct fdt_parser parser;
    enum micros_fdt_error error;

    if (blob == NULL || memory_map == NULL) {
        return MICROS_FDT_ERROR_ARGUMENT;
    }

    memory_map->blob_size = 0;
    memory_map->address_cells = 0;
    memory_map->size_cells = 0;
    memory_map->memory_range_count = 0;
    memory_map->reservation_range_count = 0;
    memory_map->reserved_memory_range_count = 0;

    parser.memory_map = memory_map;
    parser.depth = 0;
    parser.saw_root = false;
    parser.root_closed = false;
    parser.saw_reserved_memory = false;

    error = validate_header(blob, available_size, &parser.view);
    if (error != MICROS_FDT_OK) {
        return error;
    }
    memory_map->blob_size = (uint32_t)parser.view.total_size;
    error = parse_reservation_map(&parser.view, memory_map);
    if (error != MICROS_FDT_OK) {
        return error;
    }

    return parse_structure(&parser);
}

const char *micros_fdt_error_name(enum micros_fdt_error error)
{
    switch (error) {
    case MICROS_FDT_OK:
        return "ok";
    case MICROS_FDT_ERROR_ARGUMENT:
        return "argument";
    case MICROS_FDT_ERROR_TRUNCATED:
        return "truncated";
    case MICROS_FDT_ERROR_MAGIC:
        return "magic";
    case MICROS_FDT_ERROR_VERSION:
        return "version";
    case MICROS_FDT_ERROR_LAYOUT:
        return "layout";
    case MICROS_FDT_ERROR_STRUCTURE:
        return "structure";
    case MICROS_FDT_ERROR_STRING:
        return "string";
    case MICROS_FDT_ERROR_CELLS:
        return "cells";
    case MICROS_FDT_ERROR_PROPERTY:
        return "property";
    case MICROS_FDT_ERROR_RANGE:
        return "range";
    case MICROS_FDT_ERROR_CAPACITY:
        return "capacity";
    case MICROS_FDT_ERROR_DEPTH:
        return "depth";
    case MICROS_FDT_ERROR_NO_MEMORY:
        return "no-memory";
    }
    return "unknown";
}

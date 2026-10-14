#include "fdt_test_internal.h"

#include <stdlib.h>

#define FDT_MAGIC UINT32_C(0xd00dfeed)

#define PROPERTY_ADDRESS_CELLS_OFFSET 0U
#define PROPERTY_SIZE_CELLS_OFFSET \
    (PROPERTY_ADDRESS_CELLS_OFFSET + sizeof("#address-cells"))
#define PROPERTY_DEVICE_TYPE_OFFSET \
    (PROPERTY_SIZE_CELLS_OFFSET + sizeof("#size-cells"))
#define PROPERTY_REG_OFFSET \
    (PROPERTY_DEVICE_TYPE_OFFSET + sizeof("device_type"))
#define PROPERTY_RANGES_OFFSET (PROPERTY_REG_OFFSET + sizeof("reg"))
#define PROPERTY_STATUS_OFFSET (PROPERTY_RANGES_OFFSET + sizeof("ranges"))

static const char property_names[] =
    "#address-cells\0"
    "#size-cells\0"
    "device_type\0"
    "reg\0"
    "ranges\0"
    "status\0";

struct property_location {
    size_t length;
    size_t name;
    size_t value;
};

struct structure_builder {
    uint8_t bytes[TEST_STRUCTURE_CAPACITY];
    size_t size;
};

struct fdt_build_context {
    struct structure_builder builder;
    struct property_location root_address;
    struct property_location root_size;
    struct property_location memory_device_type;
    struct property_location memory_reg;
    struct property_location reserved_address;
    struct property_location reserved_size;
    uint8_t memory_ranges[16 * (MICROS_FDT_MAX_MEMORY_RANGES + 1)];
    uint8_t device_memory_range[16];
    uint8_t reserved_range[16];
    uint8_t nonempty_ranges[4];
    uint32_t structure_offset;
    uint32_t strings_offset;
    uint32_t total_size;
    size_t reservation_offset;
};

static void fail_builder(const char *message)
{
    fprintf(stderr, "fixture builder failure: %s\n", message);
    exit(2);
}

void fdt_test_write_be32(uint8_t *bytes, size_t offset, uint32_t value)
{
    bytes[offset] = (uint8_t)(value >> 24);
    bytes[offset + 1] = (uint8_t)(value >> 16);
    bytes[offset + 2] = (uint8_t)(value >> 8);
    bytes[offset + 3] = (uint8_t)value;
}

void fdt_test_write_be64(uint8_t *bytes, size_t offset, uint64_t value)
{
    fdt_test_write_be32(bytes, offset, (uint32_t)(value >> 32));
    fdt_test_write_be32(bytes, offset + 4, (uint32_t)value);
}

static void append_bytes(
    struct structure_builder *builder,
    const void *source,
    size_t length
)
{
    if (length > sizeof(builder->bytes) - builder->size) {
        fail_builder("structure capacity exceeded");
    }
    memcpy(&builder->bytes[builder->size], source, length);
    builder->size += length;
}

static void append_u32(struct structure_builder *builder, uint32_t value)
{
    uint8_t encoded[4];

    fdt_test_write_be32(encoded, 0, value);
    append_bytes(builder, encoded, sizeof(encoded));
}

static void align_structure(struct structure_builder *builder)
{
    static const uint8_t zeros[3] = {0, 0, 0};
    size_t padding = (4 - (builder->size % 4)) % 4;

    append_bytes(builder, zeros, padding);
}

static void begin_node(struct structure_builder *builder, const char *name)
{
    append_u32(builder, FDT_BEGIN_NODE);
    append_bytes(builder, name, strlen(name) + 1);
    align_structure(builder);
}

static void end_node(struct structure_builder *builder)
{
    append_u32(builder, FDT_END_NODE);
}

static struct property_location append_property(
    struct structure_builder *builder,
    uint32_t name_offset,
    const void *value,
    size_t length
)
{
    struct property_location location;

    if (length > UINT32_MAX) {
        fail_builder("property length exceeds u32");
    }
    append_u32(builder, FDT_PROP);
    location.length = builder->size;
    append_u32(builder, (uint32_t)length);
    location.name = builder->size;
    append_u32(builder, name_offset);
    location.value = builder->size;
    append_bytes(builder, value, length);
    align_structure(builder);
    return location;
}

static struct property_location append_u32_property(
    struct structure_builder *builder,
    uint32_t name_offset,
    uint32_t value
)
{
    uint8_t encoded[4];

    fdt_test_write_be32(encoded, 0, value);
    return append_property(builder, name_offset, encoded, sizeof(encoded));
}

static void append_range_cells(
    uint8_t *bytes,
    size_t offset,
    uint64_t base,
    uint64_t size
)
{
    fdt_test_write_be64(bytes, offset, base);
    fdt_test_write_be64(bytes, offset + 8, size);
}

struct fixture_options fdt_test_default_options(void)
{
    return (struct fixture_options){
        .memory_tuple_count = 2,
        .memory_reg_trim = 0,
        .nested_depth = 0,
        .include_memory = true,
        .memory_name_has_conflicting_type = false,
        .named_memory_is_disabled = false,
        .device_memory_is_disabled = false,
        .include_reserved_memory = true,
        .reserved_memory_claims_memory = false,
        .reserved_memory_overrides_device_type = false,
        .reserved_ranges_nonempty = false,
        .reserved_child_has_reg = true,
    };
}

static void build_root_properties(struct fdt_build_context *context)
{
    begin_node(&context->builder, "");
    context->root_address = append_u32_property(
        &context->builder,
        PROPERTY_ADDRESS_CELLS_OFFSET,
        2
    );
    context->root_size = append_u32_property(
        &context->builder,
        PROPERTY_SIZE_CELLS_OFFSET,
        2
    );
}

static void build_memory_nodes(
    struct fdt_build_context *context,
    struct fixture_options options
)
{
    size_t index;

    if (options.include_memory) {
        if (
            options.memory_tuple_count
            > sizeof(context->memory_ranges) / 16
        ) {
            fail_builder("too many requested memory tuples");
        }
        for (index = 0; index < options.memory_tuple_count; ++index) {
            uint64_t base = UINT64_C(0x80000000)
                + (UINT64_C(0x01000000) * index);
            uint64_t size = index == 0
                ? UINT64_C(0x08000000)
                : UINT64_C(0x00100000);

            append_range_cells(context->memory_ranges, index * 16, base, size);
        }

        begin_node(&context->builder, "memory@80000000");
        if (options.memory_name_has_conflicting_type) {
            (void)append_property(
                &context->builder,
                PROPERTY_DEVICE_TYPE_OFFSET,
                "system",
                sizeof("system")
            );
        }
        if (options.named_memory_is_disabled) {
            (void)append_property(
                &context->builder,
                PROPERTY_STATUS_OFFSET,
                "disabled",
                sizeof("disabled")
            );
        }
        if (
            options.memory_reg_trim
            > options.memory_tuple_count * 16
        ) {
            fail_builder("memory reg trim exceeds property length");
        }
        context->memory_reg = append_property(
            &context->builder,
            PROPERTY_REG_OFFSET,
            context->memory_ranges,
            (options.memory_tuple_count * 16)
                - options.memory_reg_trim
        );
        end_node(&context->builder);

        append_range_cells(
            context->device_memory_range,
            0,
            UINT64_C(0x100000000),
            UINT64_C(0x01000000)
        );
        begin_node(&context->builder, "ram-bank@100000000");
        context->memory_device_type = append_property(
            &context->builder,
            PROPERTY_DEVICE_TYPE_OFFSET,
            "memory",
            sizeof("memory")
        );
        if (options.device_memory_is_disabled) {
            (void)append_property(
                &context->builder,
                PROPERTY_STATUS_OFFSET,
                "disabled",
                sizeof("disabled")
            );
        }
        (void)append_property(
            &context->builder,
            PROPERTY_REG_OFFSET,
            context->device_memory_range,
            sizeof(context->device_memory_range)
        );
        end_node(&context->builder);
    }
}

static void build_reserved_subtree(
    struct fdt_build_context *context,
    struct fixture_options options
)
{
    if (options.include_reserved_memory) {
        begin_node(&context->builder, "reserved-memory");
        context->reserved_address = append_u32_property(
            &context->builder,
            PROPERTY_ADDRESS_CELLS_OFFSET,
            2
        );
        context->reserved_size = append_u32_property(
            &context->builder,
            PROPERTY_SIZE_CELLS_OFFSET,
            2
        );
        if (options.reserved_ranges_nonempty) {
            (void)append_property(
                &context->builder,
                PROPERTY_RANGES_OFFSET,
                context->nonempty_ranges,
                sizeof(context->nonempty_ranges)
            );
        } else {
            (void)append_property(
                &context->builder,
                PROPERTY_RANGES_OFFSET,
                NULL,
                0
            );
        }
        if (options.reserved_memory_claims_memory) {
            append_range_cells(
                context->reserved_range,
                0,
                UINT64_C(0x90000000),
                UINT64_C(0x1000)
            );
            (void)append_property(
                &context->builder,
                PROPERTY_DEVICE_TYPE_OFFSET,
                "memory",
                sizeof("memory")
            );
            (void)append_property(
                &context->builder,
                PROPERTY_REG_OFFSET,
                context->reserved_range,
                sizeof(context->reserved_range)
            );
            if (options.reserved_memory_overrides_device_type) {
                (void)append_property(
                    &context->builder,
                    PROPERTY_DEVICE_TYPE_OFFSET,
                    "reserved",
                    sizeof("reserved")
                );
            }
        }
        begin_node(&context->builder, "firmware-buffer@82000000");
        if (options.reserved_child_has_reg) {
            append_range_cells(
                context->reserved_range,
                0,
                UINT64_C(0x82000000),
                UINT64_C(0x2000)
            );
            (void)append_property(
                &context->builder,
                PROPERTY_REG_OFFSET,
                context->reserved_range,
                sizeof(context->reserved_range)
            );
        }
        end_node(&context->builder);
        end_node(&context->builder);
    }
}

static void finish_structure(
    struct fdt_build_context *context,
    struct fdt_fixture *fixture,
    struct fixture_options options
)
{
    size_t index;

    for (index = 0; index < options.nested_depth; ++index) {
        begin_node(&context->builder, "nested");
    }
    for (index = 0; index < options.nested_depth; ++index) {
        end_node(&context->builder);
    }

    end_node(&context->builder);
    fixture->final_token = context->builder.size;
    append_u32(&context->builder, FDT_END);
}

static void build_reservation_map(
    struct fdt_fixture *fixture,
    size_t reservation_offset
)
{
    fdt_test_write_be64(
        fixture->bytes,
        reservation_offset,
        UINT64_C(0x81000000)
    );
    fdt_test_write_be64(
        fixture->bytes,
        reservation_offset + 8,
        UINT64_C(0x1000)
    );
    fixture->reservation_terminator = reservation_offset + 16;
    fdt_test_write_be64(fixture->bytes, fixture->reservation_terminator, 0);
    fdt_test_write_be64(fixture->bytes, fixture->reservation_terminator + 8, 0);
}

static void finalize_blob(
    struct fdt_build_context *context,
    struct fdt_fixture *fixture,
    struct fixture_options options
)
{
    context->structure_offset = (uint32_t)(context->reservation_offset + 32);
    context->strings_offset = context->structure_offset + (uint32_t)context->builder.size;
    context->total_size = context->strings_offset + (uint32_t)sizeof(property_names);
    if (context->total_size > sizeof(fixture->bytes)) {
        fail_builder("blob capacity exceeded");
    }

    fdt_test_write_be32(fixture->bytes, 0, FDT_MAGIC);
    fdt_test_write_be32(fixture->bytes, 4, context->total_size);
    fdt_test_write_be32(fixture->bytes, 8, context->structure_offset);
    fdt_test_write_be32(fixture->bytes, 12, context->strings_offset);
    fdt_test_write_be32(fixture->bytes, 16, (uint32_t)context->reservation_offset);
    fdt_test_write_be32(fixture->bytes, 20, 17);
    fdt_test_write_be32(fixture->bytes, 24, 16);
    fdt_test_write_be32(fixture->bytes, 28, 0);
    fdt_test_write_be32(fixture->bytes, 32, (uint32_t)sizeof(property_names));
    fdt_test_write_be32(fixture->bytes, 36, (uint32_t)context->builder.size);

    build_reservation_map(fixture, context->reservation_offset);
    memcpy(
        &fixture->bytes[context->structure_offset],
        context->builder.bytes,
        context->builder.size
    );
    memcpy(
        &fixture->bytes[context->strings_offset],
        property_names,
        sizeof(property_names)
    );

    fixture->size = context->total_size;
    fixture->strings_offset = context->strings_offset;
    fixture->first_token = context->structure_offset;
    fixture->final_token += context->structure_offset;
    fixture->root_address_cells_value =
        context->structure_offset + context->root_address.value;
    fixture->root_size_cells_value = context->structure_offset + context->root_size.value;
    if (options.include_memory) {
        fixture->memory_device_type_padding =
            context->structure_offset
            + context->memory_device_type.value
            + sizeof("memory");
        fixture->memory_reg_length =
            context->structure_offset + context->memory_reg.length;
        fixture->memory_reg_name = context->structure_offset + context->memory_reg.name;
        fixture->memory_reg_value = context->structure_offset + context->memory_reg.value;
    }
    if (options.include_reserved_memory) {
        fixture->reserved_address_cells_value =
            context->structure_offset + context->reserved_address.value;
        fixture->reserved_size_cells_value =
            context->structure_offset + context->reserved_size.value;
    }
}

void fdt_test_build_fixture(
    struct fdt_fixture *fixture,
    struct fixture_options options
)
{
    struct fdt_build_context context = {
        .builder = {0},
        .memory_device_type = {0},
        .memory_reg = {0},
        .reserved_address = {0},
        .reserved_size = {0},
        .nonempty_ranges = {0, 0, 0, 0},
        .reservation_offset = FDT_HEADER_SIZE,
    };

    memset(fixture, 0, sizeof(*fixture));
    memset(context.memory_ranges, 0, sizeof(context.memory_ranges));
    build_root_properties(&context);
    build_memory_nodes(&context, options);
    build_reserved_subtree(&context, options);
    finish_structure(&context, fixture, options);
    finalize_blob(&context, fixture, options);
}

#include "micros/fdt.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    FDT_HEADER_SIZE = 40,
    FDT_BEGIN_NODE = 1,
    FDT_END_NODE = 2,
    FDT_PROP = 3,
    FDT_END = 9,
    TEST_BLOB_CAPACITY = 8192,
    TEST_STRUCTURE_CAPACITY = 4096,
};

#define FDT_MAGIC UINT32_C(0xd00dfeed)

#define PROPERTY_ADDRESS_CELLS_OFFSET 0U
#define PROPERTY_SIZE_CELLS_OFFSET \
    (PROPERTY_ADDRESS_CELLS_OFFSET + sizeof("#address-cells"))
#define PROPERTY_DEVICE_TYPE_OFFSET \
    (PROPERTY_SIZE_CELLS_OFFSET + sizeof("#size-cells"))
#define PROPERTY_REG_OFFSET \
    (PROPERTY_DEVICE_TYPE_OFFSET + sizeof("device_type"))
#define PROPERTY_RANGES_OFFSET (PROPERTY_REG_OFFSET + sizeof("reg"))

static const char property_names[] =
    "#address-cells\0"
    "#size-cells\0"
    "device_type\0"
    "reg\0"
    "ranges\0";

struct property_location {
    size_t length;
    size_t name;
    size_t value;
};

struct fdt_fixture {
    uint8_t bytes[TEST_BLOB_CAPACITY];
    size_t size;
    size_t root_address_cells_value;
    size_t root_size_cells_value;
    size_t strings_offset;
    size_t memory_device_type_padding;
    size_t memory_reg_length;
    size_t memory_reg_name;
    size_t memory_reg_value;
    size_t reserved_address_cells_value;
    size_t reserved_size_cells_value;
    size_t reservation_terminator;
    size_t first_token;
    size_t final_token;
};

struct fixture_options {
    size_t memory_tuple_count;
    size_t memory_reg_trim;
    size_t nested_depth;
    bool include_memory;
    bool include_reserved_memory;
    bool reserved_ranges_nonempty;
    bool reserved_child_has_reg;
};

struct structure_builder {
    uint8_t bytes[TEST_STRUCTURE_CAPACITY];
    size_t size;
};

static void fail_builder(const char *message)
{
    fprintf(stderr, "fixture builder failure: %s\n", message);
    exit(2);
}

static void write_be32(uint8_t *bytes, size_t offset, uint32_t value)
{
    bytes[offset] = (uint8_t)(value >> 24);
    bytes[offset + 1] = (uint8_t)(value >> 16);
    bytes[offset + 2] = (uint8_t)(value >> 8);
    bytes[offset + 3] = (uint8_t)value;
}

static void write_be64(uint8_t *bytes, size_t offset, uint64_t value)
{
    write_be32(bytes, offset, (uint32_t)(value >> 32));
    write_be32(bytes, offset + 4, (uint32_t)value);
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

    write_be32(encoded, 0, value);
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

    write_be32(encoded, 0, value);
    return append_property(builder, name_offset, encoded, sizeof(encoded));
}

static void append_range_cells(
    uint8_t *bytes,
    size_t offset,
    uint64_t base,
    uint64_t size
)
{
    write_be64(bytes, offset, base);
    write_be64(bytes, offset + 8, size);
}

static struct fixture_options default_options(void)
{
    return (struct fixture_options){
        .memory_tuple_count = 2,
        .memory_reg_trim = 0,
        .nested_depth = 0,
        .include_memory = true,
        .include_reserved_memory = true,
        .reserved_ranges_nonempty = false,
        .reserved_child_has_reg = true,
    };
}

static void build_fixture(
    struct fdt_fixture *fixture,
    struct fixture_options options
)
{
    struct structure_builder builder = {0};
    struct property_location root_address;
    struct property_location root_size;
    struct property_location memory_device_type = {0};
    struct property_location memory_reg = {0};
    struct property_location reserved_address = {0};
    struct property_location reserved_size = {0};
    uint8_t memory_ranges[16 * (MICROS_FDT_MAX_MEMORY_RANGES + 1)];
    uint8_t device_memory_range[16];
    uint8_t reserved_range[16];
    uint8_t nonempty_ranges[4] = {0, 0, 0, 0};
    uint32_t structure_offset;
    uint32_t strings_offset;
    uint32_t total_size;
    size_t reservation_offset = FDT_HEADER_SIZE;
    size_t index;

    memset(fixture, 0, sizeof(*fixture));
    memset(memory_ranges, 0, sizeof(memory_ranges));

    begin_node(&builder, "");
    root_address = append_u32_property(
        &builder,
        PROPERTY_ADDRESS_CELLS_OFFSET,
        2
    );
    root_size = append_u32_property(
        &builder,
        PROPERTY_SIZE_CELLS_OFFSET,
        2
    );

    if (options.include_memory) {
        if (
            options.memory_tuple_count
            > sizeof(memory_ranges) / 16
        ) {
            fail_builder("too many requested memory tuples");
        }
        for (index = 0; index < options.memory_tuple_count; ++index) {
            uint64_t base = UINT64_C(0x80000000)
                + (UINT64_C(0x01000000) * index);
            uint64_t size = index == 0
                ? UINT64_C(0x08000000)
                : UINT64_C(0x00100000);

            append_range_cells(memory_ranges, index * 16, base, size);
        }

        begin_node(&builder, "memory@80000000");
        if (
            options.memory_reg_trim
            > options.memory_tuple_count * 16
        ) {
            fail_builder("memory reg trim exceeds property length");
        }
        memory_reg = append_property(
            &builder,
            PROPERTY_REG_OFFSET,
            memory_ranges,
            (options.memory_tuple_count * 16)
                - options.memory_reg_trim
        );
        end_node(&builder);

        append_range_cells(
            device_memory_range,
            0,
            UINT64_C(0x100000000),
            UINT64_C(0x01000000)
        );
        begin_node(&builder, "ram-bank@100000000");
        memory_device_type = append_property(
            &builder,
            PROPERTY_DEVICE_TYPE_OFFSET,
            "memory",
            sizeof("memory")
        );
        (void)append_property(
            &builder,
            PROPERTY_REG_OFFSET,
            device_memory_range,
            sizeof(device_memory_range)
        );
        end_node(&builder);
    }

    if (options.include_reserved_memory) {
        begin_node(&builder, "reserved-memory");
        reserved_address = append_u32_property(
            &builder,
            PROPERTY_ADDRESS_CELLS_OFFSET,
            2
        );
        reserved_size = append_u32_property(
            &builder,
            PROPERTY_SIZE_CELLS_OFFSET,
            2
        );
        if (options.reserved_ranges_nonempty) {
            (void)append_property(
                &builder,
                PROPERTY_RANGES_OFFSET,
                nonempty_ranges,
                sizeof(nonempty_ranges)
            );
        } else {
            (void)append_property(
                &builder,
                PROPERTY_RANGES_OFFSET,
                NULL,
                0
            );
        }
        begin_node(&builder, "firmware-buffer@82000000");
        if (options.reserved_child_has_reg) {
            append_range_cells(
                reserved_range,
                0,
                UINT64_C(0x82000000),
                UINT64_C(0x2000)
            );
            (void)append_property(
                &builder,
                PROPERTY_REG_OFFSET,
                reserved_range,
                sizeof(reserved_range)
            );
        }
        end_node(&builder);
        end_node(&builder);
    }

    for (index = 0; index < options.nested_depth; ++index) {
        begin_node(&builder, "nested");
    }
    for (index = 0; index < options.nested_depth; ++index) {
        end_node(&builder);
    }

    end_node(&builder);
    fixture->final_token = builder.size;
    append_u32(&builder, FDT_END);

    structure_offset = (uint32_t)(reservation_offset + 32);
    strings_offset = structure_offset + (uint32_t)builder.size;
    total_size = strings_offset + (uint32_t)sizeof(property_names);
    if (total_size > sizeof(fixture->bytes)) {
        fail_builder("blob capacity exceeded");
    }

    write_be32(fixture->bytes, 0, FDT_MAGIC);
    write_be32(fixture->bytes, 4, total_size);
    write_be32(fixture->bytes, 8, structure_offset);
    write_be32(fixture->bytes, 12, strings_offset);
    write_be32(fixture->bytes, 16, (uint32_t)reservation_offset);
    write_be32(fixture->bytes, 20, 17);
    write_be32(fixture->bytes, 24, 16);
    write_be32(fixture->bytes, 28, 0);
    write_be32(fixture->bytes, 32, (uint32_t)sizeof(property_names));
    write_be32(fixture->bytes, 36, (uint32_t)builder.size);

    write_be64(
        fixture->bytes,
        reservation_offset,
        UINT64_C(0x81000000)
    );
    write_be64(
        fixture->bytes,
        reservation_offset + 8,
        UINT64_C(0x1000)
    );
    fixture->reservation_terminator = reservation_offset + 16;
    write_be64(fixture->bytes, fixture->reservation_terminator, 0);
    write_be64(fixture->bytes, fixture->reservation_terminator + 8, 0);

    memcpy(
        &fixture->bytes[structure_offset],
        builder.bytes,
        builder.size
    );
    memcpy(
        &fixture->bytes[strings_offset],
        property_names,
        sizeof(property_names)
    );

    fixture->size = total_size;
    fixture->strings_offset = strings_offset;
    fixture->first_token = structure_offset;
    fixture->final_token += structure_offset;
    fixture->root_address_cells_value =
        structure_offset + root_address.value;
    fixture->root_size_cells_value = structure_offset + root_size.value;
    if (options.include_memory) {
        fixture->memory_device_type_padding =
            structure_offset
            + memory_device_type.value
            + sizeof("memory");
        fixture->memory_reg_length =
            structure_offset + memory_reg.length;
        fixture->memory_reg_name = structure_offset + memory_reg.name;
        fixture->memory_reg_value = structure_offset + memory_reg.value;
    }
    if (options.include_reserved_memory) {
        fixture->reserved_address_cells_value =
            structure_offset + reserved_address.value;
        fixture->reserved_size_cells_value =
            structure_offset + reserved_size.value;
    }
}

#define EXPECT_TRUE(expression) \
    do { \
        if (!(expression)) { \
            fprintf( \
                stderr, \
                "%s:%d: expected %s\n", \
                __FILE__, \
                __LINE__, \
                #expression \
            ); \
            return false; \
        } \
    } while (false)

#define EXPECT_ERROR(expected, expression) \
    do { \
        enum micros_fdt_error actual_error = (expression); \
        if (actual_error != (expected)) { \
            fprintf( \
                stderr, \
                "%s:%d: expected error %d, got %d\n", \
                __FILE__, \
                __LINE__, \
                (int)(expected), \
                (int)actual_error \
            ); \
            return false; \
        } \
    } while (false)

static bool test_parses_memory_and_reservations(void)
{
    struct fdt_fixture fixture;
    struct micros_fdt_memory_map memory_map;

    build_fixture(&fixture, default_options());
    EXPECT_ERROR(
        MICROS_FDT_OK,
        micros_fdt_parse_memory_map(
            fixture.bytes,
            fixture.size,
            &memory_map
        )
    );
    EXPECT_TRUE(memory_map.blob_size == fixture.size);
    EXPECT_TRUE(memory_map.address_cells == 2);
    EXPECT_TRUE(memory_map.size_cells == 2);
    EXPECT_TRUE(memory_map.memory_range_count == 3);
    EXPECT_TRUE(
        memory_map.memory_ranges[0].base == UINT64_C(0x80000000)
    );
    EXPECT_TRUE(
        memory_map.memory_ranges[0].size == UINT64_C(0x08000000)
    );
    EXPECT_TRUE(
        memory_map.memory_ranges[1].base == UINT64_C(0x81000000)
    );
    EXPECT_TRUE(
        memory_map.memory_ranges[2].base == UINT64_C(0x100000000)
    );
    EXPECT_TRUE(memory_map.reservation_range_count == 1);
    EXPECT_TRUE(
        memory_map.reservation_ranges[0].base == UINT64_C(0x81000000)
    );
    EXPECT_TRUE(
        memory_map.reservation_ranges[0].size == UINT64_C(0x1000)
    );
    EXPECT_TRUE(memory_map.reserved_memory_range_count == 1);
    EXPECT_TRUE(
        memory_map.reserved_memory_ranges[0].base
        == UINT64_C(0x82000000)
    );
    EXPECT_TRUE(
        memory_map.reserved_memory_ranges[0].size == UINT64_C(0x2000)
    );
    return true;
}

static bool test_rejects_null_arguments(void)
{
    struct fdt_fixture fixture;
    struct micros_fdt_memory_map memory_map;

    build_fixture(&fixture, default_options());
    EXPECT_ERROR(
        MICROS_FDT_ERROR_ARGUMENT,
        micros_fdt_parse_memory_map(NULL, fixture.size, &memory_map)
    );
    EXPECT_ERROR(
        MICROS_FDT_ERROR_ARGUMENT,
        micros_fdt_parse_memory_map(
            fixture.bytes,
            fixture.size,
            NULL
        )
    );
    return true;
}

static bool test_accepts_nonzero_property_padding(void)
{
    struct fdt_fixture fixture;
    struct micros_fdt_memory_map memory_map;

    build_fixture(&fixture, default_options());
    fixture.bytes[fixture.memory_device_type_padding] = 0xa5;
    EXPECT_ERROR(
        MICROS_FDT_OK,
        micros_fdt_parse_memory_map(
            fixture.bytes,
            fixture.size,
            &memory_map
        )
    );
    EXPECT_TRUE(memory_map.memory_range_count == 3);
    return true;
}

static bool test_rejects_truncated_header(void)
{
    struct fdt_fixture fixture;
    struct micros_fdt_memory_map memory_map;

    build_fixture(&fixture, default_options());
    EXPECT_ERROR(
        MICROS_FDT_ERROR_TRUNCATED,
        micros_fdt_parse_memory_map(
            fixture.bytes,
            FDT_HEADER_SIZE - 1,
            &memory_map
        )
    );
    return true;
}

static bool test_rejects_bad_magic(void)
{
    struct fdt_fixture fixture;
    struct micros_fdt_memory_map memory_map;

    build_fixture(&fixture, default_options());
    write_be32(fixture.bytes, 0, 0);
    EXPECT_ERROR(
        MICROS_FDT_ERROR_MAGIC,
        micros_fdt_parse_memory_map(
            fixture.bytes,
            fixture.size,
            &memory_map
        )
    );
    return true;
}

static bool test_rejects_incompatible_version(void)
{
    struct fdt_fixture fixture;
    struct micros_fdt_memory_map memory_map;

    build_fixture(&fixture, default_options());
    write_be32(fixture.bytes, 20, 16);
    EXPECT_ERROR(
        MICROS_FDT_ERROR_VERSION,
        micros_fdt_parse_memory_map(
            fixture.bytes,
            fixture.size,
            &memory_map
        )
    );
    return true;
}

static bool test_rejects_total_size_beyond_buffer(void)
{
    struct fdt_fixture fixture;
    struct micros_fdt_memory_map memory_map;

    build_fixture(&fixture, default_options());
    write_be32(fixture.bytes, 4, (uint32_t)(fixture.size + 1));
    EXPECT_ERROR(
        MICROS_FDT_ERROR_TRUNCATED,
        micros_fdt_parse_memory_map(
            fixture.bytes,
            fixture.size,
            &memory_map
        )
    );
    return true;
}

static bool test_rejects_overlapping_blocks(void)
{
    struct fdt_fixture fixture;
    struct micros_fdt_memory_map memory_map;
    uint32_t structure_offset;

    build_fixture(&fixture, default_options());
    structure_offset =
        ((uint32_t)fixture.bytes[8] << 24)
        | ((uint32_t)fixture.bytes[9] << 16)
        | ((uint32_t)fixture.bytes[10] << 8)
        | fixture.bytes[11];
    write_be32(fixture.bytes, 12, structure_offset + 4);
    EXPECT_ERROR(
        MICROS_FDT_ERROR_LAYOUT,
        micros_fdt_parse_memory_map(
            fixture.bytes,
            fixture.size,
            &memory_map
        )
    );
    return true;
}

static bool test_rejects_unterminated_reservation_map(void)
{
    struct fdt_fixture fixture;
    struct micros_fdt_memory_map memory_map;

    build_fixture(&fixture, default_options());
    write_be64(fixture.bytes, fixture.reservation_terminator, 1);
    write_be64(fixture.bytes, fixture.reservation_terminator + 8, 1);
    EXPECT_ERROR(
        MICROS_FDT_ERROR_LAYOUT,
        micros_fdt_parse_memory_map(
            fixture.bytes,
            fixture.size,
            &memory_map
        )
    );
    return true;
}

static bool test_rejects_unknown_structure_token(void)
{
    struct fdt_fixture fixture;
    struct micros_fdt_memory_map memory_map;

    build_fixture(&fixture, default_options());
    write_be32(fixture.bytes, fixture.first_token, 0xffffffff);
    EXPECT_ERROR(
        MICROS_FDT_ERROR_STRUCTURE,
        micros_fdt_parse_memory_map(
            fixture.bytes,
            fixture.size,
            &memory_map
        )
    );
    return true;
}

static bool test_rejects_bad_property_name_offset(void)
{
    struct fdt_fixture fixture;
    struct micros_fdt_memory_map memory_map;

    build_fixture(&fixture, default_options());
    write_be32(fixture.bytes, fixture.memory_reg_name, UINT32_MAX);
    EXPECT_ERROR(
        MICROS_FDT_ERROR_STRING,
        micros_fdt_parse_memory_map(
            fixture.bytes,
            fixture.size,
            &memory_map
        )
    );
    return true;
}

static bool test_rejects_oversized_property_name(void)
{
    struct fdt_fixture fixture;
    struct micros_fdt_memory_map memory_map;

    build_fixture(&fixture, default_options());
    memset(&fixture.bytes[fixture.strings_offset], 'a', 32);
    EXPECT_ERROR(
        MICROS_FDT_ERROR_STRING,
        micros_fdt_parse_memory_map(
            fixture.bytes,
            fixture.size,
            &memory_map
        )
    );
    return true;
}

static bool test_rejects_unsupported_root_cells(void)
{
    struct fdt_fixture fixture;
    struct micros_fdt_memory_map memory_map;

    build_fixture(&fixture, default_options());
    write_be32(fixture.bytes, fixture.root_address_cells_value, 3);
    EXPECT_ERROR(
        MICROS_FDT_ERROR_CELLS,
        micros_fdt_parse_memory_map(
            fixture.bytes,
            fixture.size,
            &memory_map
        )
    );
    return true;
}

static bool test_rejects_partial_reg_tuple(void)
{
    struct fdt_fixture fixture;
    struct fixture_options options = default_options();
    struct micros_fdt_memory_map memory_map;

    options.memory_reg_trim = 4;
    build_fixture(&fixture, options);
    EXPECT_ERROR(
        MICROS_FDT_ERROR_PROPERTY,
        micros_fdt_parse_memory_map(
            fixture.bytes,
            fixture.size,
            &memory_map
        )
    );
    return true;
}

static bool test_rejects_range_overflow(void)
{
    struct fdt_fixture fixture;
    struct micros_fdt_memory_map memory_map;

    build_fixture(&fixture, default_options());
    write_be64(
        fixture.bytes,
        fixture.memory_reg_value,
        UINT64_MAX - 15
    );
    write_be64(fixture.bytes, fixture.memory_reg_value + 8, 32);
    EXPECT_ERROR(
        MICROS_FDT_ERROR_RANGE,
        micros_fdt_parse_memory_map(
            fixture.bytes,
            fixture.size,
            &memory_map
        )
    );
    return true;
}

static bool test_rejects_memory_range_capacity_overflow(void)
{
    struct fdt_fixture fixture;
    struct fixture_options options = default_options();
    struct micros_fdt_memory_map memory_map;

    options.memory_tuple_count = MICROS_FDT_MAX_MEMORY_RANGES;
    build_fixture(&fixture, options);
    EXPECT_ERROR(
        MICROS_FDT_ERROR_CAPACITY,
        micros_fdt_parse_memory_map(
            fixture.bytes,
            fixture.size,
            &memory_map
        )
    );
    return true;
}

static bool test_rejects_excessive_tree_depth(void)
{
    struct fdt_fixture fixture;
    struct fixture_options options = default_options();
    struct micros_fdt_memory_map memory_map;

    options.include_memory = false;
    options.include_reserved_memory = false;
    options.nested_depth = MICROS_FDT_MAX_DEPTH + 1;
    build_fixture(&fixture, options);
    EXPECT_ERROR(
        MICROS_FDT_ERROR_DEPTH,
        micros_fdt_parse_memory_map(
            fixture.bytes,
            fixture.size,
            &memory_map
        )
    );
    return true;
}

static bool test_rejects_reserved_memory_cell_mismatch(void)
{
    struct fdt_fixture fixture;
    struct micros_fdt_memory_map memory_map;

    build_fixture(&fixture, default_options());
    write_be32(fixture.bytes, fixture.reserved_address_cells_value, 1);
    EXPECT_ERROR(
        MICROS_FDT_ERROR_CELLS,
        micros_fdt_parse_memory_map(
            fixture.bytes,
            fixture.size,
            &memory_map
        )
    );
    return true;
}

static bool test_rejects_nonempty_reserved_memory_ranges(void)
{
    struct fdt_fixture fixture;
    struct fixture_options options = default_options();
    struct micros_fdt_memory_map memory_map;

    options.reserved_ranges_nonempty = true;
    build_fixture(&fixture, options);
    EXPECT_ERROR(
        MICROS_FDT_ERROR_PROPERTY,
        micros_fdt_parse_memory_map(
            fixture.bytes,
            fixture.size,
            &memory_map
        )
    );
    return true;
}

static bool test_rejects_dynamic_reserved_memory(void)
{
    struct fdt_fixture fixture;
    struct fixture_options options = default_options();
    struct micros_fdt_memory_map memory_map;

    options.reserved_child_has_reg = false;
    build_fixture(&fixture, options);
    EXPECT_ERROR(
        MICROS_FDT_ERROR_PROPERTY,
        micros_fdt_parse_memory_map(
            fixture.bytes,
            fixture.size,
            &memory_map
        )
    );
    return true;
}

static bool test_requires_at_least_one_memory_range(void)
{
    struct fdt_fixture fixture;
    struct fixture_options options = default_options();
    struct micros_fdt_memory_map memory_map;

    options.include_memory = false;
    build_fixture(&fixture, options);
    EXPECT_ERROR(
        MICROS_FDT_ERROR_NO_MEMORY,
        micros_fdt_parse_memory_map(
            fixture.bytes,
            fixture.size,
            &memory_map
        )
    );
    return true;
}

struct test_case {
    const char *name;
    bool (*run)(void);
};

int main(void)
{
    static const struct test_case tests[] = {
        {"parses memory and reservations", test_parses_memory_and_reservations},
        {"rejects null arguments", test_rejects_null_arguments},
        {"accepts nonzero property padding", test_accepts_nonzero_property_padding},
        {"rejects truncated header", test_rejects_truncated_header},
        {"rejects bad magic", test_rejects_bad_magic},
        {"rejects incompatible version", test_rejects_incompatible_version},
        {"rejects total size beyond buffer", test_rejects_total_size_beyond_buffer},
        {"rejects overlapping blocks", test_rejects_overlapping_blocks},
        {"rejects unterminated reservation map", test_rejects_unterminated_reservation_map},
        {"rejects unknown structure token", test_rejects_unknown_structure_token},
        {"rejects bad property name offset", test_rejects_bad_property_name_offset},
        {"rejects oversized property name", test_rejects_oversized_property_name},
        {"rejects unsupported root cells", test_rejects_unsupported_root_cells},
        {"rejects partial reg tuple", test_rejects_partial_reg_tuple},
        {"rejects range overflow", test_rejects_range_overflow},
        {"rejects memory capacity overflow", test_rejects_memory_range_capacity_overflow},
        {"rejects excessive depth", test_rejects_excessive_tree_depth},
        {"rejects reserved-memory cell mismatch", test_rejects_reserved_memory_cell_mismatch},
        {"rejects nonempty reserved-memory ranges", test_rejects_nonempty_reserved_memory_ranges},
        {"rejects dynamic reserved memory", test_rejects_dynamic_reserved_memory},
        {"requires memory range", test_requires_at_least_one_memory_range},
    };
    size_t index;
    size_t failures = 0;

    printf("TAP version 13\n");
    for (index = 0; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        if (tests[index].run()) {
            printf("ok %zu - %s\n", index + 1, tests[index].name);
        } else {
            printf("not ok %zu - %s\n", index + 1, tests[index].name);
            ++failures;
        }
    }
    return failures == 0 ? 0 : 1;
}

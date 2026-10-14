#include "fdt_test_internal.h"

bool test_rejects_null_arguments(void)
{
    struct fdt_fixture fixture;
    struct micros_fdt_memory_map memory_map;

    fdt_test_build_fixture(&fixture, fdt_test_default_options());
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

bool test_accepts_nonzero_property_padding(void)
{
    struct fdt_fixture fixture;
    struct micros_fdt_memory_map memory_map;

    fdt_test_build_fixture(&fixture, fdt_test_default_options());
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

bool test_rejects_truncated_header(void)
{
    struct fdt_fixture fixture;
    struct micros_fdt_memory_map memory_map;

    fdt_test_build_fixture(&fixture, fdt_test_default_options());
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

bool test_rejects_bad_magic(void)
{
    struct fdt_fixture fixture;
    struct micros_fdt_memory_map memory_map;

    fdt_test_build_fixture(&fixture, fdt_test_default_options());
    fdt_test_write_be32(fixture.bytes, 0, 0);
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

bool test_rejects_incompatible_version(void)
{
    struct fdt_fixture fixture;
    struct micros_fdt_memory_map memory_map;

    fdt_test_build_fixture(&fixture, fdt_test_default_options());
    fdt_test_write_be32(fixture.bytes, 20, 16);
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

bool test_rejects_total_size_beyond_buffer(void)
{
    struct fdt_fixture fixture;
    struct micros_fdt_memory_map memory_map;

    fdt_test_build_fixture(&fixture, fdt_test_default_options());
    fdt_test_write_be32(fixture.bytes, 4, (uint32_t)(fixture.size + 1));
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

bool test_rejects_overlapping_blocks(void)
{
    struct fdt_fixture fixture;
    struct micros_fdt_memory_map memory_map;
    uint32_t structure_offset;

    fdt_test_build_fixture(&fixture, fdt_test_default_options());
    structure_offset =
        ((uint32_t)fixture.bytes[8] << 24)
        | ((uint32_t)fixture.bytes[9] << 16)
        | ((uint32_t)fixture.bytes[10] << 8)
        | fixture.bytes[11];
    fdt_test_write_be32(fixture.bytes, 12, structure_offset + 4);
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

bool test_rejects_unterminated_reservation_map(void)
{
    struct fdt_fixture fixture;
    struct micros_fdt_memory_map memory_map;

    fdt_test_build_fixture(&fixture, fdt_test_default_options());
    fdt_test_write_be64(fixture.bytes, fixture.reservation_terminator, 1);
    fdt_test_write_be64(fixture.bytes, fixture.reservation_terminator + 8, 1);
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

bool test_rejects_unknown_structure_token(void)
{
    struct fdt_fixture fixture;
    struct micros_fdt_memory_map memory_map;

    fdt_test_build_fixture(&fixture, fdt_test_default_options());
    fdt_test_write_be32(fixture.bytes, fixture.first_token, 0xffffffff);
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

bool test_rejects_bad_property_name_offset(void)
{
    struct fdt_fixture fixture;
    struct micros_fdt_memory_map memory_map;

    fdt_test_build_fixture(&fixture, fdt_test_default_options());
    fdt_test_write_be32(fixture.bytes, fixture.memory_reg_name, UINT32_MAX);
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

bool test_rejects_oversized_property_name(void)
{
    struct fdt_fixture fixture;
    struct micros_fdt_memory_map memory_map;

    fdt_test_build_fixture(&fixture, fdt_test_default_options());
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

bool test_rejects_excessive_tree_depth(void)
{
    struct fdt_fixture fixture;
    struct fixture_options options = fdt_test_default_options();
    struct micros_fdt_memory_map memory_map;

    options.include_memory = false;
    options.include_reserved_memory = false;
    options.nested_depth = MICROS_FDT_MAX_DEPTH + 1;
    fdt_test_build_fixture(&fixture, options);
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

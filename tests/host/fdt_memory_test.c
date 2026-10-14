#include "fdt_test_internal.h"

bool test_parses_memory_and_reservations(void)
{
    struct fdt_fixture fixture;
    struct micros_fdt_memory_map memory_map;

    fdt_test_build_fixture(&fixture, fdt_test_default_options());
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

bool test_rejects_unsupported_root_cells(void)
{
    struct fdt_fixture fixture;
    struct micros_fdt_memory_map memory_map;

    fdt_test_build_fixture(&fixture, fdt_test_default_options());
    fdt_test_write_be32(fixture.bytes, fixture.root_address_cells_value, 3);
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

bool test_rejects_partial_reg_tuple(void)
{
    struct fdt_fixture fixture;
    struct fixture_options options = fdt_test_default_options();
    struct micros_fdt_memory_map memory_map;

    options.memory_reg_trim = 4;
    fdt_test_build_fixture(&fixture, options);
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

bool test_rejects_memory_name_with_conflicting_type(void)
{
    struct fdt_fixture fixture;
    struct fixture_options options = fdt_test_default_options();
    struct micros_fdt_memory_map memory_map;

    options.memory_name_has_conflicting_type = true;
    fdt_test_build_fixture(&fixture, options);
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

bool test_skips_disabled_named_memory(void)
{
    struct fdt_fixture fixture;
    struct fixture_options options = fdt_test_default_options();
    struct micros_fdt_memory_map memory_map;

    options.named_memory_is_disabled = true;
    fdt_test_build_fixture(&fixture, options);
    EXPECT_ERROR(
        MICROS_FDT_OK,
        micros_fdt_parse_memory_map(
            fixture.bytes,
            fixture.size,
            &memory_map
        )
    );
    EXPECT_TRUE(memory_map.memory_range_count == 1);
    EXPECT_TRUE(
        memory_map.memory_ranges[0].base == UINT64_C(0x100000000)
    );
    return true;
}

bool test_skips_disabled_device_type_memory(void)
{
    struct fdt_fixture fixture;
    struct fixture_options options = fdt_test_default_options();
    struct micros_fdt_memory_map memory_map;

    options.device_memory_is_disabled = true;
    fdt_test_build_fixture(&fixture, options);
    EXPECT_ERROR(
        MICROS_FDT_OK,
        micros_fdt_parse_memory_map(
            fixture.bytes,
            fixture.size,
            &memory_map
        )
    );
    EXPECT_TRUE(memory_map.memory_range_count == 2);
    EXPECT_TRUE(
        memory_map.memory_ranges[0].base == UINT64_C(0x80000000)
    );
    EXPECT_TRUE(
        memory_map.memory_ranges[1].base == UINT64_C(0x81000000)
    );
    return true;
}

bool test_rejects_range_overflow(void)
{
    struct fdt_fixture fixture;
    struct micros_fdt_memory_map memory_map;

    fdt_test_build_fixture(&fixture, fdt_test_default_options());
    fdt_test_write_be64(
        fixture.bytes,
        fixture.memory_reg_value,
        UINT64_MAX - 15
    );
    fdt_test_write_be64(fixture.bytes, fixture.memory_reg_value + 8, 32);
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

bool test_rejects_memory_range_capacity_overflow(void)
{
    struct fdt_fixture fixture;
    struct fixture_options options = fdt_test_default_options();
    struct micros_fdt_memory_map memory_map;

    options.memory_tuple_count = MICROS_FDT_MAX_MEMORY_RANGES;
    fdt_test_build_fixture(&fixture, options);
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

bool test_disabled_memory_does_not_consume_capacity(void)
{
    struct fdt_fixture fixture;
    struct fixture_options options = fdt_test_default_options();
    struct micros_fdt_memory_map memory_map;

    options.memory_tuple_count = MICROS_FDT_MAX_MEMORY_RANGES;
    options.device_memory_is_disabled = true;
    fdt_test_build_fixture(&fixture, options);
    EXPECT_ERROR(
        MICROS_FDT_OK,
        micros_fdt_parse_memory_map(
            fixture.bytes,
            fixture.size,
            &memory_map
        )
    );
    EXPECT_TRUE(
        memory_map.memory_range_count == MICROS_FDT_MAX_MEMORY_RANGES
    );
    return true;
}

bool test_rejects_reserved_memory_cell_mismatch(void)
{
    struct fdt_fixture fixture;
    struct micros_fdt_memory_map memory_map;

    fdt_test_build_fixture(&fixture, fdt_test_default_options());
    fdt_test_write_be32(fixture.bytes, fixture.reserved_address_cells_value, 1);
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

bool test_rejects_nonempty_reserved_memory_ranges(void)
{
    struct fdt_fixture fixture;
    struct fixture_options options = fdt_test_default_options();
    struct micros_fdt_memory_map memory_map;

    options.reserved_ranges_nonempty = true;
    fdt_test_build_fixture(&fixture, options);
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

bool test_rejects_reserved_memory_as_usable_memory(void)
{
    struct fdt_fixture fixture;
    struct fixture_options options = fdt_test_default_options();
    struct micros_fdt_memory_map memory_map;

    options.reserved_memory_claims_memory = true;
    fdt_test_build_fixture(&fixture, options);
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

bool test_rejects_duplicate_device_type(void)
{
    struct fdt_fixture fixture;
    struct fixture_options options = fdt_test_default_options();
    struct micros_fdt_memory_map memory_map;

    options.reserved_memory_claims_memory = true;
    options.reserved_memory_overrides_device_type = true;
    fdt_test_build_fixture(&fixture, options);
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

bool test_rejects_dynamic_reserved_memory(void)
{
    struct fdt_fixture fixture;
    struct fixture_options options = fdt_test_default_options();
    struct micros_fdt_memory_map memory_map;

    options.reserved_child_has_reg = false;
    fdt_test_build_fixture(&fixture, options);
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

bool test_requires_at_least_one_memory_range(void)
{
    struct fdt_fixture fixture;
    struct fixture_options options = fdt_test_default_options();
    struct micros_fdt_memory_map memory_map;

    options.include_memory = false;
    fdt_test_build_fixture(&fixture, options);
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

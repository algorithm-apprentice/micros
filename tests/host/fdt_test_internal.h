#ifndef MICROS_TESTS_HOST_FDT_TEST_INTERNAL_H
#define MICROS_TESTS_HOST_FDT_TEST_INTERNAL_H

#include "micros/fdt.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
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
    bool memory_name_has_conflicting_type;
    bool named_memory_is_disabled;
    bool device_memory_is_disabled;
    bool include_reserved_memory;
    bool reserved_memory_claims_memory;
    bool reserved_memory_overrides_device_type;
    bool reserved_ranges_nonempty;
    bool reserved_child_has_reg;
};

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

void fdt_test_write_be32(uint8_t *bytes, size_t offset, uint32_t value);
void fdt_test_write_be64(uint8_t *bytes, size_t offset, uint64_t value);
struct fixture_options fdt_test_default_options(void);
void fdt_test_build_fixture(
    struct fdt_fixture *fixture,
    struct fixture_options options
);

bool test_parses_memory_and_reservations(void);
bool test_rejects_null_arguments(void);
bool test_accepts_nonzero_property_padding(void);
bool test_rejects_truncated_header(void);
bool test_rejects_bad_magic(void);
bool test_rejects_incompatible_version(void);
bool test_rejects_total_size_beyond_buffer(void);
bool test_rejects_overlapping_blocks(void);
bool test_rejects_unterminated_reservation_map(void);
bool test_rejects_unknown_structure_token(void);
bool test_rejects_bad_property_name_offset(void);
bool test_rejects_oversized_property_name(void);
bool test_rejects_unsupported_root_cells(void);
bool test_rejects_partial_reg_tuple(void);
bool test_rejects_memory_name_with_conflicting_type(void);
bool test_skips_disabled_named_memory(void);
bool test_skips_disabled_device_type_memory(void);
bool test_rejects_range_overflow(void);
bool test_rejects_memory_range_capacity_overflow(void);
bool test_disabled_memory_does_not_consume_capacity(void);
bool test_rejects_excessive_tree_depth(void);
bool test_rejects_reserved_memory_cell_mismatch(void);
bool test_rejects_nonempty_reserved_memory_ranges(void);
bool test_rejects_reserved_memory_as_usable_memory(void);
bool test_rejects_duplicate_device_type(void);
bool test_rejects_dynamic_reserved_memory(void);
bool test_requires_at_least_one_memory_range(void);

#endif

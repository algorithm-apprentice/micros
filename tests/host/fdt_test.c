#include "fdt_test_internal.h"

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
        {"rejects conflicting memory device_type", test_rejects_memory_name_with_conflicting_type},
        {"skips disabled named memory", test_skips_disabled_named_memory},
        {"skips disabled device_type memory", test_skips_disabled_device_type_memory},
        {"rejects range overflow", test_rejects_range_overflow},
        {"rejects memory capacity overflow", test_rejects_memory_range_capacity_overflow},
        {"disabled memory preserves capacity", test_disabled_memory_does_not_consume_capacity},
        {"rejects excessive depth", test_rejects_excessive_tree_depth},
        {"rejects reserved-memory cell mismatch", test_rejects_reserved_memory_cell_mismatch},
        {"rejects nonempty reserved-memory ranges", test_rejects_nonempty_reserved_memory_ranges},
        {"rejects reserved-memory as usable memory", test_rejects_reserved_memory_as_usable_memory},
        {"rejects duplicate device_type", test_rejects_duplicate_device_type},
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
